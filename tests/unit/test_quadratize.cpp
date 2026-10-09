// ============================================================================
// tests/unit/test_quadratize.cpp
//
// Тесты модуля quadratize: IsQuadratic и Quadratize.
//
// Структура файла:
//   1. Хелперы и общие утилиты
//   2. IsQuadratic — предикат квадратичности
//   3. QuadratizeSingle — базовые сценарии одной функции
//   4. QuadratizeCaching — повторное использование мономов
//   5. QuadratizeIdempotence — повторный вызов — no-op
//   6. QuadratizeDeep — глубокие цепочки (x^7 .. x^20)
//   7. QuadratizeMultivariate — многомерные мономы
//   8. QuadratizeCoefficients — числовые коэффициенты
//   9. Начальные условия (QuadratizeIC + QuadratizeICVariety)
//  10. Постусловия и структура (QuadratizePost + QuadratizeStructure)
//  11. QuadratizeErrors — ошибки входа
//  12. QuadratizeIntegration — стык с Polynomize / ReduceOrder
//  13. PropertyQuadratize — P1..P9
//  14. QuadratizeNonAutonomous — DISABLED (t не обрабатывается)
//
// Отдельно: тесты Plan A (соответствующие ТЗ №3.5, §10) сохранены
// без изменения имён — только реорганизованы.
// ============================================================================
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "input/input.h"
#include "input/parser.h"
#include "normalize/normalize.h"
#include "order_reducer/order_reducer.h"
#include "polynomization/polynomization.h"
#include "quadratize/quadratize.h"
#include "simplify/simplify.h"

namespace diffuri {
    namespace {

        constexpr double kPi = 3.14159265358979323846;

        // ========================================================================
        // 1. Хелперы
        // ========================================================================

        ExprPtr ParsedExpr(const std::string& text) {
            return ParseExpression(text);
        }

        // Полный пайплайн до Polynomize включительно.
        RawSystem PipelineText(const std::string& text) {
            RawSystem sys = ParseSystem(text);
            NormalizeSystem(sys);
            ReduceOrder(sys);
            Polynomize(sys);
            return sys;
        }

        // Пайплайн без Polynomize — для тестов, где RHS заведомо
        // не полиномиален и мы хотим убедиться, что Quadratize упадёт.
        RawSystem PipelineThroughOrderReducer(const std::string& text) {
            RawSystem sys = ParseSystem(text);
            NormalizeSystem(sys);
            ReduceOrder(sys);
            return sys;
        }

        bool AllRhsQuadratic(const RawSystem& sys) {
            for (const auto& eq : sys.equations) {
                if (!IsQuadratic(*eq.rhs)) return false;
            }
            return true;
        }

        bool AllLhsFirstOrderDerivatives(const RawSystem& sys) {
            for (const auto& eq : sys.equations) {
                auto* d = std::get_if<Derivative>(&eq.lhs->value);
                if (d == nullptr || d->order != 1) return false;
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
                if (ic.function_name == name && ic.order == order) {
                    return ic.value;
                }
            }
            return std::numeric_limits<double>::quiet_NaN();
        }

        // Является ли значение карты произведением двух Function?
        // (без Simplify — ровно Mul(Function, Function)).
        bool IsMulOfTwoFunctions(const Expr& e) {
            auto* b = std::get_if<Binary>(&e.value);
            if (b == nullptr || b->op != Binary::Op::Mul) return false;
            return std::holds_alternative<Function>(b->lhs->value)
                && std::holds_alternative<Function>(b->rhs->value);
        }

        // RHS после Quadratize содержит только допустимые узлы:
        // Number, Constant, Function, Add/Sub/Mul/Pow.
        bool ContainsOnlyAllowedNodes(const Expr& e) {
            return std::visit([&](const auto& n) -> bool {
                using T = std::decay_t<decltype(n)>;
                if constexpr (std::is_same_v<T, Number>)     return true;
                if constexpr (std::is_same_v<T, Constant>)   return true;
                if constexpr (std::is_same_v<T, Function>)   return true;
                if constexpr (std::is_same_v<T, Binary>) {
                    if (n.op == Binary::Op::Add || n.op == Binary::Op::Sub ||
                        n.op == Binary::Op::Mul || n.op == Binary::Op::Pow) {
                        return ContainsOnlyAllowedNodes(*n.lhs)
                            && ContainsOnlyAllowedNodes(*n.rhs);
                    }
                    return false;
                }
                return false;
                }, e.value);
        }

        // ========================================================================
        // 2. IsQuadratic — предикат квадратичности
        // ========================================================================

        TEST(IsQuadratic, NumberTrue) {
            auto e = MakeNumber(3.5);
            EXPECT_TRUE(IsQuadratic(*e));
        }

        TEST(IsQuadratic, FunctionTrue) {
            auto e = MakeFunction("x");
            EXPECT_TRUE(IsQuadratic(*e));
        }

        TEST(IsQuadratic, ConstantTrue) {
            auto e = MakeConstant("pi", kPi);
            EXPECT_TRUE(IsQuadratic(*e));
        }

        TEST(IsQuadratic, SumWithNumberTrue) {
            auto e = ParsedExpr("x + 1");
            EXPECT_TRUE(IsQuadratic(*e));
        }

        TEST(IsQuadratic, ProductOfTwoFunctionsTrue) {
            auto e = ParsedExpr("x * y");
            EXPECT_TRUE(IsQuadratic(*e));
        }

        TEST(IsQuadratic, SquareTrue) {
            auto e = ParsedExpr("x^2");
            EXPECT_TRUE(IsQuadratic(*e));
        }

        TEST(IsQuadratic, CubeFalse) {
            auto e = ParsedExpr("x^3");
            EXPECT_FALSE(IsQuadratic(*e));
        }

        TEST(IsQuadratic, FifthPowerFalse) {
            auto e = ParsedExpr("x^5");
            EXPECT_FALSE(IsQuadratic(*e));
        }

        TEST(IsQuadratic, TripleProductFalse) {
            auto e = ParsedExpr("x * y * z");
            EXPECT_FALSE(IsQuadratic(*e));
        }

        TEST(IsQuadratic, XSquaredTimesYFalse) {
            // Степень 3, хоть и через произведение.
            auto e = ParsedExpr("x^2 * y");
            EXPECT_FALSE(IsQuadratic(*e));
        }

        TEST(IsQuadratic, SumOfSquaresTrue) {
            auto e = ParsedExpr("x^2 + y^2");
            EXPECT_TRUE(IsQuadratic(*e));
        }

        TEST(IsQuadratic, CallFalse) {
            auto e = ParsedExpr("sin(x)");
            EXPECT_FALSE(IsQuadratic(*e));
        }

        TEST(IsQuadratic, DivFalse) {
            auto e = ParsedExpr("1 / x");
            EXPECT_FALSE(IsQuadratic(*e));
        }

        TEST(IsQuadratic, DerivativeFalse) {
            auto e = MakeDerivative("x", 1);
            EXPECT_FALSE(IsQuadratic(*e));
        }

