// ============================================================================
// tests/unit/test_semantic_normalize.cpp
//
// Семантические тесты нормализации: проверяют, что NormalizeSystem
// СОХРАНЯЕТ множество решений уравнения, а не только его внешний вид.
//
// Идея. Пусть исходное уравнение L = R нормализовано в y^(n) = R'.
// Обозначим
//     Δ_orig(a) = eval(L, a) - eval(R, a)
//     Δ_norm(a) = eval(y^(n), a) - eval(R', a)
// где a — произвольная подстановка значений функций и их производных.
//
// По построению NormalizeSystem Δ_orig(a) = C * Δ_norm(a) для всех a,
// где C — ненулевая константа (коэффициент при старшей производной).
// Тест проверяет это на случайных подстановках: отношение Δ_orig/Δ_norm
// должно быть одним и тем же для всех a.
//
// Такой тест ловит ошибки в:
//   - знаке при переносе слагаемых через знак равенства;
//   - делении на коэффициент при старшей производной;
//   - «потере» слагаемых без старшей производной;
//   - путанице порядков при построении lhs = y^(n);
//   - несогласованном изменении lhs и rhs.
//
// Тесты:
//   S1. Детерминированные примеры (общий случай, отрицательный
//       коэффициент, одинаковые слагаемые с двух сторон).
//   S2. Случайно сгенерированные уравнения (свойственный тест).
//   S3. Система из нескольких уравнений.
// ============================================================================
#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <map>
#include <memory>
#include <random>
#include <set>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "input/expression.h"
#include "input/input.h"
#include "input/parser.h"
#include "normalize/normalize.h"
#include "simplify/simplify.h"

namespace diffuri {
    namespace {

        // ---------------------------------------------------------------------------
        // Клонирование (NormalizeSystem модифицирует уравнения in-place).
        // ---------------------------------------------------------------------------
        ExprPtr Clone(const Expr& e) {
            return std::visit([&](const auto& n) -> ExprPtr {
                using T = std::decay_t<decltype(n)>;
                if constexpr (std::is_same_v<T, Number>) {
                    return MakeNumber(n.value);
                }
                else if constexpr (std::is_same_v<T, Function>) {
                    return MakeFunction(n.name);
                }
                else if constexpr (std::is_same_v<T, Constant>) {
                    return MakeConstant(n.name, n.value);
                }
                else if constexpr (std::is_same_v<T, Derivative>) {
                    return MakeDerivative(n.function_name, n.order);
                }
                else if constexpr (std::is_same_v<T, Unary>) {
                    return MakeUnary(n.op, Clone(*n.operand));
                }
                else if constexpr (std::is_same_v<T, Binary>) {
                    return MakeBinary(n.op, Clone(*n.lhs), Clone(*n.rhs));
                }
                else if constexpr (std::is_same_v<T, Call>) {
                    std::vector<ExprPtr> args;
                    args.reserve(n.args.size());
                    for (const auto& a : n.args) args.push_back(Clone(*a));
                    return MakeCall(n.name, std::move(args));
                }
                return nullptr;
                }, e.value);
        }

        // ---------------------------------------------------------------------------
        // Множество имён переменных, встречающихся в выражении.
        // ---------------------------------------------------------------------------
        struct VariableSet {
            std::set<std::string>                  functions;
            std::set<std::pair<std::string, int>>  derivatives;
        };

        void CollectVars(const Expr& e, VariableSet& out) {
            std::visit([&](const auto& n) {
                using T = std::decay_t<decltype(n)>;
                if constexpr (std::is_same_v<T, Function>) {
                    out.functions.insert(n.name);
                }
                else if constexpr (std::is_same_v<T, Derivative>) {
                    out.derivatives.insert({ n.function_name, n.order });
                }
                else if constexpr (std::is_same_v<T, Unary>) {
                    CollectVars(*n.operand, out);
                }
                else if constexpr (std::is_same_v<T, Binary>) {
                    CollectVars(*n.lhs, out);
                    CollectVars(*n.rhs, out);
                }
                else if constexpr (std::is_same_v<T, Call>) {
                    for (const auto& a : n.args) CollectVars(*a, out);
                }
                }, e.value);
        }

        // ---------------------------------------------------------------------------
        // Ключ подстановки: "x", "x'", "x''".
        // ---------------------------------------------------------------------------
        std::string KeyOf(const std::string& func, int order) {
            std::string k = func;
            for (int i = 0; i < order; ++i) k += "'";
            return k;
        }

        using Assignment = std::map<std::string, double>;

