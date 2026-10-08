// ============================================================================
// tests/unit/test_autonomize.cpp
//
// Тесты модуля autonomize: ContainsFunction (косвенно), Autonomize,
// идемпотентность, постусловия, интеграция с Polynomize/Quadratize.
//
// Структура файла:
//   1. Хелперы
//   2. AutonomizeNoOp — no-op на автономных системах
//   3. AutonomizeAppearance — появление t в RHS
//   4. AutonomizeNonZeroT0 — ненулевой t0
//   5. AutonomizeIdempotence — повторный вызов — no-op
//   6. AutonomizePost — постусловия
//   7. AutonomizeErrors — ошибки входа
//   8. AutonomizeIntegration — стык с Polynomize / Quadratize
//   9. PropertyAutonomize — P1..P7
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

#include "autonomize/autonomize.h"
#include "input/input.h"
#include "input/parser.h"
#include "normalize/normalize.h"
#include "order_reducer/order_reducer.h"
#include "polynomization/polynomization.h"
#include "quadratize/quadratize.h"
#include "simplify/simplify.h"

namespace diffuri {
    namespace {

        // ====================================================================
        // 1. Хелперы
        // ====================================================================

        // Пайплайн до Autonomize включительно, но без самого Autonomize:
        // Parse -> Normalize -> OrderReducer. Именно на таком входе
        // Autonomize должен работать.
        RawSystem PipelineText(const std::string& text) {
            RawSystem sys = ParseSystem(text);
            NormalizeSystem(sys);
            OrderReducer(sys);
            return sys;
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

        // Глубокий обход дерева: есть ли узел Function{name}?
        bool ContainsFunctionNode(const Expr& e, const std::string& name) {
            return std::visit([&](const auto& n) -> bool {
                using T = std::decay_t<decltype(n)>;
                if constexpr (std::is_same_v<T, Function>) {
                    return n.name == name;
                }
                else if constexpr (std::is_same_v<T, Unary>) {
                    return ContainsFunctionNode(*n.operand, name);
                }
                else if constexpr (std::is_same_v<T, Binary>) {
                    return ContainsFunctionNode(*n.lhs, name)
                        || ContainsFunctionNode(*n.rhs, name);
                }
                else if constexpr (std::is_same_v<T, Call>) {
                    for (const auto& a : n.args) {
                        if (ContainsFunctionNode(*a, name)) return true;
                    }
                    return false;
                }
                else {
                    // Number, Constant, Derivative — Function внутри нет.
                    return false;
                }
                }, e.value);
        }

        // Есть ли Function{t} в RHS хотя бы одного уравнения системы.
        bool ContainsFunctionNode(const RawSystem& sys,
            const std::string& name) {
            for (const auto& eq : sys.equations) {
                if (ContainsFunctionNode(*eq.rhs, name)) return true;
            }
            return false;
        }

        bool AllLhsFirstOrderDerivatives(const RawSystem& sys) {
            for (const auto& eq : sys.equations) {
                auto* d = std::get_if<Derivative>(&eq.lhs->value);
                if (d == nullptr || d->order != 1) return false;
            }
            return true;
        }

        // Есть ли в системе уравнение Derivative{name, 1} = Number(1.0).
        bool HasTrivialEquation(const RawSystem& sys,
            const std::string& name) {
            for (const auto& eq : sys.equations) {
                auto* d = std::get_if<Derivative>(&eq.lhs->value);
                if (d == nullptr) continue;
                if (d->function_name != name || d->order != 1) continue;
                auto* num = std::get_if<Number>(&eq.rhs->value);
                if (num != nullptr && num->value == 1.0) return true;
            }
            return false;
        }

        // ====================================================================
        // 2. AutonomizeNoOp — автономные системы не меняются
        // ====================================================================

        TEST(AutonomizeNoOp, LinearAutonomous) {
            RawSystem sys = PipelineText("x' = -x\nx(0) = 1\n");
            const std::string before = ToString(sys);
            auto aux = Autonomize(sys);
            EXPECT_TRUE(aux.empty());
            EXPECT_EQ(ToString(sys), before);
        }

        TEST(AutonomizeNoOp, CubicAutonomous) {
            RawSystem sys = PipelineText("x' = x^3\nx(0) = 1\n");
            const std::string before = ToString(sys);
            auto aux = Autonomize(sys);
            EXPECT_TRUE(aux.empty());
            EXPECT_EQ(ToString(sys), before);
        }

        TEST(AutonomizeNoOp, TwoFunctionsAutonomous) {
            RawSystem sys = PipelineText(
                "x' = y\n"
                "y' = -x\n"
                "x(0) = 1\ny(0) = 0\n");
            const std::string before = ToString(sys);
            auto aux = Autonomize(sys);
            EXPECT_TRUE(aux.empty());
            EXPECT_EQ(ToString(sys), before);
            EXPECT_FALSE(HasFunction(sys, "t"));
        }

        TEST(AutonomizeNoOp, SinOfXAutonomous) {
            // sin(x) — не полином, но автономен: t не встречается.
            RawSystem sys = PipelineText("x' = sin(x)\nx(0) = 0\n");
            const std::string before = ToString(sys);
            auto aux = Autonomize(sys);
            EXPECT_TRUE(aux.empty());
            EXPECT_EQ(ToString(sys), before);
        }

        TEST(AutonomizeNoOp, NoFunctionTInRhs) {
            RawSystem sys = PipelineText("x' = x\ny' = y\nx(0)=1\ny(0)=1\n");
            EXPECT_FALSE(ContainsFunctionNode(sys, "t"));
            auto aux = Autonomize(sys);
            EXPECT_TRUE(aux.empty());
        }

        // ====================================================================
        // 3. AutonomizeAppearance — появление t в RHS
        // ====================================================================

        TEST(AutonomizeAppearance, LinearT) {
            RawSystem sys = PipelineText("x' = t\nx(0) = 1\n");
            auto aux = Autonomize(sys);
            ASSERT_EQ(aux.size(), 1u);
            ASSERT_TRUE(aux.count("t"));
            EXPECT_TRUE(HasFunction(sys, "t"));
            EXPECT_TRUE(HasOrderZeroIC(sys, "t"));
            EXPECT_TRUE(HasTrivialEquation(sys, "t"));
            EXPECT_TRUE(AllLhsFirstOrderDerivatives(sys));
        }

        TEST(AutonomizeAppearance, TMulX) {
            RawSystem sys = PipelineText("x' = t * x\nx(0) = 1\n");
            auto aux = Autonomize(sys);
            EXPECT_EQ(aux.size(), 1u);
            EXPECT_TRUE(HasFunction(sys, "t"));
            EXPECT_TRUE(HasTrivialEquation(sys, "t"));
        }

        TEST(AutonomizeAppearance, TMulXSquared) {
            RawSystem sys = PipelineText("x' = t * x^2\nx(0) = 1\n");
            auto aux = Autonomize(sys);
            EXPECT_EQ(aux.size(), 1u);
            EXPECT_TRUE(HasFunction(sys, "t"));
            EXPECT_TRUE(HasOrderZeroIC(sys, "t"));
            EXPECT_TRUE(HasTrivialEquation(sys, "t"));
        }

        TEST(AutonomizeAppearance, TCubed) {
            RawSystem sys = PipelineText("x' = t^3\nx(0) = 1\n");
            auto aux = Autonomize(sys);
            EXPECT_EQ(aux.size(), 1u);
            EXPECT_TRUE(HasFunction(sys, "t"));
            EXPECT_TRUE(HasTrivialEquation(sys, "t"));
        }

        TEST(AutonomizeAppearance, SinOfT) {
            RawSystem sys = PipelineText("x' = sin(t)\nx(0) = 1\n");
            auto aux = Autonomize(sys);
            EXPECT_EQ(aux.size(), 1u);
            EXPECT_TRUE(HasFunction(sys, "t"));
            EXPECT_TRUE(HasTrivialEquation(sys, "t"));
        }

        TEST(AutonomizeAppearance, TSquaredPlusX) {
            RawSystem sys = PipelineText("x' = t^2 + x\nx(0) = 1\n");
            auto aux = Autonomize(sys);
            EXPECT_EQ(aux.size(), 1u);
            EXPECT_TRUE(HasFunction(sys, "t"));
            EXPECT_TRUE(HasTrivialEquation(sys, "t"));
        }

        TEST(AutonomizeAppearance, TInOneOfTwoEquations) {
            RawSystem sys = PipelineText(
                "x' = t\n"
                "y' = 0\n"
                "x(0) = 1\ny(0) = 0\n");
            auto aux = Autonomize(sys);
            EXPECT_EQ(aux.size(), 1u);
            EXPECT_TRUE(HasFunction(sys, "t"));
            EXPECT_TRUE(HasTrivialEquation(sys, "t"));
        }

        // ====================================================================
        // 4. AutonomizeNonZeroT0 — ненулевой t0
        // ====================================================================

        TEST(AutonomizeNonZeroT0, TMulXAtTwo) {
            RawSystem sys = PipelineText("x' = t * x\nx(2) = 1\n");
            auto aux = Autonomize(sys);
            ASSERT_EQ(aux.size(), 1u);
            EXPECT_DOUBLE_EQ(ICValue(sys, "t", 0), 2.0);
            for (const auto& ic : sys.initial_conditions) {
                if (ic.function_name == "t") {
                    EXPECT_DOUBLE_EQ(ic.t0, 2.0);
                    EXPECT_DOUBLE_EQ(ic.value, 2.0);
                    EXPECT_EQ(ic.order, 0);
                }
            }
        }

        TEST(AutonomizeNonZeroT0, TAtHalf) {
            RawSystem sys = PipelineText("x' = t\nx(0.5) = 1\n");
            auto aux = Autonomize(sys);
            ASSERT_EQ(aux.size(), 1u);
            EXPECT_DOUBLE_EQ(ICValue(sys, "t", 0), 0.5);
            for (const auto& ic : sys.initial_conditions) {
                if (ic.function_name == "t") {
                    EXPECT_DOUBLE_EQ(ic.t0, 0.5);
                    EXPECT_DOUBLE_EQ(ic.value, 0.5);
                }
            }
        }

        TEST(AutonomizeNonZeroT0, AuxValueIsNumberT0) {
            RawSystem sys = PipelineText("x' = t\nx(3.25) = 1\n");
            auto aux = Autonomize(sys);
            ASSERT_TRUE(aux.count("t"));
            auto* num = std::get_if<Number>(&aux.at("t")->value);
            ASSERT_NE(num, nullptr);
            EXPECT_DOUBLE_EQ(num->value, 3.25);
        }

        // ====================================================================
        // 5. AutonomizeIdempotence — повторный вызов — no-op
        // ====================================================================

        TEST(AutonomizeIdempotence, SecondCallReturnsEmpty) {
            RawSystem sys = PipelineText("x' = t * x\nx(0) = 1\n");
            auto aux1 = Autonomize(sys);
            EXPECT_FALSE(aux1.empty());

            auto aux2 = Autonomize(sys);
            EXPECT_TRUE(aux2.empty());
        }

        TEST(AutonomizeIdempotence, SystemUnchangedAfterSecondCall) {
            RawSystem sys = PipelineText("x' = t * x^2\nx(0) = 1\n");
            Autonomize(sys);
            const std::string once = ToString(sys);
            Autonomize(sys);
            EXPECT_EQ(ToString(sys), once);
        }

        TEST(AutonomizeIdempotence, NoAdditionalTEquationAdded) {
            RawSystem sys = PipelineText("x' = t\nx(0) = 1\n");
            Autonomize(sys);
            const std::size_t eqs_once = sys.equations.size();
            Autonomize(sys);
            EXPECT_EQ(sys.equations.size(), eqs_once);
        }

        // ====================================================================
        // 6. AutonomizePost — постусловия
        // ====================================================================

        TEST(AutonomizePost, AllLhsAreFirstOrderDerivatives) {
            const char* kCases[] = {
                "x' = t\nx(0) = 1\n",
                "x' = t * x^2\nx(0) = 1\n",
                "x' = t\ny' = t\ny(0) = 0\nx(0) = 1\n",
            };
            for (const char* text : kCases) {
                SCOPED_TRACE(std::string("input: ") + text);
                RawSystem sys = PipelineText(text);
                Autonomize(sys);
                EXPECT_TRUE(AllLhsFirstOrderDerivatives(sys));
            }
        }

        TEST(AutonomizePost, TRegisteredInFunctions) {
            RawSystem sys = PipelineText("x' = t\nx(0) = 1\n");
            Autonomize(sys);
            EXPECT_TRUE(HasFunction(sys, "t"));
        }

        TEST(AutonomizePost, THasOrderZeroIC) {
            RawSystem sys = PipelineText("x' = t\nx(0) = 1\n");
            Autonomize(sys);
            EXPECT_TRUE(HasOrderZeroIC(sys, "t"));
        }

        TEST(AutonomizePost, THasTrivialEquation) {
            RawSystem sys = PipelineText("x' = t\nx(0) = 1\n");
            Autonomize(sys);
            EXPECT_TRUE(HasTrivialEquation(sys, "t"));
        }

        TEST(AutonomizePost, OriginalFunctionsOrderPreserved) {
            RawSystem sys = PipelineText(
                "x' = t * x\n"
                "y' = y\n"
                "x(0) = 1\ny(0) = 1\n");
            const std::vector<std::string> before = sys.functions;
            Autonomize(sys);
            ASSERT_GE(sys.functions.size(), before.size());
            for (std::size_t i = 0; i < before.size(); ++i) {
                EXPECT_EQ(sys.functions[i], before[i]);
            }
        }

        TEST(AutonomizePost, OriginalLhsUnchanged) {
            RawSystem sys = PipelineText(
                "x' = t * x\n"
                "y' = y\n"
                "x(0) = 1\ny(0) = 1\n");
            std::vector<std::string> before;
            for (const auto& eq : sys.equations) {
                before.push_back(ToString(*eq.lhs));
            }
            Autonomize(sys);
            ASSERT_GE(sys.equations.size(), before.size());
            for (std::size_t i = 0; i < before.size(); ++i) {
                EXPECT_EQ(ToString(*sys.equations[i].lhs), before[i]);
            }
        }

        TEST(AutonomizePost, AllICsHaveOrderZero) {
            RawSystem sys = PipelineText("x' = t\nx(0) = 1\n");
            Autonomize(sys);
            for (const auto& ic : sys.initial_conditions) {
                EXPECT_EQ(ic.order, 0);
            }
        }

        TEST(AutonomizePost, EquationAppendedAtEnd) {
            RawSystem sys = PipelineText("x' = t\nx(0) = 1\n");
            const std::size_t n_before = sys.equations.size();
            Autonomize(sys);
            ASSERT_EQ(sys.equations.size(), n_before + 1);
            auto* d = std::get_if<Derivative>(
                &sys.equations[n_before].lhs->value);
            ASSERT_NE(d, nullptr);
            EXPECT_EQ(d->function_name, "t");
            EXPECT_EQ(d->order, 1);
        }

        // ====================================================================
        // 7. AutonomizeErrors — ошибки входа
        // ====================================================================

        TEST(AutonomizeErrors, NotFirstOrderThrows) {
            // Система второго порядка без OrderReducer.
            RawSystem sys = ParseSystem("x'' = x\nx(0)=1\nx'(0)=0\n");
            NormalizeSystem(sys);
            // OrderReducer НЕ вызываем — Autonomize должен упасть.
            EXPECT_THROW(Autonomize(sys), AutonomizeError);
        }

        TEST(AutonomizeErrors, NoICsAndTInRhsThrows) {
            // Собираем RawSystem вручную: Parse через Validate не пропустит
            // систему без IC.
            RawSystem sys;
            sys.independent_variable = "t";
            sys.functions = { "x" };

            Equation eq;
            eq.lhs = MakeDerivative("x", 1);
            eq.rhs = MakeFunction("t");
            sys.equations.push_back(std::move(eq));
            // initial_conditions пуст.

            EXPECT_THROW(Autonomize(sys), AutonomizeError);
        }

        TEST(AutonomizeErrors, DifferentT0sAndTInRhsThrows) {
            RawSystem sys = PipelineText(
                "x' = t\n"
                "y' = 0\n"
                "x(0) = 1\ny(1) = 0\n");
            EXPECT_THROW(Autonomize(sys), AutonomizeError);
        }

        TEST(AutonomizeErrors, DifferentT0sButNoTIsNoOp) {
            // Контрпример: разные t0 в автономной системе не мешают.
            RawSystem sys = PipelineText(
                "x' = -x\n"
                "y' = 0\n"
                "x(0) = 1\ny(1) = 0\n");
            EXPECT_FALSE(ContainsFunctionNode(sys, "t"));
            auto aux = Autonomize(sys);
            EXPECT_TRUE(aux.empty());
        }

        // ====================================================================
        // 8. AutonomizeIntegration — стык с Polynomize / Quadratize
        // ====================================================================

        TEST(AutonomizeIntegration, TMulXSquaredThenPolynomizeThenQuadratize) {
            RawSystem sys = PipelineText("x' = t * x^2\nx(0) = 1\n");
            Autonomize(sys);
            Polynomize(sys);   // no-op: t * x^2 уже полиномиален
            Quadratize(sys);   // t*x^2 имеет степень 3 -> вводится q_i

            for (const auto& eq : sys.equations) {
                EXPECT_TRUE(IsQuadratic(*eq.rhs))
                    << "bad RHS: " << ToString(*eq.rhs);
            }
        }

        TEST(AutonomizeIntegration, TPowerFiveThenQuadratize) {
            RawSystem sys = PipelineText("x' = t^5\nx(0) = 1\n");
            Autonomize(sys);
            Polynomize(sys);
            Quadratize(sys);
            for (const auto& eq : sys.equations) {
                EXPECT_TRUE(IsQuadratic(*eq.rhs))
                    << "bad RHS: " << ToString(*eq.rhs);
            }
        }

        TEST(AutonomizeIntegration, LinearInTIsQuadraticNoOp) {
            // Реплика размороженного теста QuadratizeNonAutonomous.
            RawSystem sys = PipelineText("x' = t * x\nx(0) = 1\n");
            Autonomize(sys);
            Polynomize(sys);
            auto aux = Quadratize(sys);
            EXPECT_TRUE(aux.empty());
            for (const auto& eq : sys.equations) {
                EXPECT_TRUE(IsQuadratic(*eq.rhs));
            }
        }

        TEST(AutonomizeIntegration, QuadraticInTWithSquareOfX) {
            // Реплика размороженного теста QuadratizeNonAutonomous.
            RawSystem sys = PipelineText("x' = t * x^2\nx(0) = 1\n");
            Autonomize(sys);
            Polynomize(sys);
            auto aux = Quadratize(sys);
            EXPECT_FALSE(aux.empty());
            for (const auto& eq : sys.equations) {
                EXPECT_TRUE(IsQuadratic(*eq.rhs));
            }
        }

        TEST(AutonomizeIntegration, TTripleProduct) {
            // Реплика размороженного теста QuadratizeNonAutonomous.
            RawSystem sys = PipelineText(
                "x' = t * x * y\n"
                "y' = 0\n"
                "x(0) = 1\ny(0) = 1\n");
            Autonomize(sys);
            Polynomize(sys);
            Quadratize(sys);
            for (const auto& eq : sys.equations) {
                EXPECT_TRUE(IsQuadratic(*eq.rhs))
                    << "bad RHS: " << ToString(*eq.rhs);
            }
        }

        // ====================================================================
        // 9. PropertyAutonomize — P1..P7
        // ====================================================================

        TEST(PropertyAutonomize, P1_Idempotent) {
            RawSystem sys = PipelineText("x' = t * x\nx(0) = 1\n");
            Autonomize(sys);
            const std::string once = ToString(sys);

            auto aux2 = Autonomize(sys);
            EXPECT_TRUE(aux2.empty());
            EXPECT_EQ(ToString(sys), once);
        }

        TEST(PropertyAutonomize, P2_AllLhsAreFirstOrderDerivatives) {
            const char* kCases[] = {
                "x' = t\nx(0) = 1\n",
                "x' = t * x\nx(0) = 1\n",
                "x' = t * x^2\nx(0) = 1\n",
                "x' = sin(t)\nx(0) = 1\n",
                "x' = t\ny' = t\nx(0) = 1\ny(0) = 1\n",
            };
            for (const char* text : kCases) {
                SCOPED_TRACE(std::string("input: ") + text);
                RawSystem sys = PipelineText(text);
                Autonomize(sys);
                EXPECT_TRUE(AllLhsFirstOrderDerivatives(sys));
            }
        }

        TEST(PropertyAutonomize, P3_OriginalFunctionsPreserved) {
            const char* kCases[] = {
                "x' = t\nx(0) = 1\n",
                "x' = t * x\ny' = -x\nx(0)=1\ny(0)=0\n",
                "x' = t\ny' = t\nz' = t\nx(0)=1\ny(0)=1\nz(0)=1\n",
            };
            for (const char* text : kCases) {
                SCOPED_TRACE(std::string("input: ") + text);
                RawSystem sys = PipelineText(text);
                const std::vector<std::string> before = sys.functions;
                Autonomize(sys);
                for (std::size_t i = 0; i < before.size(); ++i) {
                    EXPECT_EQ(sys.functions[i], before[i]);
                }
            }
        }

        TEST(PropertyAutonomize, P4_TRegisteredWhenAuxNonEmpty) {
            const char* kCases[] = {
                "x' = t\nx(0) = 1\n",
                "x' = t * x\nx(2) = 1\n",
                "x' = sin(t)\nx(0.5) = 1\n",
            };
            for (const char* text : kCases) {
                SCOPED_TRACE(std::string("input: ") + text);
                RawSystem sys = PipelineText(text);
                auto aux = Autonomize(sys);
                if (!aux.empty()) {
                    EXPECT_TRUE(HasFunction(sys, "t"));
                    EXPECT_TRUE(HasOrderZeroIC(sys, "t"));
                }
            }
        }

        TEST(PropertyAutonomize, P5_TrivialEquationForT) {
            const char* kCases[] = {
                "x' = t\nx(0) = 1\n",
                "x' = t * x^2\nx(0) = 1\n",
                "x' = sin(t)\nx(0) = 1\n",
            };
            for (const char* text : kCases) {
                SCOPED_TRACE(std::string("input: ") + text);
                RawSystem sys = PipelineText(text);
                auto aux = Autonomize(sys);
                if (!aux.empty()) {
                    EXPECT_TRUE(HasTrivialEquation(sys, "t"));
                }
            }
        }

        TEST(PropertyAutonomize, P6_Determinism) {
            const char* kCases[] = {
                "x' = t\nx(0) = 1\n",
                "x' = t * x^2\nx(0) = 1\n",
                "x' = t\ny' = t\nx(0)=1\ny(0)=1\n",
            };
            for (const char* text : kCases) {
                SCOPED_TRACE(std::string("input: ") + text);
                RawSystem sys1 = PipelineText(text);
                RawSystem sys2 = PipelineText(text);
                Autonomize(sys1);
                Autonomize(sys2);
                EXPECT_EQ(ToString(sys1), ToString(sys2));
            }
        }

        TEST(PropertyAutonomize, P7_NoOpWhenTNotInRhs) {
            const char* kCases[] = {
                "x' = -x\nx(0) = 1\n",
                "x' = x^3\nx(0) = 1\n",
                "x' = y\ny' = -x\nx(0)=1\ny(0)=0\n",
                "x' = sin(x)\nx(0) = 0\n",
            };
            for (const char* text : kCases) {
                SCOPED_TRACE(std::string("input: ") + text);
                RawSystem sys = PipelineText(text);
                const std::string before = ToString(sys);
                auto aux = Autonomize(sys);
                EXPECT_TRUE(aux.empty());
                EXPECT_EQ(ToString(sys), before);
            }
        }

        // ========================================================================
// 10. Дополнительные тесты
//
// Кастомное имя независимой переменной, ложные срабатывания,
// глубокий обход AST, отрицательный t0, P8–P10, интеграция
// со вторым порядком.
// ========================================================================

        namespace {

            // Хелпер для систем с кастомной независимой переменной.
            RawSystem PipelineTextWithVar(const std::string& text,
                const std::string& var) {
                ParseOptions opts;
                opts.independent_variable = var;
                RawSystem sys = ParseSystem(text, opts);
                NormalizeSystem(sys);
                OrderReducer(sys);
                return sys;
            }

        } // namespace

        // ------------------------------------------------------------------------
        // 10.1. Кастомное имя независимой переменной
        // ------------------------------------------------------------------------

        TEST(AutonomizeCustomVariable, CustomNameS) {
            RawSystem sys = PipelineTextWithVar("x' = s * x\nx(0) = 1\n", "s");
            auto aux = Autonomize(sys);
            ASSERT_EQ(aux.size(), 1u);
            ASSERT_TRUE(aux.count("s"));      // ключ — именно "s"
            EXPECT_FALSE(aux.count("t"));     // литерал "t" не должен появиться
            EXPECT_TRUE(HasFunction(sys, "s"));
            EXPECT_TRUE(HasOrderZeroIC(sys, "s"));
            EXPECT_TRUE(HasTrivialEquation(sys, "s"));
        }

        TEST(AutonomizeCustomVariable, CustomNameTau) {
            RawSystem sys = PipelineTextWithVar("x' = tau * x\nx(0) = 1\n", "tau");
            auto aux = Autonomize(sys);
            ASSERT_EQ(aux.size(), 1u);
            ASSERT_TRUE(aux.count("tau"));
            EXPECT_FALSE(aux.count("t"));
            EXPECT_TRUE(HasFunction(sys, "tau"));
            EXPECT_TRUE(HasTrivialEquation(sys, "tau"));
        }

        TEST(AutonomizeCustomVariable, CustomNameTNotUsed) {
            // Переменная называется s, а t в системе вообще не встречается.
            RawSystem sys = PipelineTextWithVar("x' = -x\nx(0) = 1\n", "s");
            auto aux = Autonomize(sys);
            EXPECT_TRUE(aux.empty());
            EXPECT_FALSE(HasFunction(sys, "s"));
        }

        TEST(AutonomizeCustomVariable, AuxValueIsNumberT0CustomName) {
            RawSystem sys = PipelineTextWithVar("x' = s\nx(2) = 1\n", "s");
            auto aux = Autonomize(sys);
            ASSERT_TRUE(aux.count("s"));
            auto* num = std::get_if<Number>(&aux.at("s")->value);
            ASSERT_NE(num, nullptr);
            EXPECT_DOUBLE_EQ(num->value, 2.0);
        }

        // ------------------------------------------------------------------------
        // 10.2. Уже автономизированная система на входе
        // ------------------------------------------------------------------------

        TEST(AutonomizeAlreadyAutonomized, ManualSystemIsNoOpOrThrows) {
            // Пользователь сам написал автономизированную систему.
            // Возможны два допустимых поведения:
            //   (а) Validate отклонит — t конфликтует с independent_variable;
            //   (б) Validate пропустит, Autonomize вернёт пустую карту.
            // Тест документирует, что других вариантов быть не должно.
            try {
                RawSystem sys = ParseSystem(
                    "x' = t\n"
                    "t' = 1\n"
                    "x(0) = 1\nt(0) = 0\n");
                NormalizeSystem(sys);
                OrderReducer(sys);
                auto aux = Autonomize(sys);
                EXPECT_TRUE(aux.empty());
            }
            catch (const InputError&) {
                SUCCEED() << "Validate отклоняет такую систему — тоже корректно";
            }
        }

        // ------------------------------------------------------------------------
        // 10.3. Ложные срабатывания: t_1, tx, time — не t
        // ------------------------------------------------------------------------

        TEST(AutonomizeFalsePositive, SimilarNamesNotConfusedWithT) {
            // t_1, tx, time — не t. Autonomize должен вернуть пустую карту.
            RawSystem sys = PipelineText(
                "x' = t_1 * x + tx + time\n"
                "t_1' = 0\n"
                "tx' = 0\n"
                "time' = 0\n"
                "x(0) = 1\nt_1(0) = 1\ntx(0) = 1\ntime(0) = 1\n");
            auto aux = Autonomize(sys);
            EXPECT_TRUE(aux.empty());
            EXPECT_FALSE(HasFunction(sys, "t"));
        }

        TEST(AutonomizeFalsePositive, SimilarNameInsideCall) {
            // sin(t_1) — это не sin(t).
            RawSystem sys = PipelineText(
                "x' = sin(t_1)\n"
                "t_1' = 0\n"
                "x(0) = 1\nt_1(0) = 1\n");
            auto aux = Autonomize(sys);
            EXPECT_TRUE(aux.empty());
        }

        // ------------------------------------------------------------------------
        // 10.4. t в нетривиальных узлах AST
        // ------------------------------------------------------------------------

        TEST(AutonomizeDeepT, TInsideCallArgument) {
            // Покрывает обход Call::args, где внутри Add.
            RawSystem sys = PipelineText("x' = sin(t + 1)\nx(0) = 1\n");
            auto aux = Autonomize(sys);
            EXPECT_EQ(aux.size(), 1u);
            EXPECT_TRUE(HasFunction(sys, "t"));
            EXPECT_TRUE(HasTrivialEquation(sys, "t"));
        }

        TEST(AutonomizeDeepT, TInsideNestedCall) {
            // sin(cos(t)) — t на глубине 2 внутри Call.
            RawSystem sys = PipelineText("x' = sin(cos(t))\nx(0) = 1\n");
            auto aux = Autonomize(sys);
            EXPECT_EQ(aux.size(), 1u);
            EXPECT_TRUE(HasFunction(sys, "t"));
        }

        TEST(AutonomizeDeepT, TInsideAddThenMul) {
            // (t + 1) * x — Add внутри Mul.
            RawSystem sys = PipelineText("x' = (t + 1) * x\nx(0) = 1\n");
            auto aux = Autonomize(sys);
            EXPECT_EQ(aux.size(), 1u);
            EXPECT_TRUE(HasFunction(sys, "t"));
        }

        TEST(AutonomizeDeepT, TInsideSub) {
            RawSystem sys = PipelineText("x' = x - t\nx(0) = 1\n");
            auto aux = Autonomize(sys);
            EXPECT_EQ(aux.size(), 1u);
            EXPECT_TRUE(HasFunction(sys, "t"));
        }

        // ------------------------------------------------------------------------
        // 10.5. Отрицательный t0 и много функций с одним t0
        // ------------------------------------------------------------------------

        TEST(AutonomizeNegativeT0, NegativeT0) {
            RawSystem sys = PipelineText("x' = t\nx(-5) = 1\n");
            auto aux = Autonomize(sys);
            ASSERT_EQ(aux.size(), 1u);
            EXPECT_DOUBLE_EQ(ICValue(sys, "t", 0), -5.0);
            for (const auto& ic : sys.initial_conditions) {
                if (ic.function_name == "t") {
                    EXPECT_DOUBLE_EQ(ic.t0, -5.0);
                    EXPECT_DOUBLE_EQ(ic.value, -5.0);
                }
            }
        }

        TEST(AutonomizeNegativeT0, NegativeT0AuxValue) {
            RawSystem sys = PipelineText("x' = t * x\nx(-2.5) = 1\n");
            auto aux = Autonomize(sys);
            ASSERT_TRUE(aux.count("t"));
            auto* num = std::get_if<Number>(&aux.at("t")->value);
            ASSERT_NE(num, nullptr);
            EXPECT_DOUBLE_EQ(num->value, -2.5);
        }

        TEST(AutonomizeMultiFuncSameT0, TwoFunctionsSameT0) {
            // x(2)=1, y(2)=1 — t0 один, автономизация должна пройти.
            RawSystem sys = PipelineText(
                "x' = t * x\n"
                "y' = t * y\n"
                "x(2) = 1\ny(2) = 1\n");
            auto aux = Autonomize(sys);
            EXPECT_EQ(aux.size(), 1u);
            EXPECT_DOUBLE_EQ(ICValue(sys, "t", 0), 2.0);
        }

        // ------------------------------------------------------------------------
        // 10.6. PropertyAutonomize — P8..P10
        // ------------------------------------------------------------------------

        TEST(PropertyAutonomize, P8_AuxSizeAtMostOne) {
            // Autonomize вводит либо ноль, либо одну переменную (t).
            const char* kCases[] = {
                "x' = -x\nx(0) = 1\n",
                "x' = t\nx(0) = 1\n",
                "x' = t * x^2\nx(2) = 1\n",
                "x' = sin(t)\nx(0.5) = 1\n",
            };
            for (const char* text : kCases) {
                SCOPED_TRACE(std::string("input: ") + text);
                RawSystem sys = PipelineText(text);
                auto aux = Autonomize(sys);
                EXPECT_LE(aux.size(), 1u);
            }
        }

        TEST(PropertyAutonomize, P9_AuxKeyIsIndependentVariable) {
            // Ключ в aux-карте всегда совпадает с sys.independent_variable.
            const char* kCases[] = {
                "x' = t\nx(0) = 1\n",
                "x' = t * x\nx(0) = 1\n",
                "x' = sin(t)\nx(0) = 1\n",
            };
            for (const char* text : kCases) {
                SCOPED_TRACE(std::string("input: ") + text);
                RawSystem sys = PipelineText(text);
                auto aux = Autonomize(sys);
                for (const auto& kv : aux) {
                    EXPECT_EQ(kv.first, sys.independent_variable);
                }
            }
        }

        TEST(PropertyAutonomize, P10_TNotPresentBeforeAutonomize) {
            // До Autonomize независимая переменная не должна быть
            // в sys.functions.
            const char* kCases[] = {
                "x' = t\nx(0) = 1\n",
                "x' = t * x^2\nx(0) = 1\n",
            };
            for (const char* text : kCases) {
                SCOPED_TRACE(std::string("input: ") + text);
                RawSystem sys = PipelineText(text);
                EXPECT_FALSE(HasFunction(sys, sys.independent_variable));
            }
        }

        // ------------------------------------------------------------------------
        // 10.7. Интеграция: второй порядок с t
        // ------------------------------------------------------------------------

        TEST(AutonomizeIntegration, SecondOrderWithTAfterReduce) {
            // x'' = t * x  ->  OrderReducer даёт x' = x_1, x_1' = t * x.
            RawSystem sys = PipelineText("x'' = t * x\nx(0) = 1\nx'(0) = 0\n");
            auto aux = Autonomize(sys);
            EXPECT_EQ(aux.size(), 1u);
            EXPECT_TRUE(HasFunction(sys, "t"));
            EXPECT_TRUE(HasTrivialEquation(sys, "t"));
            EXPECT_TRUE(AllLhsFirstOrderDerivatives(sys));
        }

        TEST(AutonomizeIntegration, SecondOrderThenPolynomizeThenQuadratize) {
            RawSystem sys = PipelineText("x'' = t * x^2\nx(0) = 1\nx'(0) = 0\n");
            Autonomize(sys);
            Polynomize(sys);
            Quadratize(sys);
            for (const auto& eq : sys.equations) {
                EXPECT_TRUE(IsQuadratic(*eq.rhs))
                    << "bad RHS: " << ToString(*eq.rhs);
            }
        }

    } // namespace
} // namespace diffuri