        TEST(IsQuadratic, UnaryFalse) {
            // Unary без Simplify — присутствует в дереве как узел.
            auto e = MakeUnary(Unary::Op::Neg, MakeFunction("x"));
            EXPECT_FALSE(IsQuadratic(*e));
        }

        TEST(IsQuadratic, ConstantCoefficientProductTrue) {
            // 2 * x * y — Mul трёх множителей, но степень 2.
            auto e = ParsedExpr("2 * x * y");
            EXPECT_TRUE(IsQuadratic(*e));
        }

        TEST(IsQuadratic, PowWithNonNumberExponentFalse) {
            auto e = ParsedExpr("x^y");
            EXPECT_FALSE(IsQuadratic(*e));
        }

        TEST(IsQuadratic, PowWithNegativeExponentFalse) {
            auto e = ParsedExpr("x^(-1)");
            EXPECT_FALSE(IsQuadratic(*e));
        }

        TEST(IsQuadratic, PowWithFractionalExponentFalse) {
            auto e = ParsedExpr("x^0.5");
            EXPECT_FALSE(IsQuadratic(*e));
        }

        // ========================================================================
        // 3. QuadratizeSingle — базовые сценарии
        // ========================================================================

        TEST(QuadratizeSingle, CubeIntroducesOneVariable) {
            RawSystem sys = PipelineText("x' = x^3\nx(0) = 1\n");
            auto aux = Quadratize(sys);

            EXPECT_EQ(aux.size(), 1u);
            ASSERT_TRUE(aux.count("q_1"));
            EXPECT_TRUE(IsMulOfTwoFunctions(*aux.at("q_1")));
            EXPECT_TRUE(AllRhsQuadratic(sys));
        }

        TEST(QuadratizeSingle, FifthPowerIntroducesFourVariables) {
            RawSystem sys = PipelineText("x' = x^5\nx(0) = 1\n");
            auto aux = Quadratize(sys);

            EXPECT_EQ(aux.size(), 4u);
            for (const char* name : { "q_1", "q_2", "q_3", "q_4" }) {
                ASSERT_TRUE(aux.count(name)) << "missing " << name;
                EXPECT_TRUE(IsMulOfTwoFunctions(*aux.at(name)))
                    << "aux " << name << ": " << ToString(*aux.at(name));
            }
            EXPECT_TRUE(AllRhsQuadratic(sys));
        }

        TEST(QuadratizeSingle, SquarePlusCubeIntroducesOneVariable) {
            // x^2 остаётся как есть (уже квадратичен),
            // для x^3 вводится q_1 = x^2, x^3 переписывается как x * q_1.
            RawSystem sys = PipelineText("x' = x^2 + x^3\nx(0) = 1\n");
            auto aux = Quadratize(sys);

            EXPECT_EQ(aux.size(), 1u);
            EXPECT_TRUE(AllRhsQuadratic(sys));
        }

        TEST(QuadratizeSingle, TripleProductIsQuadraticized) {
            RawSystem sys = PipelineText(
                "x' = x * y * z\n"
                "y' = 0\n"
                "z' = 0\n"
                "x(0) = 1\n"
                "y(0) = 1\n"
                "z(0) = 1\n");
            auto aux = Quadratize(sys);

            EXPECT_GE(aux.size(), 1u);
            EXPECT_TRUE(AllRhsQuadratic(sys));
        }

        TEST(QuadratizeSingle, LinearSystemUnchanged) {
            RawSystem sys = PipelineText(
                "x' = y\n"
                "y' = -x\n"
                "x(0) = 1\ny(0) = 0\n");
            auto aux = Quadratize(sys);
            EXPECT_TRUE(aux.empty());
            EXPECT_TRUE(AllRhsQuadratic(sys));
        }

        // ========================================================================
        // 4. QuadratizeCaching — повторное использование мономов
        // ========================================================================

        TEST(QuadratizeCaching, SameMonomialOnce) {
            // x^3 + x^3 — после Simplify это 2 * x^3, но в любом случае
            // одна переменная для x^3.
            RawSystem sys = PipelineText("x' = x^3 + x^3\nx(0) = 1\n");
            auto aux = Quadratize(sys);
            EXPECT_EQ(aux.size(), 1u);
            EXPECT_TRUE(AllRhsQuadratic(sys));
        }

        TEST(QuadratizeCaching, SharedMonomialAcrossEquations) {
            RawSystem sys = PipelineText(
                "x' = x^3\n"
                "y' = x^3\n"
                "x(0) = 1\ny(0) = 1\n");
            auto aux = Quadratize(sys);
            EXPECT_EQ(aux.size(), 1u);
            EXPECT_TRUE(AllRhsQuadratic(sys));
        }

        TEST(QuadratizeCaching, SquareTimesSquareIsQuadraticized) {
            // x^2 * y^2 — степень 4. Минимальная квадратизация не требуется;
            // достаточно, чтобы после Quadratize RHS был квадратичен.
            RawSystem sys = PipelineText(
                "x' = x^2 * y^2\n"
                "y' = 0\n"
                "x(0) = 1\ny(0) = 1\n");
            auto aux = Quadratize(sys);
            EXPECT_GE(aux.size(), 1u);
            EXPECT_TRUE(AllRhsQuadratic(sys));
        }

        // ========================================================================
        // 5. QuadratizeIdempotence — повторный вызов — no-op
        // ========================================================================

        TEST(QuadratizeIdempotence, AlreadyQuadraticNoOp) {
            RawSystem sys = PipelineText("x' = x^2\nx(0) = 1\n");
            const std::size_t before = sys.equations.size();
            auto aux = Quadratize(sys);
            EXPECT_TRUE(aux.empty());
            EXPECT_EQ(sys.equations.size(), before);
            EXPECT_TRUE(AllRhsQuadratic(sys));
        }

        TEST(QuadratizeIdempotence, SecondCallReturnsEmptyMap) {
            RawSystem sys = PipelineText("x' = x^5\nx(0) = 1\n");
            auto aux1 = Quadratize(sys);
            EXPECT_FALSE(aux1.empty());
            auto aux2 = Quadratize(sys);
            EXPECT_TRUE(aux2.empty());
        }

        TEST(QuadratizeIdempotence, SystemUnchangedAfterSecondCall) {
            RawSystem sys = PipelineText("x' = x^5\nx(0) = 1\n");
            Quadratize(sys);
            const std::string once = ToString(sys);
            Quadratize(sys);
            EXPECT_EQ(ToString(sys), once);
        }

        // ========================================================================
        // 6. QuadratizeDeep — глубокие цепочки одной переменной
        // ========================================================================

        TEST(QuadratizeDeep, SeventhPower) {
            RawSystem sys = PipelineText("x' = x^7\nx(0) = 1\n");
            auto aux = Quadratize(sys);
            EXPECT_EQ(aux.size(), 6u);   // q_1..q_6
            EXPECT_TRUE(AllRhsQuadratic(sys));
            for (int i = 1; i <= 6; ++i) {
                EXPECT_TRUE(aux.count("q_" + std::to_string(i)))
                    << "missing q_" << i;
            }
        }