        // ---------------------------------------------------------------------------
        // Вычислитель: значение выражения при заданной подстановке.
        // ---------------------------------------------------------------------------
        double Eval(const Expr& e, const Assignment& a) {
            return std::visit([&](const auto& n) -> double {
                using T = std::decay_t<decltype(n)>;
                if constexpr (std::is_same_v<T, Number>) {
                    return n.value;
                }
                else if constexpr (std::is_same_v<T, Function>) {
                    return a.at(n.name);
                }
                else if constexpr (std::is_same_v<T, Constant>) {
                    return n.value;
                }
                else if constexpr (std::is_same_v<T, Derivative>) {
                    return a.at(KeyOf(n.function_name, n.order));
                }
                else if constexpr (std::is_same_v<T, Unary>) {
                    double v = Eval(*n.operand, a);
                    if (n.op == Unary::Op::Neg) return -v;
                    throw std::runtime_error("Eval: unsupported unary op");
                }
                else if constexpr (std::is_same_v<T, Binary>) {
                    double l = Eval(*n.lhs, a);
                    double r = Eval(*n.rhs, a);
                    switch (n.op) {
                    case Binary::Op::Add: return l + r;
                    case Binary::Op::Sub: return l - r;
                    case Binary::Op::Mul: return l * r;
                    case Binary::Op::Div: return l / r;
                    case Binary::Op::Pow: return std::pow(l, r);
                    }
                    throw std::runtime_error("Eval: unsupported binary op");
                }
                else if constexpr (std::is_same_v<T, Call>) {
                    if (n.args.size() != 1) {
                        throw std::runtime_error("Eval: bad arity");
                    }
                    double v = Eval(*n.args[0], a);
                    if (n.name == "sin") return std::sin(v);
                    if (n.name == "cos") return std::cos(v);
                    if (n.name == "exp") return std::exp(v);
                    throw std::runtime_error("Eval: unknown call " + n.name);
                }
                return 0.0;
                }, e.value);
        }

        // ---------------------------------------------------------------------------
        // Ядро: проверяет семантическую эквивалентность системы до/после нормализации.
        // ---------------------------------------------------------------------------
        void ExpectSemanticEquivalence(RawSystem sys,
            int n_samples = 80,
            int seed = 0x5EED) {
            // Снимок исходных частей.
            std::vector<ExprPtr> orig_lhs, orig_rhs;
            orig_lhs.reserve(sys.equations.size());
            orig_rhs.reserve(sys.equations.size());
            for (const auto& eq : sys.equations) {
                orig_lhs.push_back(Clone(*eq.lhs));
                orig_rhs.push_back(Clone(*eq.rhs));
            }

            NormalizeSystem(sys);
            ASSERT_EQ(sys.equations.size(), orig_lhs.size());

            for (std::size_t i = 0; i < sys.equations.size(); ++i) {
                const Equation& norm = sys.equations[i];

                // Целевая функция и порядок — из нормализованного lhs.
                std::string target;
                int max_order = 0;
                std::visit([&](const auto& n) {
                    using T = std::decay_t<decltype(n)>;
                    if constexpr (std::is_same_v<T, Derivative>) {
                        target = n.function_name;
                        max_order = n.order;
                    }
                    }, norm.lhs->value);
                ASSERT_FALSE(target.empty())
                    << "eq " << i << ": normalized lhs is not a Derivative";
                ASSERT_NE(norm.rhs, nullptr);

                // Собираем все переменные.
                VariableSet vars;
                CollectVars(*orig_lhs[i], vars);
                CollectVars(*orig_rhs[i], vars);
                CollectVars(*norm.rhs, vars);
                vars.functions.insert("t");
                vars.derivatives.insert({ target, max_order });

                std::mt19937 rng(static_cast<std::uint32_t>(seed + i));
                std::uniform_real_distribution<double> vd(-2.0, 2.0);

                std::vector<double> ratios;
                ratios.reserve(n_samples);

                for (int s = 0; s < n_samples; ++s) {
                    Assignment a;
                    for (const auto& f : vars.functions) a[f] = vd(rng);
                    for (const auto& [f, o] : vars.derivatives) {
                        a[KeyOf(f, o)] = vd(rng);
                    }

                    double orig_delta =
                        Eval(*orig_lhs[i], a) - Eval(*orig_rhs[i], a);
                    double norm_delta =
                        a.at(KeyOf(target, max_order)) - Eval(*norm.rhs, a);

                    if (std::abs(norm_delta) < 1e-3) continue;
                    ratios.push_back(orig_delta / norm_delta);
                }

                ASSERT_GE(ratios.size(), 5u)
                    << "eq " << i << ": not enough non-degenerate samples";

                double ref = ratios.front();
                EXPECT_GT(std::abs(ref), 1e-9)
                    << "eq " << i << ": coefficient collapsed to zero";

                for (std::size_t k = 1; k < ratios.size(); ++k) {
                    double tol = std::abs(ref) * 1e-6 + 1e-9;
                    EXPECT_NEAR(ratios[k], ref, tol)
                        << "eq " << i << " sample " << k
                        << " ref=" << ref << " got=" << ratios[k];
                }
            }
        }

