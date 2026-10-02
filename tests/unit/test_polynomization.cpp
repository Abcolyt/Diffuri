// ============================================================================
// tests/unit/test_polynomization.cpp
//
// Unit-, end-to-end- и property-тесты модуля polynomization.
//
// Реализация модуля ещё не написана; тесты фиксируют ожидаемое поведение
// публичного API (Clone, IsPolynomial, FindTarget, Substitute,
// TimeDerivative, Polynomize) и постусловия этапа.
// ============================================================================
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "input/input.h"
#include "input/parser.h"
#include "normalize/normalize.h"
#include "order_reducer/order_reducer.h"
#include "polynomization/polynomization.h"
#include "simplify/simplify.h"

namespace diffuri {
    namespace {

        constexpr double kPi = 3.14159265358979323846;
        constexpr double kE = 2.71828182845904523536;

        // ------------------------------------------------------------------------
        // Хелперы
        // ------------------------------------------------------------------------

        ExprPtr ParsedExpr(const std::string& text) { return ParseExpression(text); }

        RawSystem ReduceText(const std::string& text) {
            RawSystem sys = ParseSystem(text);
            NormalizeSystem(sys);
            OrderReducer(sys);
            return sys;
        }

        bool AllRhsPolynomial(const RawSystem& sys) {
            for (const auto& eq : sys.equations) {
                if (!IsPolynomial(*eq.rhs)) return false;
            }
            return true;
        }

        bool HasFunction(const RawSystem& sys, const std::string& name) {
            return std::find(sys.functions.begin(), sys.functions.end(), name)
                != sys.functions.end();
        }

        bool HasOrderZeroIC(const RawSystem& sys, const std::string& name) {
            for (const auto& ic : sys.initial_conditions) {
                if (ic.function_name == name && ic.order == 0) return true;
            }
            return false;
        }

        double ICValue(const RawSystem& sys,
            const std::string& name,
            int order) {
            for (const auto& ic : sys.initial_conditions) {
                if (ic.function_name == name && ic.order == order) return ic.value;
            }
            return std::numeric_limits<double>::quiet_NaN();
        }

        // ========================================================================
        // Clone
        // ========================================================================

        TEST(Clone, Number) {
            auto e = MakeNumber(3.5);
            auto c = Clone(*e);
            EXPECT_TRUE(ExprEquals(*e, *c));
        }

        TEST(Clone, Function) {
            auto e = MakeFunction("x");
            auto c = Clone(*e);
            EXPECT_TRUE(ExprEquals(*e, *c));
        }

        TEST(Clone, Constant) {
            auto e = MakeConstant("pi", kPi);
            auto c = Clone(*e);
            EXPECT_TRUE(ExprEquals(*e, *c));
        }

        TEST(Clone, Derivative) {
            auto e = MakeDerivative("x", 2);
            auto c = Clone(*e);
            EXPECT_TRUE(ExprEquals(*e, *c));
        }

        TEST(Clone, Unary) {
            auto e = MakeUnary(Unary::Op::Neg, MakeFunction("x"));
            auto c = Clone(*e);
            EXPECT_TRUE(ExprEquals(*e, *c));
        }

        TEST(Clone, Binary) {
            auto e = MakeBinary(Binary::Op::Mul,
                MakeFunction("x"), MakeNumber(2.0));
            auto c = Clone(*e);
            EXPECT_TRUE(ExprEquals(*e, *c));
        }

        TEST(Clone, Call) {
            auto e = MakeCallArgs("sin", MakeFunction("x"));
            auto c = Clone(*e);
            EXPECT_TRUE(ExprEquals(*e, *c));
        }

        TEST(Clone, DeepCopyIsolation) {
            auto e = ParsedExpr("x + y * sin(t)");
            auto c = Clone(*e);
            // Изменяем клон — оригинал не должен измениться.
            // Простейшая проверка: разные адреса и структурное равенство до правки.
            EXPECT_NE(e.get(), c.get());
            EXPECT_TRUE(ExprEquals(*e, *c));
        }

        // ========================================================================
        // IsPolynomial
        // ========================================================================