        TEST(QuadratizeDeep, TwentiethPower) {
            RawSystem sys = PipelineText("x' = x^20\nx(0) = 1\n");
            auto aux = Quadratize(sys);
            EXPECT_EQ(aux.size(), 19u);
            EXPECT_TRUE(AllRhsQuadratic(sys));
        }

        TEST(QuadratizeDeep, TenthPowerICChain) {
            RawSystem sys = PipelineText("x' = x^10\nx(0) = 1\n");
            auto aux = Quadratize(sys);
            for (int i = 1; i <= 9; ++i) {
                EXPECT_NEAR(ICValue(sys, "q_" + std::to_string(i), 0),
                    1.0, 1e-12);
            }
        }

        // ========================================================================
        // 7. QuadratizeMultivariate — многомерные мономы
        // ========================================================================

        TEST(QuadratizeMultivariate, CubeTimesCube) {
            // x^3 * y^3 — степень 6.
            RawSystem sys = PipelineText(
                "x' = x^3 * y^3\n"
                "y' = 0\n"
                "x(0) = 1\ny(0) = 1\n");
            auto aux = Quadratize(sys);
            EXPECT_GE(aux.size(), 2u);
            EXPECT_TRUE(AllRhsQuadratic(sys));
        }

        TEST(QuadratizeMultivariate, FourthTimesSquare) {
            // x^4 * y^2 — степень 6.
            RawSystem sys = PipelineText(
                "x' = x^4 * y^2\n"
                "y' = 0\n"
                "x(0) = 1\ny(0) = 1\n");
            auto aux = Quadratize(sys);
            EXPECT_TRUE(AllRhsQuadratic(sys));
        }

        TEST(QuadratizeMultivariate, FourDistinctFunctions) {
            // x * y * z * w — степень 4.
            RawSystem sys = PipelineText(
                "x' = x * y * z * w\n"
                "y' = 0\nz' = 0\nw' = 0\n"
                "x(0)=1\ny(0)=1\nz(0)=1\nw(0)=1\n");
            auto aux = Quadratize(sys);
            EXPECT_GE(aux.size(), 2u);
            EXPECT_TRUE(AllRhsQuadratic(sys));
        }

        TEST(QuadratizeMultivariate, SquareTimesYTimesZ) {
            // x^2 * y * z — степень 4.
            RawSystem sys = PipelineText(
                "x' = x^2 * y * z\n"
                "y' = 0\nz' = 0\n"
                "x(0)=1\ny(0)=1\nz(0)=1\n");
            auto aux = Quadratize(sys);
            EXPECT_TRUE(AllRhsQuadratic(sys));
        }

        // ========================================================================
        // 8. QuadratizeCoefficients — числовые коэффициенты
        // ========================================================================

        TEST(QuadratizeCoefficients, TwoTimesXCubed) {
            RawSystem sys = PipelineText("x' = 2 * x^3\nx(0) = 1\n");
            auto aux = Quadratize(sys);
            EXPECT_EQ(aux.size(), 1u);
            EXPECT_TRUE(AllRhsQuadratic(sys));
        }

        TEST(QuadratizeCoefficients, NegativeTimesFifthPower) {
            RawSystem sys = PipelineText("x' = -5 * x^5\nx(0) = 1\n");
            auto aux = Quadratize(sys);
            EXPECT_EQ(aux.size(), 4u);
            EXPECT_TRUE(AllRhsQuadratic(sys));
        }

        TEST(QuadratizeCoefficients, SumWithCoefficients) {
            // 3*x^3 - 2*x^5 — минимум не гарантируется (ТЗ №3.5, §8 и §15),
            // поэтому проверяем только постусловия.
            RawSystem sys = PipelineText("x' = 3 * x^3 - 2 * x^5\nx(0) = 1\n");
            auto aux = Quadratize(sys);
            EXPECT_GE(aux.size(), 4u);   // минимум x^2, x^3, x^4, x^5
            EXPECT_TRUE(AllRhsQuadratic(sys));
            EXPECT_TRUE(HasFunction(sys, "x"));
        }

        // ========================================================================
        // 9. Начальные условия
        // ========================================================================

        TEST(QuadratizeIC, SquareOfTwo) {
            RawSystem sys = PipelineText("x' = x^3\nx(0) = 2\n");
            auto aux = Quadratize(sys);
            ASSERT_EQ(aux.size(), 1u);
            EXPECT_NEAR(ICValue(sys, "q_1", 0), 4.0, 1e-12);
        }

        TEST(QuadratizeIC, PowersOfHalf) {
            RawSystem sys = PipelineText("x' = x^5\nx(0) = 0.5\n");
            auto aux = Quadratize(sys);
            ASSERT_EQ(aux.size(), 4u);
            EXPECT_NEAR(ICValue(sys, "q_1", 0), 0.25, 1e-12);
            EXPECT_NEAR(ICValue(sys, "q_2", 0), 0.125, 1e-12);
            EXPECT_NEAR(ICValue(sys, "q_3", 0), 0.0625, 1e-12);
            EXPECT_NEAR(ICValue(sys, "q_4", 0), 0.03125, 1e-12);
        }

        TEST(QuadratizeIC, AllNewICsHaveOrderZero) {
            RawSystem sys = PipelineText("x' = x^5\nx(0) = 1\n");
            auto aux = Quadratize(sys);
            for (const auto& ic : sys.initial_conditions) {
                if (aux.count(ic.function_name)) {
                    EXPECT_EQ(ic.order, 0)
                        << "aux " << ic.function_name << " has order "
                        << ic.order;
                }
            }
        }

        TEST(QuadratizeICVariety, NonZeroT0) {
            RawSystem sys = PipelineText("x' = x^3\nx(1) = 2\n");
            auto aux = Quadratize(sys);
            ASSERT_EQ(aux.size(), 1u);
            EXPECT_NEAR(ICValue(sys, "q_1", 0), 4.0, 1e-12);
            for (const auto& ic : sys.initial_conditions) {
                if (ic.function_name == "q_1") EXPECT_DOUBLE_EQ(ic.t0, 1.0);
            }
        }

        TEST(QuadratizeICVariety, NegativeValue) {
            RawSystem sys = PipelineText("x' = x^3\nx(0) = -2\n");
            auto aux = Quadratize(sys);
            EXPECT_NEAR(ICValue(sys, "q_1", 0), 4.0, 1e-12);
        }

        TEST(QuadratizeICVariety, OddPowerNegativeValue) {
            // (-2)^1..5 = -2, 4, -8, 16, -32.
            RawSystem sys = PipelineText("x' = x^5\nx(0) = -2\n");
            auto aux = Quadratize(sys);
            EXPECT_NEAR(ICValue(sys, "q_1", 0), 4.0, 1e-12);
            EXPECT_NEAR(ICValue(sys, "q_2", 0), -8.0, 1e-12);
            EXPECT_NEAR(ICValue(sys, "q_3", 0), 16.0, 1e-12);
            EXPECT_NEAR(ICValue(sys, "q_4", 0), -32.0, 1e-12);
        }