        // ---------------------------------------------------------------------------
        // S1. Детерминированные примеры.
        // ---------------------------------------------------------------------------
        TEST(SemanticNormalize, S1_FixedExamples) {
            const char* kCases[] = {
                "x' = -x\nx(0) = 1\n",
                "x'' + x = 0\nx(0) = 1\nx'(0) = 0\n",
                "x'' + 2 * x' + 3 * x = sin(t)\nx(0) = 1\nx'(0) = 0\n",
                "2 * x' = x\nx(0) = 1\n",
                "3 * x' - x' = 2 * x\nx(0) = 1\n",
                "-x'' = x\nx(0) = 0\nx'(0) = 1\n",
                "x'' + x' + x = t + 1\nx(0) = 0\nx'(0) = 0\n",
                "5 * x' + 2 * x = -3 * x\nx(0) = 1\n",
            };
            for (const char* text : kCases) {
                SCOPED_TRACE(std::string("input: ") + text);
                ExpectSemanticEquivalence(ParseSystem(text));
            }
        }

        // ---------------------------------------------------------------------------
        // Генератор «остатка» — выражения, не содержащего старшую производную
        // x^(order) и старшие. Разрешены: числа, t, x, x', ..., x^(order-1).
        // ---------------------------------------------------------------------------
        ExprPtr GenRest(int order, int depth, std::mt19937& rng) {
            std::uniform_int_distribution<int> d(0, 99);
            if (depth <= 0 || d(rng) < 35) {
                int roll = d(rng);
                if (roll < 30) {
                    std::uniform_int_distribution<int> dn(-3, 3);
                    return MakeNumber(static_cast<double>(dn(rng)));
                }
                if (roll < 65) return MakeFunction("t");
                if (roll < 85 || order < 2) return MakeFunction("x");
                std::uniform_int_distribution<int> do_(1, order - 1);
                return MakeDerivative("x", do_(rng));
            }
            static const Binary::Op ops[] = {
                Binary::Op::Add, Binary::Op::Sub, Binary::Op::Mul
            };
            std::uniform_int_distribution<int> dop(0, 2);
            return MakeBinary(ops[dop(rng)],
                GenRest(order, depth - 1, rng),
                GenRest(order, depth - 1, rng));
        }

        std::string GenEquationText(int order, std::mt19937& rng) {
            // Строим: c1 * x^(order) + rest_lhs = c2 * x^(order) + rest_rhs.
            std::uniform_int_distribution<int> dc(-3, 3);
            int c1 = dc(rng);
            if (c1 == 0) c1 = 1;
            int c2 = dc(rng);
            if (c2 == c1) c2 = c1 + 1;

            auto lhs_top = MakeBinary(Binary::Op::Mul,
                MakeNumber(static_cast<double>(c1)),
                MakeDerivative("x", order));
            auto lhs = MakeBinary(Binary::Op::Add,
                std::move(lhs_top), GenRest(order, 2, rng));

            auto rhs_top = MakeBinary(Binary::Op::Mul,
                MakeNumber(static_cast<double>(c2)),
                MakeDerivative("x", order));
            auto rhs = MakeBinary(Binary::Op::Add,
                std::move(rhs_top), GenRest(order, 2, rng));

            std::string text = ToString(*lhs) + " = " + ToString(*rhs) + "\n";
            text += "x(0) = 1\n";
            if (order >= 2) text += "x'(0) = 0\n";
            return text;
        }

        // ---------------------------------------------------------------------------
        // S2. Случайные уравнения.
        // ---------------------------------------------------------------------------
        TEST(SemanticNormalize, S2_RandomEquations) {
            std::mt19937 rng(0xC0DEu);
            for (int i = 0; i < 40; ++i) {
                int order = 1 + (i % 2);
                std::string text = GenEquationText(order, rng);
                SCOPED_TRACE("case " + std::to_string(i) + ": " + text);
                ExpectSemanticEquivalence(ParseSystem(text), 60, 0x1000 + i);
            }
        }