        TEST(IsPolynomial, NumberTrue) {
            auto e = MakeNumber(5.0);
            EXPECT_TRUE(IsPolynomial(*e));
        }

        TEST(IsPolynomial, FunctionTrue) {
            auto e = MakeFunction("x");
            EXPECT_TRUE(IsPolynomial(*e));
        }

        TEST(IsPolynomial, ConstantTrue) {
            auto e = MakeConstant("pi", kPi);
            EXPECT_TRUE(IsPolynomial(*e));
        }

        TEST(IsPolynomial, SumOfPolynomialsTrue) {
            auto e = ParsedExpr("x + 1");
            EXPECT_TRUE(IsPolynomial(*e));
        }

        TEST(IsPolynomial, MulOfPolynomialsTrue) {
            auto e = ParsedExpr("x * y");
            EXPECT_TRUE(IsPolynomial(*e));
        }

        TEST(IsPolynomial, SubTrue) {
            auto e = ParsedExpr("x - y - 1");
            EXPECT_TRUE(IsPolynomial(*e));
        }

        TEST(IsPolynomial, PowWithIntegerNonNegativeTrue) {
            auto e = ParsedExpr("x^2");
            EXPECT_TRUE(IsPolynomial(*e));
        }

        TEST(IsPolynomial, PowZeroTrue) {
            auto e = ParsedExpr("x^0");
            EXPECT_TRUE(IsPolynomial(*e));
        }

        TEST(IsPolynomial, PowNegativeFalse) {
            auto e = ParsedExpr("x^(-1)");
            EXPECT_FALSE(IsPolynomial(*e));
        }

        TEST(IsPolynomial, PowFractionalFalse) {
            auto e = ParsedExpr("x^0.5");
            EXPECT_FALSE(IsPolynomial(*e));
        }

        TEST(IsPolynomial, PowSymbolicExponentFalse) {
            auto e = ParsedExpr("x^y");
            EXPECT_FALSE(IsPolynomial(*e));
        }

        TEST(IsPolynomial, DivFalse) {
            auto e = ParsedExpr("x / y");
            EXPECT_FALSE(IsPolynomial(*e));
        }

        TEST(IsPolynomial, CallFalse) {
            auto e = ParsedExpr("sin(x)");
            EXPECT_FALSE(IsPolynomial(*e));
        }

        TEST(IsPolynomial, SumWithCallFalse) {
            auto e = ParsedExpr("x * x + sin(y)");
            EXPECT_FALSE(IsPolynomial(*e));
        }

        TEST(IsPolynomial, UnaryFalse) {
            // Unary присутствует в дереве без Simplify.
            auto e = MakeUnary(Unary::Op::Neg, MakeFunction("x"));
            EXPECT_FALSE(IsPolynomial(*e));
        }

        TEST(IsPolynomial, DerivativeFalse) {
            auto e = MakeDerivative("x", 1);
            EXPECT_FALSE(IsPolynomial(*e));
        }

        // ========================================================================
        // Substitute
        // ========================================================================

        TEST(Substitute, SimpleVarReplacement) {
            auto tree = ParsedExpr("x + x");
            auto target = MakeFunction("x");
            auto repl = MakeFunction("y");
            auto result = Substitute(*tree, *target, *repl);
            auto expected = ParsedExpr("y + y");
            EXPECT_TRUE(ExprEquals(*result, *expected));
        }

        TEST(Substitute, CallReplacementInMultiplePlaces) {
            auto tree = ParsedExpr("sin(x) + sin(x)");
            auto target = ParsedExpr("sin(x)");
            auto repl = MakeFunction("v");
            auto result = Substitute(*tree, *target, *repl);
            auto expected = ParsedExpr("v + v");
            EXPECT_TRUE(ExprEquals(*result, *expected));
        }

        TEST(Substitute, NoMatchKeepsTree) {
            auto tree = ParsedExpr("x + y");
            auto target = ParsedExpr("sin(x)");
            auto repl = MakeFunction("v");
            auto result = Substitute(*tree, *target, *repl);
            EXPECT_TRUE(ExprEquals(*result, *tree));
        }