        TEST(QuadratizeICVariety, MultiVariable) {
            RawSystem sys = PipelineText(
                "x' = x^2 * y^2\n"
                "y' = 0\n"
                "x(0) = 2\ny(0) = 3\n");
            auto aux = Quadratize(sys);
            for (const auto& kv : aux) {
                EXPECT_FALSE(std::isnan(ICValue(sys, kv.first, 0)));
            }
            EXPECT_TRUE(AllRhsQuadratic(sys));
        }

        // ========================================================================
        // 10. Постусловия и структура
        // ========================================================================

        TEST(QuadratizePost, AllRhsQuadraticForVarietyOfSystems) {
            const char* kCases[] = {
                "x' = x^3\nx(0) = 1\n",
                "x' = x^5\nx(0) = 1\n",
                "x' = x^2 + x^3\nx(0) = 1\n",
                "x' = x * y * z\ny' = 0\nz' = 0\nx(0)=1\ny(0)=1\nz(0)=1\n",
                "x' = x^2 * y^2\ny' = 0\nx(0)=1\ny(0)=1\n",
            };
            for (const char* text : kCases) {
                SCOPED_TRACE(std::string("input: ") + text);
                RawSystem sys = PipelineText(text);
                Quadratize(sys);
                EXPECT_TRUE(AllRhsQuadratic(sys));
            }
        }

        TEST(QuadratizePost, LhsAreFirstOrderDerivatives) {
            RawSystem sys = PipelineText("x' = x^5\nx(0) = 1\n");
            Quadratize(sys);
            EXPECT_TRUE(AllLhsFirstOrderDerivatives(sys));
        }

        TEST(QuadratizePost, FunctionsContainsOriginalsAndNew) {
            RawSystem sys = PipelineText("x' = x^5\nx(0) = 1\n");
            auto aux = Quadratize(sys);

            EXPECT_TRUE(HasFunction(sys, "x"));
            for (const auto& kv : aux) {
                EXPECT_TRUE(HasFunction(sys, kv.first))
                    << "aux name not registered in sys.functions: " << kv.first;
            }
        }

        TEST(QuadratizePost, EachNewVariableHasIC) {
            RawSystem sys = PipelineText("x' = x^5\nx(0) = 1\n");
            auto aux = Quadratize(sys);
            for (const auto& kv : aux) {
                EXPECT_TRUE(HasOrderZeroIC(sys, kv.first))
                    << "missing IC for " << kv.first;
            }
        }

        TEST(QuadratizePost, AuxValuesAreMulOfTwoFunctions) {
            RawSystem sys = PipelineText("x' = x^5\nx(0) = 1\n");
            auto aux = Quadratize(sys);
            for (const auto& kv : aux) {
                EXPECT_TRUE(IsMulOfTwoFunctions(*kv.second))
                    << "aux " << kv.first << " value: " << ToString(*kv.second);
            }
        }

        TEST(QuadratizeStructure, EquationsAppendedInOrder) {
            RawSystem sys = PipelineText("x' = x^5\nx(0) = 1\n");
            const std::size_t n_orig = sys.equations.size();
            auto aux = Quadratize(sys);

            EXPECT_EQ(sys.equations.size(), n_orig + aux.size());
            for (std::size_t i = 0; i < aux.size(); ++i) {
                auto* d = std::get_if<Derivative>(
                    &sys.equations[n_orig + i].lhs->value);
                ASSERT_NE(d, nullptr);
                EXPECT_EQ(d->order, 1);
                const std::string expected = "q_" + std::to_string(i + 1);
                EXPECT_EQ(d->function_name, expected)
                    << "aux equation " << i << " has wrong lhs";
            }
        }

        TEST(QuadratizeStructure, AuxFunctionsAppendedInOrder) {
            RawSystem sys = PipelineText("x' = x^5\nx(0) = 1\n");
            auto aux = Quadratize(sys);
            ASSERT_GE(sys.functions.size(), aux.size());
            const std::size_t n_orig = sys.functions.size() - aux.size();
            std::size_t idx = 0;
            for (auto it = aux.begin(); it != aux.end(); ++it, ++idx) {
                EXPECT_EQ(sys.functions[n_orig + idx], it->first);
            }
        }

        TEST(QuadratizeStructure, OriginalFunctionsOrderPreserved) {
            RawSystem sys = PipelineText(
                "x' = x^3\n"
                "y' = y^3\n"
                "z' = 0\n"
                "x(0)=1\ny(0)=1\nz(0)=1\n");
            const auto before = sys.functions;
            Quadratize(sys);
            ASSERT_GE(sys.functions.size(), before.size());
            for (std::size_t i = 0; i < before.size(); ++i) {
                EXPECT_EQ(sys.functions[i], before[i]);
            }
        }

        TEST(QuadratizeStructure, OriginalLhsUnchanged) {
            RawSystem sys = PipelineText(
                "x' = x^3\n"
                "y' = y^3\n"
                "x(0)=1\ny(0)=1\n");
            std::vector<std::string> before;
            for (const auto& eq : sys.equations) {
                before.push_back(ToString(*eq.lhs));
            }
            const std::size_t n = before.size();
            Quadratize(sys);
            for (std::size_t i = 0; i < n; ++i) {
                EXPECT_EQ(ToString(*sys.equations[i].lhs), before[i]);
            }
        }

        // ========================================================================
        // 10.1. Общие мономы в одном RHS и между уравнениями
        // ========================================================================

        TEST(QuadratizeSharing, SameSquareInTwoCubics) {
            // x^3 * y + x^5 — общий q_1 = x^2 между разными кусками.
            RawSystem sys = PipelineText(
                "x' = x^3 * y + x^5\n"
                "y' = 0\n"
                "x(0)=1\ny(0)=1\n");
            auto aux = Quadratize(sys);
            EXPECT_TRUE(AllRhsQuadratic(sys));
            int q_x2_count = 0;
            for (const auto& kv : aux) {
                if (ToString(*kv.second) == "(x * x)") ++q_x2_count;
            }
            EXPECT_EQ(q_x2_count, 1);
        }

        TEST(QuadratizeSharing, SameCubicAcrossThreeEquations) {
            RawSystem sys = PipelineText(
                "x' = x^3\n"
                "y' = x^3\n"
                "z' = x^3\n"
                "x(0)=1\ny(0)=1\nz(0)=1\n");
            auto aux = Quadratize(sys);
            EXPECT_EQ(aux.size(), 1u);
            EXPECT_TRUE(AllRhsQuadratic(sys));
        }

        // ========================================================================
        // 11. QuadratizeErrors — ошибки входа
        // ========================================================================

        TEST(QuadratizeErrors, NotFirstOrderThrows) {
            RawSystem sys = ParseSystem("x'' = x^3\nx(0) = 1\nx'(0) = 0\n");
            NormalizeSystem(sys);
            // ReduceOrder НЕ вызываем — система остаётся второго порядка.
            EXPECT_THROW(Quadratize(sys), QuadratizeError);
        }