        // ---------------------------------------------------------------------------
        // S3. Система из нескольких уравнений.
        // ---------------------------------------------------------------------------
        TEST(SemanticNormalize, S3_TwoEquationSystem) {
            ExpectSemanticEquivalence(ParseSystem(
                "x' = y\n"
                "y' = -x\n"
                "x(0) = 1\n"
                "y(0) = 0\n"));

            ExpectSemanticEquivalence(ParseSystem(
                "x'' + x = y\n"
                "y' = -x\n"
                "x(0) = 1\n"
                "x'(0) = 0\n"
                "y(0) = 0\n"));
        }

        // ---------------------------------------------------------------------------
// S4. Неавтономное уравнение с exp(t).
//
// Обязательный сценарий из ТЗ: y' = t*y + exp(t).
// Проверяет: Eval умеет exp, RHS-зависимость от t не ломает
// нормализацию, коэффициент при y' равен 1.
// ---------------------------------------------------------------------------
        TEST(SemanticNormalize, S4_NonAutonomousExp) {
            ExpectSemanticEquivalence(ParseSystem(
                "y' = t * y + exp(t)\n"
                "y(0) = 1\n"));
        }

        // ---------------------------------------------------------------------------
        // S5. Уравнение с коэффициентом не 1 при старшей производной.
        //
        // Обязательный сценарий из ТЗ: 2*x'' - 4*x = 0.
        // Проверяет: корректное деление на C=2, эквивалентность
        // исходного и нормализованного уравнения с точностью до C.
        // ---------------------------------------------------------------------------
        TEST(SemanticNormalize, S5_NonUnitCoefficient) {
            ExpectSemanticEquivalence(ParseSystem(
                "2 * x'' - 4 * x = 0\n"
                "x(0) = 1\n"
                "x'(0) = 0\n"));
        }

        // ---------------------------------------------------------------------------
        // S6. Производные с обеих сторон, вырожденный случай.
        //
        // Обязательный сценарий из ТЗ: x' + x = x' - x + 2.
        // После переноса слагаемых коэффициент при x' сокращается
        // в ноль — уравнение перестаёт быть дифференциальным.
        // Ожидаем NormalizeError.
        // ---------------------------------------------------------------------------
        TEST(SemanticNormalize, S6_DerivativeCancelsBothSides) {
            RawSystem sys = ParseSystem(
                "x' + x = x' - x + 2\n"
                "x(0) = 1\n");
            EXPECT_THROW(NormalizeSystem(sys), NormalizeError);
        }

        // ---------------------------------------------------------------------------
        // S6b. Производные с обеих сторон, невырожденный случай.
        //
        // Тот же паттерн, что в S6, но коэффициент при старшей
        // производной после переноса не ноль. Проверяет, что
        // перенос слагаемых с обеих сторон корректен.
        // ---------------------------------------------------------------------------
        TEST(SemanticNormalize, S6b_DerivativeBothSidesNonDegenerate) {
            ExpectSemanticEquivalence(ParseSystem(
                "2 * x' + x = x' - x + 2\n"
                "x(0) = 1\n"));
        }

        // ---------------------------------------------------------------------------
        // S7. NormalizeSystem идемпотентна.
        //
        // Повторный прогон уже нормализованной системы не меняет
        // деревья ни в lhs, ни в rhs. Это контракт, на который
        // опираются последующие этапы (полиномизация, solver).
        // ---------------------------------------------------------------------------
        TEST(SemanticNormalize, S7_NormalizeIdempotent) {
            RawSystem sys = ParseSystem(
                "x'' + 3 * x' - 2 * x = sin(t)\n"
                "x(0) = 1\n"
                "x'(0) = 0\n");

            NormalizeSystem(sys);

            std::vector<std::string> lhs_before, rhs_before;
            lhs_before.reserve(sys.equations.size());
            rhs_before.reserve(sys.equations.size());
            for (const auto& eq : sys.equations) {
                lhs_before.push_back(ToString(*eq.lhs));
                rhs_before.push_back(ToString(*eq.rhs));
            }

            NormalizeSystem(sys);

            ASSERT_EQ(sys.equations.size(), lhs_before.size());
            for (std::size_t i = 0; i < sys.equations.size(); ++i) {
                EXPECT_EQ(ToString(*sys.equations[i].lhs), lhs_before[i])
                    << "eq " << i << " lhs changed on second pass";
                EXPECT_EQ(ToString(*sys.equations[i].rhs), rhs_before[i]) 
                    << "eq " << i << " rhs changed on second pass";
            }
        }
    } // namespace
} // namespace diffuri