        TEST(Substitute, ReplacementInsideCallArgument) {
            auto tree = ParsedExpr("sin(x) + cos(x)");
            auto target = MakeFunction("x");
            auto repl = MakeFunction("u");
            auto result = Substitute(*tree, *target, *repl);
            auto expected = ParsedExpr("sin(u) + cos(u)");
            EXPECT_TRUE(ExprEquals(*result, *expected));
        }

        // ========================================================================
        // FindTarget
        // ========================================================================

        TEST(FindTarget, ReturnsFirstCallWithPolyArgs) {
            auto tree = ParsedExpr("x + sin(x)");
            auto t = FindTarget(*tree);
            ASSERT_NE(t, nullptr);
            EXPECT_EQ(ToString(*t), "sin(x)");
        }

        TEST(FindTarget, ReturnsInnerCallForNested) {
            auto tree = ParsedExpr("sin(cos(x))");
            auto t = FindTarget(*tree);
            ASSERT_NE(t, nullptr);
            EXPECT_EQ(ToString(*t), "cos(x)");
        }

        TEST(FindTarget, ReturnsNullForPurePoly) {
            auto tree = ParsedExpr("x + y");
            auto t = FindTarget(*tree);
            EXPECT_EQ(t, nullptr);
        }

        TEST(FindTarget, PostOrderFirstFromLeft) {
            auto tree = ParsedExpr("sin(x) + cos(y)");
            auto t = FindTarget(*tree);
            ASSERT_NE(t, nullptr);
            EXPECT_EQ(ToString(*t), "sin(x)");
        }

        TEST(FindTarget, SkipsCallWithNonPolyArgument) {
            // sin(cos(x)) — sin не годится (аргумент не полином),
            // первым подходящим является cos(x).
            auto tree = ParsedExpr("sin(sin(x))");
            auto t = FindTarget(*tree);
            ASSERT_NE(t, nullptr);
            EXPECT_EQ(ToString(*t), "sin(x)");  // внутренний
        }

        // ========================================================================
        // TimeDerivative
        // ========================================================================

        TEST(TimeDerivative, NumberIsZero) {
            RawSystem sys = ReduceText("x' = -x\nx(0) = 1\n");
            auto e = MakeNumber(5.0);
            auto d = TimeDerivative(*e, sys);
            EXPECT_EQ(ToString(*d), "0");
        }

        TEST(TimeDerivative, ConstantIsZero) {
            RawSystem sys = ReduceText("x' = -x\nx(0) = 1\n");
            auto e = MakeConstant("pi", kPi);
            auto d = TimeDerivative(*e, sys);
            EXPECT_EQ(ToString(*d), "0");
        }

        TEST(TimeDerivative, FunctionUsesRhs) {
            RawSystem sys = ReduceText("x' = -x\nx(0) = 1\n");
            auto e = MakeFunction("x");
            auto d = TimeDerivative(*e, sys);
            auto expected = ParsedExpr("-x");
            expected = Simplify(std::move(expected));
            EXPECT_TRUE(ExprEquals(*d, *expected));
        }

        TEST(TimeDerivative, SumRule) {
            RawSystem sys = ReduceText("x' = y\ny' = -x\nx(0) = 1\ny(0) = 0\n");
            auto e = ParsedExpr("x + y");
            auto d = TimeDerivative(*e, sys);
            // d/dt(x + y) = y - x
            auto expected = ParsedExpr("y - x");
            expected = Simplify(std::move(expected));
            EXPECT_TRUE(ExprEquals(*d, *expected));
        }

        TEST(TimeDerivative, ProductRule) {
            RawSystem sys = ReduceText("x' = y\ny' = -x\nx(0) = 1\ny(0) = 0\n");
            auto e = ParsedExpr("x * y");
            auto d = TimeDerivative(*e, sys);
            // d/dt(x*y) = x' * y + x * y' = y*y + x*(-x) = y^2 - x^2
            auto expected = ParsedExpr("y^2 - x^2");
            expected = Simplify(std::move(expected));
            EXPECT_TRUE(ExprEquals(*d, *expected));
        }