        TEST(QuadratizeErrors, NonPolynomialRhsThrows) {
            // sin(x) без Polynomize — RHS не полиномиален.
            RawSystem sys = PipelineThroughOrderReducer(
                "x' = sin(x)\nx(0) = 0\n");
            EXPECT_THROW(Quadratize(sys), QuadratizeError);
        }

        TEST(QuadratizeErrors, Q1NameCollisionUsesUnderscore) {
            // q_1 уже занято как пользовательская функция — новые
            // переменные должны получить префикс "_".
            RawSystem sys = PipelineText(
                "x' = x^5\n"
                "q_1' = 0\n"
                "x(0) = 1\n"
                "q_1(0) = 0\n");
            auto aux = Quadratize(sys);

            EXPECT_FALSE(aux.empty());
            EXPECT_FALSE(aux.count("q_1"));
            EXPECT_TRUE(aux.count("_q_1"));
            for (const auto& kv : aux) {
                EXPECT_NE(kv.first, "q_1");
            }
            EXPECT_TRUE(AllRhsQuadratic(sys));
        }

        TEST(QuadratizeErrors, BothQ1AndUnderscoreQ1CollideThrows) {
            RawSystem sys = PipelineText(
                "x' = x^5\n"
                "q_1' = 0\n"
                "_q_1' = 0\n"
                "x(0) = 1\n"
                "q_1(0) = 0\n"
                "_q_1(0) = 0\n");
            EXPECT_THROW(Quadratize(sys), QuadratizeError);
        }

        TEST(QuadratizeErrors, DifferentT0InICThrows) {
            RawSystem sys = PipelineText(
                "x' = x^3\n"
                "y' = 0\n"
                "x(0) = 1\n"
                "y(1) = 0\n");
            EXPECT_THROW(Quadratize(sys), QuadratizeError);
        }

        // ========================================================================
        // 12. QuadratizeIntegration — стык с Polynomize / ReduceOrder
        // ========================================================================

        TEST(QuadratizeIntegration, FifthPowerPolynomizeNoOpThenQuadratize) {
            RawSystem sys = PipelineText("x' = x^5\nx(0) = 1\n");
            auto aux = Quadratize(sys);
            EXPECT_FALSE(aux.empty());
            EXPECT_TRUE(AllRhsQuadratic(sys));
        }

        TEST(QuadratizeIntegration, SquareTimesSin) {
            // Polynomize вводит v_1 = sin(x); RHS становится x^2 * v_1.
            // Quadratize должен добить систему до квадратичной.
            RawSystem sys = PipelineText("x' = x^2 * sin(x)\nx(0) = 0\n");
            auto aux = Quadratize(sys);
            EXPECT_TRUE(AllRhsQuadratic(sys));
        }

        TEST(QuadratizeIntegration, SecondOrderCubeAfterReduce) {
            RawSystem sys = PipelineText("x'' = x^3\nx(0) = 1\nx'(0) = 0\n");
            auto aux = Quadratize(sys);
            EXPECT_FALSE(aux.empty());
            EXPECT_TRUE(AllRhsQuadratic(sys));
        }

        TEST(QuadratizeIntegration, SecondOrderWithSinThenCube) {
            RawSystem sys = PipelineText(
                "x'' = sin(x) + x^3\n"
                "x(0) = 0\n"
                "x'(0) = 1\n");
            auto aux = Quadratize(sys);
            EXPECT_TRUE(AllRhsQuadratic(sys));
        }

        // ========================================================================
        // 13. PropertyQuadratize — P1..P9
        // ========================================================================

        TEST(PropertyQuadratize, P1_Idempotent) {
            RawSystem sys = PipelineText("x' = x^5\nx(0) = 1\n");
            Quadratize(sys);
            const std::string once = ToString(sys);

            auto aux2 = Quadratize(sys);
            EXPECT_TRUE(aux2.empty());
            EXPECT_EQ(ToString(sys), once);
        }

        TEST(PropertyQuadratize, P2_AllRhsQuadratic) {
            const char* kCases[] = {
                "x' = x^3\nx(0) = 1\n",
                "x' = x^5\nx(0) = 1\n",
                "x' = x^2 + x^3\nx(0) = 1\n",
                "x' = x * y * z\ny' = 0\nz' = 0\nx(0)=1\ny(0)=1\nz(0)=1\n",
                "x' = x^2 * y^2\ny' = 0\nx(0)=1\ny(0)=1\n",
                "x' = x^4 + y^5\ny' = 0\nx(0)=1\ny(0)=1\n",
            };
            for (const char* text : kCases) {
                SCOPED_TRACE(std::string("input: ") + text);
                RawSystem sys = PipelineText(text);
                Quadratize(sys);
                EXPECT_TRUE(AllRhsQuadratic(sys));
            }
        }

        TEST(PropertyQuadratize, P3_OriginalFunctionsPreserved) {
            RawSystem sys = PipelineText(
                "x' = x^5\n"
                "y' = -x\n"
                "x(0) = 1\ny(0) = 0\n");
            Quadratize(sys);
            EXPECT_TRUE(HasFunction(sys, "x"));
            EXPECT_TRUE(HasFunction(sys, "y"));
        }

        TEST(PropertyQuadratize, P4_EachNewVariableHasIC) {
            const char* kCases[] = {
                "x' = x^3\nx(0) = 1\n",
                "x' = x^5\nx(0) = 1\n",
                "x' = x^2 * y^2\ny' = 0\nx(0)=1\ny(0)=1\n",
                "x' = x * y * z\ny' = 0\nz' = 0\nx(0)=1\ny(0)=1\nz(0)=1\n",
            };
            for (const char* text : kCases) {
                SCOPED_TRACE(std::string("input: ") + text);
                RawSystem sys = PipelineText(text);
                auto aux = Quadratize(sys);
                for (const auto& kv : aux) {
                    EXPECT_TRUE(HasOrderZeroIC(sys, kv.first))
                        << "missing IC for " << kv.first;
                }
            }
        }

        TEST(PropertyQuadratize, P5_AuxValueIsMulOfTwoFunctions) {
            const char* kCases[] = {
                "x' = x^3\nx(0) = 1\n",
                "x' = x^5\nx(0) = 1\n",
                "x' = x * y * z\ny' = 0\nz' = 0\nx(0)=1\ny(0)=1\nz(0)=1\n",
            };
            for (const char* text : kCases) {
                SCOPED_TRACE(std::string("input: ") + text);
                RawSystem sys = PipelineText(text);
                auto aux = Quadratize(sys);
                for (const auto& kv : aux) {
                    EXPECT_TRUE(IsMulOfTwoFunctions(*kv.second))
                        << "aux " << kv.first << ": " << ToString(*kv.second);
                }
            }
        }