        TEST(TimeDerivative, PowerRuleInteger) {
            RawSystem sys = ReduceText("x' = y\ny' = -x\nx(0) = 1\ny(0) = 0\n");
            auto e = ParsedExpr("x^2");
            auto d = TimeDerivative(*e, sys);
            // d/dt(x^2) = 2*x*x' = 2*x*y
            auto expected = ParsedExpr("2 * x * y");
            expected = Simplify(std::move(expected));
            EXPECT_TRUE(ExprEquals(*d, *expected));
        }

        // ========================================================================
        // Polynomize: end-to-end
        // ========================================================================

        TEST(Polynomize, AlreadyPolynomialNoChange) {
            RawSystem sys = ReduceText("x' = x^2\nx(0) = 1\n");
            const std::size_t before = sys.equations.size();
            auto aux = Polynomize(sys);
            EXPECT_TRUE(aux.empty());
            EXPECT_EQ(sys.equations.size(), before);
            EXPECT_TRUE(AllRhsPolynomial(sys));
        }

        TEST(Polynomize, AlreadyPolynomialLinearNoChange) {
            RawSystem sys = ReduceText("x' = y\ny' = -x\nx(0) = 1\ny(0) = 0\n");
            const std::size_t before = sys.equations.size();
            auto aux = Polynomize(sys);
            EXPECT_TRUE(aux.empty());
            EXPECT_EQ(sys.equations.size(), before);
        }

        TEST(Polynomize, SinIntroducesTwoVariables) {
            RawSystem sys = ReduceText("x' = sin(x)\nx(0) = 0\n");
            auto aux = Polynomize(sys);

            EXPECT_EQ(aux.size(), 2u);
            ASSERT_TRUE(aux.count("v_1"));
            ASSERT_TRUE(aux.count("v_2"));
            EXPECT_EQ(ToString(*aux.at("v_1")), "sin(x)");
            EXPECT_EQ(ToString(*aux.at("v_2")), "cos(x)");

            EXPECT_EQ(sys.equations.size(), 3u);   // x', v_1', v_2'
            EXPECT_TRUE(HasFunction(sys, "v_1"));
            EXPECT_TRUE(HasFunction(sys, "v_2"));
            EXPECT_TRUE(AllRhsPolynomial(sys));
        }

        TEST(Polynomize, OnlySinIsReplacedNotPolyParts) {
            RawSystem sys = ReduceText("x' = x^2 + sin(x)\nx(0) = 0\n");
            auto aux = Polynomize(sys);

            EXPECT_EQ(aux.size(), 2u);
            // x' остаётся с x^2, но sin(x) → v_1.
            const std::string rhs0 = ToString(*sys.equations[0].rhs);
            EXPECT_NE(rhs0.find("x^2"), std::string::npos);
            EXPECT_NE(rhs0.find("v_1"), std::string::npos);
            EXPECT_TRUE(AllRhsPolynomial(sys));
        }

        TEST(Polynomize, ExpIntroducesOneVariable) {
            RawSystem sys = ReduceText("x' = exp(x)\nx(0) = 1\n");
            auto aux = Polynomize(sys);

            EXPECT_EQ(aux.size(), 1u);
            ASSERT_TRUE(aux.count("v_1"));
            EXPECT_EQ(ToString(*aux.at("v_1")), "exp(x)");

            EXPECT_EQ(sys.equations.size(), 2u);
            EXPECT_TRUE(AllRhsPolynomial(sys));
        }

        TEST(Polynomize, LnIntroducesTwoVariables) {
            RawSystem sys = ReduceText("x' = ln(x)\nx(0) = 1\n");
            auto aux = Polynomize(sys);

            EXPECT_EQ(aux.size(), 2u);
            ASSERT_TRUE(aux.count("v_1"));
            ASSERT_TRUE(aux.count("v_2"));
            EXPECT_EQ(ToString(*aux.at("v_1")), "ln(x)");
            EXPECT_EQ(ToString(*aux.at("v_2")), "inv(x)");
            EXPECT_TRUE(AllRhsPolynomial(sys));
        }