        TEST(PropertyQuadratize, P6_EquationCountFormula) {
            const char* kCases[] = {
                "x' = x^3\nx(0) = 1\n",
                "x' = x^5\nx(0) = 1\n",
                "x' = x^2 * y^2\ny' = 0\nx(0)=1\ny(0)=1\n",
                "x' = x^3 + y^3\ny' = 0\nx(0)=1\ny(0)=1\n",
            };
            for (const char* text : kCases) {
                SCOPED_TRACE(std::string("input: ") + text);
                RawSystem sys = PipelineText(text);
                const std::size_t n_before = sys.equations.size();
                auto aux = Quadratize(sys);
                EXPECT_EQ(sys.equations.size(), n_before + aux.size());
            }
        }

        TEST(PropertyQuadratize, P7_Determinism) {
            const char* kCases[] = {
                "x' = x^5\nx(0) = 1\n",
                "x' = x^3 * y + x^5\ny'=0\nx(0)=1\ny(0)=1\n",
                "x' = x^2 * y^2\ny'=0\nx(0)=1\ny(0)=1\n",
            };
            for (const char* text : kCases) {
                SCOPED_TRACE(std::string("input: ") + text);
                RawSystem sys1 = PipelineText(text);
                RawSystem sys2 = PipelineText(text);
                Quadratize(sys1);
                Quadratize(sys2);
                EXPECT_EQ(ToString(sys1), ToString(sys2));
            }
        }

        TEST(PropertyQuadratize, P8_AllowedNodesOnly) {
            RawSystem sys = PipelineText(
                "x' = x^5 + y^3\n"
                "y' = 0\n"
                "x(0)=1\ny(0)=1\n");
            Quadratize(sys);
            for (const auto& eq : sys.equations) {
                EXPECT_TRUE(ContainsOnlyAllowedNodes(*eq.rhs))
                    << "bad RHS: " << ToString(*eq.rhs);
            }
        }

        TEST(PropertyQuadratize, P9_IdempotencePreservesStructure) {
            RawSystem sys = PipelineText("x' = x^7\nx(0) = 1\n");
            Quadratize(sys);
            const auto funcs_once = sys.functions;
            const auto eqs_once = sys.equations.size();
            Quadratize(sys);
            EXPECT_EQ(sys.functions, funcs_once);
            EXPECT_EQ(sys.equations.size(), eqs_once);
        }


        // ========================================================================
        // 15. QuadratizeHighDegreeSum — диагностика для ОЗТ (задел под ТЗ №7)
        //
        // Набор фиксирует текущие возможности Quadratize на системах,
        // возникающих при ручной полиномиализации ограниченной задачи
        // трёх тел (ОЗТ / CR3BP) и задачи N тел:
        //   - высокие степени одной переменной (рекурсия),
        //   - произведения разных переменных,
        //   - суммы внутри произведения (главный проблемный случай).
        //
        // Все тесты с префиксом DISABLED_ в имени помечены как известные
        // падения: после фикса Quadratize (ТЗ №7) убрать префикс, чтобы
        // они ожили и стали регрессионной защитой.
        // ========================================================================

        // --- Работает сейчас: чистые высокие степени -------------------------

        TEST(QuadratizeHighDegreeSum, FifthPowerSingleVariable) {
            // x^5 — рекурсия без сумм.
            RawSystem sys = PipelineText("x' = x*x*x*x*x\nx(0) = 0.5\n");
            auto aux = Quadratize(sys);
            EXPECT_FALSE(aux.empty());
            EXPECT_TRUE(AllRhsQuadratic(sys));
        }

        TEST(QuadratizeHighDegreeSum, Degree5MixedThreeVariables) {
            // x^3 * y * z — степень 5, разные переменные, без сумм.
            RawSystem sys = PipelineText(
                "x' = x*x*x*y*z\n"
                "y' = 0\n"
                "z' = 0\n"
                "x(0) = 0.5\ny(0) = 1\nz(0) = 1\n");
            auto aux = Quadratize(sys);
            EXPECT_TRUE(AllRhsQuadratic(sys));
        }

        TEST(QuadratizeHighDegreeSum, OuterCubicInnerProductNoSum) {
            // u1^3 * x * vx — степень 5, разные переменные, БЕЗ суммы.
            // Ключевой положительный кейс: эта структура ОЗТ работает.
            RawSystem sys = PipelineText(
                "u1' = u1*u1*u1*x*vx\n"
                "w1' = 0\n"
                "x' = 0\n"
                "vx' = 0\n"
                "u1(0) = 1\nw1(0) = 1\nx(0) = 0.5\nvx(0) = 0.5\n");
            auto aux = Quadratize(sys);
            EXPECT_TRUE(AllRhsQuadratic(sys));
        }

        // --- Работает сейчас: сумма внутри произведения, но степень <= 3 ----

        TEST(QuadratizeHighDegreeSum, SumInsideProductLowDegree) {
            // x^2 * (y + z) — сумма внутри произведения, итоговая степень 3.
            // Работает.
            RawSystem sys = PipelineText(
                "x' = x*x*(y + z)\n"
                "y' = 0\n"
                "z' = 0\n"
                "x(0) = 1\ny(0) = 1\nz(0) = 1\n");
            auto aux = Quadratize(sys);
            EXPECT_TRUE(AllRhsQuadratic(sys));
        }

        // --- Падает сейчас: сумма внутри произведения, степень >= 5 ---------
        //
        // Симптом: Quadratize НЕ бросает исключение, а оставляет в RHS
        // моном степени > 2. Дальше TaylorSpec падает с
        //   "TaylorSpec: RHS contains monomial of degree > 2".
        // В тестах это проявляется как AllRhsQuadratic(sys) == false.

        TEST(QuadratizeHighDegreeSum, SumInsideProductHighDegree) {
            // Минимальная структура ОЗТ: u1^3 * (x*vx + y*vy).
            // Степень 5 после раскрытия скобок, сумма внутри.
            RawSystem sys = PipelineText(
                "u1' = u1*u1*u1*(x*vx + y*vy)\n"
                "w1' = 0\n"
                "x' = 0\n"
                "y' = 0\n"
                "vx' = 0\n"
                "vy' = 0\n"
                "u1(0) = 1\nw1(0) = 1\nx(0) = 0.5\ny(0) = 0.5\n"
                "vx(0) = 0.5\nvy(0) = 0.5\n");
            auto aux = Quadratize(sys);
            EXPECT_TRUE(AllRhsQuadratic(sys));
        }

        TEST(QuadratizeHighDegreeSum, SumInsideProductWithCoefficients) {
            // С коэффициентами и вычитанием внутри суммы.
            RawSystem sys = PipelineText(
                "u1' = 2*u1*u1*u1*(x*vx + y*vy) - 3*u1*u1*(x*vx - y*vy)\n"
                "w1' = 0\nx' = 0\ny' = 0\nvx' = 0\nvy' = 0\n"
                "u1(0) = 1\nw1(0) = 1\nx(0) = 0.5\ny(0) = 0.5\n"
                "vx(0) = 0.5\nvy(0) = 0.5\n");
            auto aux = Quadratize(sys);
            EXPECT_TRUE(AllRhsQuadratic(sys));
        }