        TEST(Polynomize, ShIntroducesTwoVariables) {
            RawSystem sys = ReduceText("x' = sh(x)\nx(0) = 0\n");
            auto aux = Polynomize(sys);

            EXPECT_EQ(aux.size(), 2u);
            ASSERT_TRUE(aux.count("v_1"));
            ASSERT_TRUE(aux.count("v_2"));
            EXPECT_EQ(ToString(*aux.at("v_1")), "sh(x)");
            EXPECT_EQ(ToString(*aux.at("v_2")), "ch(x)");
            EXPECT_TRUE(AllRhsPolynomial(sys));
        }

        TEST(Polynomize, SinAndCosTogetherUseOneExpansion) {
            RawSystem sys = ReduceText("x' = sin(x) + cos(x)\nx(0) = 0\n");
            auto aux = Polynomize(sys);

            // Одна и та же пара v_1 = sin(x), v_2 = cos(x), не две пары.
            EXPECT_EQ(aux.size(), 2u);
            EXPECT_TRUE(AllRhsPolynomial(sys));
        }

        TEST(Polynomize, SharedSubexpressionCachedOnce) {
            RawSystem sys = ReduceText(
                "x' = sin(y) + sin(y) + sin(z)\n"
                "y' = 0\n"
                "z' = 0\n"
                "x(0) = 0\n"
                "y(0) = 0\n"
                "z(0) = 0\n");
            auto aux = Polynomize(sys);

            // sin(y) и sin(z) — два разных ключа, каждый со своим расширением {sin, cos}.
            EXPECT_EQ(aux.size(), 4u);
            // sin(y) должен быть введён один раз.
            int count_sin_y = 0;
            for (const auto& kv : aux) {
                if (ToString(*kv.second) == "sin(y)") ++count_sin_y;
            }
            EXPECT_EQ(count_sin_y, 1);
            EXPECT_TRUE(AllRhsPolynomial(sys));
        }

        TEST(Polynomize, NestedSinNeedsSeveralIterations) {
            RawSystem sys = ReduceText("x' = sin(sin(x))\nx(0) = 0\n");
            auto aux = Polynomize(sys);

            EXPECT_FALSE(aux.empty());
            EXPECT_TRUE(AllRhsPolynomial(sys));
            // В карте обязательно должна быть запись cos(x) (внутренний разбор).
            bool has_cos_x = false;
            for (const auto& kv : aux) {
                if (ToString(*kv.second) == "cos(x)") { has_cos_x = true; break; }
            }
            EXPECT_TRUE(has_cos_x);
        }

        TEST(Polynomize, ExistingVNamesAreAvoided) {
            // v_1 уже занята как пользовательская функция.
            RawSystem sys = ReduceText(
                "x' = sin(x)\n"
                "v_1' = 0\n"
                "x(0) = 0\n"
                "v_1(0) = 0\n");
            auto aux = Polynomize(sys);
            EXPECT_FALSE(aux.empty());
            // Новые переменные не должны носить имя "v_1".
            EXPECT_FALSE(aux.count("v_1"));
            EXPECT_TRUE(AllRhsPolynomial(sys));
        }

        // ========================================================================
        // Polynomize: IC для новых переменных
        // ========================================================================

        TEST(PolynomizeIC, SinAtZero) {
            RawSystem sys = ReduceText("x' = sin(x)\nx(0) = 0\n");
            auto aux = Polynomize(sys);
            ASSERT_EQ(aux.size(), 2u);

            EXPECT_NEAR(ICValue(sys, "v_1", 0), std::sin(0.0), 1e-12);
            EXPECT_NEAR(ICValue(sys, "v_2", 0), std::cos(0.0), 1e-12);
            EXPECT_TRUE(HasOrderZeroIC(sys, "v_1"));
            EXPECT_TRUE(HasOrderZeroIC(sys, "v_2"));
        }

        TEST(PolynomizeIC, SinAtPiOverTwo) {
            RawSystem sys = ReduceText("x' = sin(x)\nx(0) = 1.5707963267948966\n");
            auto aux = Polynomize(sys);
            ASSERT_EQ(aux.size(), 2u);

            EXPECT_NEAR(ICValue(sys, "v_1", 0), 1.0, 1e-9);
            EXPECT_NEAR(ICValue(sys, "v_2", 0), 0.0, 1e-9);
        }

        TEST(PolynomizeIC, ExpAtOne) {
            RawSystem sys = ReduceText("x' = exp(x)\nx(0) = 1\n");
            auto aux = Polynomize(sys);
            ASSERT_EQ(aux.size(), 1u);

            EXPECT_NEAR(ICValue(sys, "v_1", 0), kE, 1e-12);
        }

        TEST(PolynomizeIC, LnAtOne) {
            RawSystem sys = ReduceText("x' = ln(x)\nx(0) = 1\n");
            auto aux = Polynomize(sys);
            ASSERT_EQ(aux.size(), 2u);

            EXPECT_NEAR(ICValue(sys, "v_1", 0), std::log(1.0), 1e-12);
            EXPECT_NEAR(ICValue(sys, "v_2", 0), 1.0 / 1.0, 1e-12);
        }

        // ========================================================================
        // Ошибки Polynomize
        // ========================================================================

        TEST(PolynomizeErrors, NotFirstOrderThrows) {
            RawSystem sys = ParseSystem("x'' = -x\nx(0) = 1\nx'(0) = 0\n");
            NormalizeSystem(sys);
            // OrderReducer НЕ вызываем — система остаётся второго порядка.
            EXPECT_THROW(Polynomize(sys), PolynomizeError);
        }

        TEST(PolynomizeErrors, NotFirstOrderMessage) {
            RawSystem sys = ParseSystem("x'' = -x\nx(0) = 1\nx'(0) = 0\n");
            NormalizeSystem(sys);
            try {
                Polynomize(sys);
                FAIL() << "expected PolynomizeError";
            }
            catch (const PolynomizeError& e) {
                const std::string msg = e.what();
                // Сообщение должно упоминать порядок / ReduceOrder.
                const bool ok =
                    msg.find("порядок") != std::string::npos ||
                    msg.find("ReduceOrder") != std::string::npos ||
                    msg.find("перв") != std::string::npos;
                EXPECT_TRUE(ok) << "message: " << msg;
            }
        }

        TEST(PolynomizeErrors, UnknownLibraryFunctionThrows) {
            RawSystem sys = ReduceText("x' = myfunc(x)\nx(0) = 0\n");
            EXPECT_THROW(Polynomize(sys), PolynomizeError);
        }

        TEST(PolynomizeErrors, UnknownLibraryFunctionMessage) {
            RawSystem sys = ReduceText("x' = myfunc(x)\nx(0) = 0\n");
            try {
                Polynomize(sys);
                FAIL() << "expected PolynomizeError";
            }
            catch (const PolynomizeError& e) {
                const std::string msg = e.what();
                EXPECT_NE(msg.find("myfunc"), std::string::npos);
            }
        }

        TEST(PolynomizeErrors, DifferentT0InICThrows) {
            RawSystem sys = ParseSystem(
                "x' = sin(y)\n"
                "y' = -x\n"
                "x(0) = 0\n"
                "y(1) = 0\n");
            NormalizeSystem(sys);
            OrderReducer(sys);
            EXPECT_THROW(Polynomize(sys), PolynomizeError);
        }

        // ========================================================================
        // Интеграция с ReduceOrder
        // ========================================================================

        TEST(PolynomizeIntegration, SecondOrderNoSinPolyAfterReduce) {
            RawSystem sys = ReduceText("x'' = -x\nx(0) = 1\nx'(0) = 0\n");
            auto aux = Polynomize(sys);
            EXPECT_TRUE(aux.empty());
            EXPECT_TRUE(AllRhsPolynomial(sys));
        }