        TEST(QuadratizeHighDegreeSum, ThreeTermsInSumInsideProduct) {
            // Три слагаемых внутри суммы.
            RawSystem sys = PipelineText(
                "u1' = u1*u1*u1*(x*vx + y*vy + z*vz)\n"
                "w1' = 0\nx' = 0\ny' = 0\nz' = 0\nvx' = 0\nvy' = 0\nvz' = 0\n"
                "u1(0) = 1\nw1(0) = 1\nx(0) = 0.5\ny(0) = 0.5\nz(0) = 0.5\n"
                "vx(0) = 0.5\nvy(0) = 0.5\nvz(0) = 0.5\n");
            auto aux = Quadratize(sys);
            EXPECT_TRUE(AllRhsQuadratic(sys));
        }

        TEST(QuadratizeHighDegreeSum, FullCR3BPStructure) {
            // Полная структура правых частей ОЗТ в форме, которую получает
            // Quadratize после ручной полиномиализации (u1 = 1/r1, w1 = u1^2).
            // Здесь только первое и второе уравнения — минимальный
            // воспроизводящий набор, без кинематических связей.
            RawSystem sys = PipelineText(
                "u1' = -u1*u1*u1*(x*vx + y*vy)\n"
                "w1' = 2*u1*u1*u1*u1*(x*vx + y*vy)\n"
                "x' = 0\ny' = 0\nvx' = 0\nvy' = 0\n"
                "u1(0) = 1\nw1(0) = 1\nx(0) = 0.5\ny(0) = 0.0\n"
                "vx(0) = 0.0\nvy(0) = 0.5\n");
            auto aux = Quadratize(sys);
            EXPECT_TRUE(AllRhsQuadratic(sys));
        }

        // --- Проба: неизвестно, работает ли -------------------------------

        TEST(QuadratizeHighDegreeSum, PowerOfSumDegree4) {
            // (y + z)^4 — раскрытие степени суммы. Пока не проверено,
            // ожидается падение по той же причине.
            RawSystem sys = PipelineText(
                "x' = (y + z)^4\n"
                "y' = 0\n"
                "z' = 0\n"
                "x(0) = 1\ny(0) = 1\nz(0) = 1\n");
            auto aux = Quadratize(sys);
            EXPECT_TRUE(AllRhsQuadratic(sys));
        }

        TEST(QuadratizeHighDegreeSum, ProductOfThreeSums) {
            // (a + b) * (c + d) * (e + f) — три суммы, степень 3 после
            // раскрытия. Проверяет, что проблема не только в степени >= 5.
            RawSystem sys = PipelineText(
                "x' = (a + b)*(c + d)*(e + f)\n"
                "a' = 0\nb' = 0\nc' = 0\nd' = 0\ne' = 0\nf' = 0\n"
                "x(0) = 1\na(0) = 1\nb(0) = 1\nc(0) = 1\n"
                "d(0) = 1\ne(0) = 1\nf(0) = 1\n");
            auto aux = Quadratize(sys);
            EXPECT_TRUE(AllRhsQuadratic(sys));
        }

            // ========================================================================
            // 15. QuadratizeHighDegreeSumBug — падающие тесты, документируют баг.
            //
            // Все пять тестов СЕЙЧАС ПАДАЮТ. Причина одна и та же:
            // Quadratize не раскрывает сумму внутри произведения, когда после
            // раскрытия получаются мономы степени > 3.
            //
            // Симптом: Quadratize молча пропускает уравнение, оставляя в RHS
            // моном степени > 2. Дальше TaylorSpec падает с сообщением
            // "RHS contains monomial of degree > 2". В тесте это видно как
            // AllRhsQuadratic(sys) == false.
            //
            // После фикса Quadratize (ТЗ №7) все пять должны стать зелёными.
            // НЕ УДАЛЯТЬ, НЕ ПОМЕЧАТЬ DISABLED_ — это маркер долга.
            // ========================================================================

            TEST(QuadratizeHighDegreeSumBug, MinimalReproducer) {
            // Минимальный воспроизводитель бага.
            // Точно совпадает с первым уравнением ОЗТ после ручной
            // полиномиализации через u1 = 1/r1.
            RawSystem sys = PipelineText(
                "u1' = u1*u1*u1*(x*vx + y*vy)\n"
                "w1' = 0\n"
                "x' = 0\n"
                "y' = 0\n"
                "vx' = 0\n"
                "vy' = 0\n"
                "u1(0) = 1\nw1(0) = 1\nx(0) = 0.5\ny(0) = 0.5\n"
                "vx(0) = 0.5\nvy(0) = 0.5\n");
            auto aux = Quadratize(sys);
            EXPECT_TRUE(AllRhsQuadratic(sys))
                << "RHS after Quadratize: "
                << ToString(*sys.equations[0].rhs);
        }

        TEST(QuadratizeHighDegreeSumBug, WithCoefficientsAndSubtraction) {
            // Усложнение: коэффициенты, вычитание, разные степени.
            RawSystem sys = PipelineText(
                "u1' = 2*u1*u1*u1*(x*vx + y*vy) - 3*u1*u1*(x*vx - y*vy)\n"
                "w1' = 0\n"
                "x' = 0\n"
                "y' = 0\n"
                "vx' = 0\n"
                "vy' = 0\n"
                "u1(0) = 1\nw1(0) = 1\nx(0) = 0.5\ny(0) = 0.5\n"
                "vx(0) = 0.5\nvy(0) = 0.5\n");
            auto aux = Quadratize(sys);
            EXPECT_TRUE(AllRhsQuadratic(sys))
                << "RHS after Quadratize: "
                << ToString(*sys.equations[0].rhs);
        }

        TEST(QuadratizeHighDegreeSumBug, ThreeTermsInsideSum) {
            // Три слагаемых внутри суммы.
            RawSystem sys = PipelineText(
                "u1' = u1*u1*u1*(x*vx + y*vy + z*vz)\n"
                "w1' = 0\n"
                "x' = 0\ny' = 0\nz' = 0\n"
                "vx' = 0\nvy' = 0\nvz' = 0\n"
                "u1(0) = 1\nw1(0) = 1\n"
                "x(0) = 0.5\ny(0) = 0.5\nz(0) = 0.5\n"
                "vx(0) = 0.5\nvy(0) = 0.5\nvz(0) = 0.5\n");
            auto aux = Quadratize(sys);
            EXPECT_TRUE(AllRhsQuadratic(sys))
                << "RHS after Quadratize: "
                << ToString(*sys.equations[0].rhs);
        }

        TEST(QuadratizeHighDegreeSumBug, TwoEquationsOfCR3BP) {
            // Первые два уравнения ОЗТ вместе: u1^3 и u1^4 под суммами.
            RawSystem sys = PipelineText(
                "u1' = -u1*u1*u1*(x*vx + y*vy)\n"
                "w1' = 2*u1*u1*u1*u1*(x*vx + y*vy)\n"
                "x' = 0\ny' = 0\nvx' = 0\nvy' = 0\n"
                "u1(0) = 1\nw1(0) = 1\n"
                "x(0) = 0.5\ny(0) = 0.0\nvx(0) = 0.0\nvy(0) = 0.5\n");
            auto aux = Quadratize(sys);
            EXPECT_TRUE(AllRhsQuadratic(sys));
        }