        TEST(PolynomizeIntegration, SecondOrderWithSinReducedThenPolynomized) {
            RawSystem sys = ReduceText(
                "x'' = sin(x)\n"
                "x(0) = 0\n"
                "x'(0) = 1\n");
            auto aux = Polynomize(sys);
            EXPECT_FALSE(aux.empty());
            EXPECT_TRUE(AllRhsPolynomial(sys));
            // IC для x_1 (введена ReduceOrder) должна остаться с order 0.
            EXPECT_TRUE(HasOrderZeroIC(sys, "x_1"));
        }

        TEST(PolynomizeIntegration, ReducedAndPolynomizedHasICsForAllNewVars) {
            RawSystem sys = ReduceText(
                "x'' = sin(x)\n"
                "x(0) = 0\n"
                "x'(0) = 1\n");
            auto aux = Polynomize(sys);
            for (const auto& kv : aux) {
                EXPECT_TRUE(HasOrderZeroIC(sys, kv.first))
                    << "missing IC for " << kv.first;
            }
        }

        // ========================================================================
        // Property-based
        // ========================================================================

        TEST(PropertyPolynomize, P1_Idempotent) {
            RawSystem sys = ReduceText(
                "x' = sin(x) + x^2\n"
                "x(0) = 0\n");
            Polynomize(sys);
            const std::string once = ToString(sys);

            Polynomize(sys);
            const std::string twice = ToString(sys);

            EXPECT_EQ(once, twice);
        }

        TEST(PropertyPolynomize, P2_AllRhsPolynomial) {
            const char* kCases[] = {
                "x' = sin(x)\nx(0) = 0\n",
                "x' = exp(x)\nx(0) = 0\n",
                "x' = ln(x)\nx(0) = 1\n",
                "x' = sh(x)\nx(0) = 0\n",
                "x' = sin(x) + cos(x) + x\nx(0) = 0\n",
                "x' = sin(cos(x))\nx(0) = 0\n",
                "x' = x^2 + sin(x)\nx(0) = 0\n",
            };
            for (const char* text : kCases) {
                SCOPED_TRACE(std::string("input: ") + text);
                RawSystem sys = ReduceText(text);
                Polynomize(sys);
                EXPECT_TRUE(AllRhsPolynomial(sys))
                    << "not polynomial after Polynomize";
            }
        }

        TEST(PropertyPolynomize, P3_OriginalLhsUnchanged) {
            RawSystem sys = ReduceText(
                "x' = sin(x)\n"
                "y' = -x\n"
                "x(0) = 0\n"
                "y(0) = 0\n");
            const std::size_t orig_count = 2;
            ASSERT_GE(sys.equations.size(), orig_count);

            std::vector<std::string> lhs_before;
            for (std::size_t i = 0; i < orig_count; ++i) {
                lhs_before.push_back(ToString(*sys.equations[i].lhs));
            }

            Polynomize(sys);

            for (std::size_t i = 0; i < orig_count; ++i) {
                EXPECT_EQ(ToString(*sys.equations[i].lhs), lhs_before[i]);
            }
        }

        TEST(PropertyPolynomize, P4_EachNewVariableHasIC) {
            const char* kCases[] = {
                "x' = sin(x)\nx(0) = 0\n",
                "x' = exp(x)\nx(0) = 1\n",
                "x' = ln(x)\nx(0) = 1\n",
                "x' = sin(cos(x))\nx(0) = 0\n",
            };
            for (const char* text : kCases) {
                SCOPED_TRACE(std::string("input: ") + text);
                RawSystem sys = ReduceText(text);
                auto aux = Polynomize(sys);
                for (const auto& kv : aux) {
                    EXPECT_TRUE(HasOrderZeroIC(sys, kv.first))
                        << "missing IC for " << kv.first;
                }
            }
        }

        TEST(PropertyPolynomize, P5_AuxNamesRegisteredInFunctions) {
            RawSystem sys = ReduceText("x' = sin(x)\nx(0) = 0\n");
            auto aux = Polynomize(sys);
            for (const auto& kv : aux) {
                EXPECT_TRUE(HasFunction(sys, kv.first))
                    << "aux name not in sys.functions: " << kv.first;
            }
        }

    } // namespace
} // namespace diffuri