        TEST(QuadratizeHighDegreeSumBug, FullCR3BPAfterManualPolynomization) {
            // Полная система ОЗТ в форме, которую получает Quadratize
            // после ручной полиномиализации. Ровно тот ввод, который
            // пользователь подавал в CLI и получал
            // "TaylorSpec: RHS contains monomial of degree > 2".
            RawSystem sys = PipelineText(
                "u1' = -u1*u1*u1*(x*vx + y*vy)\n"
                "w1' = 2*u1*u1*u1*u1*(x*vx + y*vy)\n"
                "vx' = 2*vy + x - u1*w1*x\n"
                "vy' = -2*vx + y - u1*w1*y\n"
                "x'  = vx\n"
                "y'  = vy\n"
                "x(0)  = 0.5\n"
                "y(0)  = 0.0\n"
                "vx(0) = 0.0\n"
                "vy(0) = 0.5\n"
                "u1(0) = 1.0\n"
                "w1(0) = 1.0\n");
            auto aux = Quadratize(sys);
            EXPECT_TRUE(AllRhsQuadratic(sys));
        }

        // ========================================================================
        // 16. QuadratizeHighDegreeSumBugExtra — дополнительные тесты по ТЗ №7
        // ========================================================================

        TEST(QuadratizeHighDegreeSumBugExtra, PowerOfSumDegree4) {
            RawSystem sys = PipelineText(
                "x' = (y + z)^4\n"
                "y' = 0\n"
                "z' = 0\n"
                "x(0) = 1\ny(0) = 1\nz(0) = 1\n");
            auto aux = Quadratize(sys);
            EXPECT_TRUE(AllRhsQuadratic(sys));
        }

        TEST(QuadratizeHighDegreeSumBugExtra, ProductOfThreeSums) {
            RawSystem sys = PipelineText(
                "x' = (a + b)*(c + d)*(e + f)\n"
                "a' = 0\nb' = 0\nc' = 0\nd' = 0\ne' = 0\nf' = 0\n"
                "x(0) = 1\na(0) = 1\nb(0) = 1\nc(0) = 1\n"
                "d(0) = 1\ne(0) = 1\nf(0) = 1\n");
            auto aux = Quadratize(sys);
            EXPECT_TRUE(AllRhsQuadratic(sys));
        }

        TEST(QuadratizeHighDegreeSumBugExtra, FullCR3BPAllSixEquations) {
            RawSystem sys = PipelineText(
                "u1' = -u1*u1*u1*(x*vx + y*vy)\n"
                "w1' = 2*u1*u1*u1*u1*(x*vx + y*vy)\n"
                "vx' = 2*vy + x - u1*w1*x\n"
                "vy' = -2*vx + y - u1*w1*y\n"
                "x'  = vx\n"
                "y'  = vy\n"
                "x(0)  = 0.5\n"
                "y(0)  = 0.0\n"
                "vx(0) = 0.0\n"
                "vy(0) = 0.5\n"
                "u1(0) = 1.0\n"
                "w1(0) = 1.0\n");
            auto aux = Quadratize(sys);
            EXPECT_TRUE(AllRhsQuadratic(sys));
        }

        TEST(QuadratizeHighDegreeSumBugExtra, CachingAfterDistribution) {
            // u1³*(x*vx + y*vy) + u1³*(x*vx - y*vy)
            // После раскрытия: 2 * u1³ * x * vx.
            // Общий моном u1³ * x * vx должен быть закэширован и использован один раз.
            RawSystem sys = PipelineText(
                "u1' = u1*u1*u1*(x*vx + y*vy) + u1*u1*u1*(x*vx - y*vy)\n"
                "w1' = 0\nx' = 0\ny' = 0\nvx' = 0\nvy' = 0\n"
                "u1(0) = 1\nw1(0) = 1\nx(0) = 0.5\ny(0) = 0.5\n"
                "vx(0) = 0.5\nvy(0) = 0.5\n");
            auto aux = Quadratize(sys);
            EXPECT_TRUE(AllRhsQuadratic(sys));
            std::set<std::string> defs;
            for (const auto& kv : aux) {
                defs.insert(ToString(*kv.second));
            }
            EXPECT_EQ(defs.size(), aux.size()) << "Duplicate monomials in aux map";
        }

        TEST(PropertyQuadratize, P10_VarietyOfHighDegreeSystems) {
            const char* kCases[] = {
                "x' = x^3\nx(0) = 1\n",
                "x' = x^5\nx(0) = 1\n",
                "x' = x^2 + x^3\nx(0) = 1\n",
                "x' = x * y * z\ny' = 0\nz' = 0\nx(0)=1\ny(0)=1\nz(0)=1\n",
                "x' = x^2 * y^2\ny' = 0\nx(0)=1\ny(0)=1\n",
                "x' = x^4 + y^5\ny' = 0\nx(0)=1\ny(0)=1\n",
                "u1' = u1*u1*u1*(x*vx + y*vy)\nw1'=0\nx'=0\ny'=0\nvx'=0\nvy'=0\n"
                "u1(0)=1\nw1(0)=1\nx(0)=1\ny(0)=1\nvx(0)=1\nvy(0)=1\n",
                "u1' = 2*u1*u1*u1*(x*vx + y*vy) - 3*u1*u1*(x*vx - y*vy)\n"
                "w1'=0\nx'=0\ny'=0\nvx'=0\nvy'=0\n"
                "u1(0)=1\nw1(0)=1\nx(0)=1\ny(0)=1\nvx(0)=1\nvy(0)=1\n",
                "u1' = u1*u1*u1*(x*vx + y*vy + z*vz)\n"
                "w1'=0\nx'=0\ny'=0\nz'=0\nvx'=0\nvy'=0\nvz'=0\n"
                "u1(0)=1\nw1(0)=1\nx(0)=1\ny(0)=1\nz(0)=1\nvx(0)=1\nvy(0)=1\nvz(0)=1\n",
                "x' = (y + z)^4\ny'=0\nz'=0\nx(0)=1\ny(0)=1\nz(0)=1\n",
                "x' = (a + b)*(c + d)*(e + f)\na'=0\nb'=0\nc'=0\nd'=0\ne'=0\nf'=0\n"
                "x(0)=1\na(0)=1\nb(0)=1\nc(0)=1\nd(0)=1\ne(0)=1\nf(0)=1\n"
            };
            for (const char* text : kCases) {
                SCOPED_TRACE(std::string("input: ") + text);
                RawSystem sys = PipelineText(text);
                Quadratize(sys);
                EXPECT_TRUE(AllRhsQuadratic(sys));
            }
        }

    } // namespace
} // namespace diffuri