// ============================================================================
// tests/unit/test_taylor_spec.cpp
//
// Тесты модуля taylor_spec (ТЗ №4.1, §6.2).
// ============================================================================
#include <gtest/gtest.h>

#include <string>

#include "input/input.h"
#include "solver/solver.h"
#include "solver/taylor_spec.h"

namespace diffuri {
    namespace {

        TaylorSpec MakeSpec(const std::string& text) {
            auto sys = ParseSystem(text);
            return BuildTaylorSpec(sys);
        }

        // --- §6.2: x' = -x -----------------------------------------------------------

        TEST(TaylorSpec, LinearDecay) {
            auto spec = MakeSpec("x' = -x\nx(0) = 1\n");
            EXPECT_EQ(spec.n, 1u);
            EXPECT_EQ(spec.u, 1u);                    // только x; нелинейных нет
            EXPECT_EQ(spec.monomial_keys.size(), 2u); // {1, x}
            EXPECT_EQ(spec.a.size(), 1u);             // 0-based: одна функция x
        }

        // --- §6.2: x' = x^2 ----------------------------------------------------------

        TEST(TaylorSpec, QuadraticSimple) {
            auto spec = MakeSpec("x' = x^2\nx(0) = 1\n");
            EXPECT_EQ(spec.n, 1u);
            EXPECT_EQ(spec.u, 2u);   // {1, x, x^2}
            // x_2 = x_1 * x_1
            ASSERT_GT(spec.scheme.size(), 2u);
            EXPECT_EQ(spec.scheme[2].first, spec.scheme[2].second);
            EXPECT_EQ(spec.scheme[2].first, 1u);
        }

        // --- §6.2: x' = y, y' = -x ---------------------------------------------------

        TEST(TaylorSpec, TwoLinearCoupled) {
            auto spec = MakeSpec("x' = y\ny' = -x\nx(0) = 1\ny(0) = 0\n");
            EXPECT_EQ(spec.n, 2u);
            EXPECT_EQ(spec.u, 2u);   // только линейные; нелинейных нет
            EXPECT_EQ(spec.monomial_keys.size(), 3u); // {1, x, y}
        }

        // --- §6.2: x' = y, y' = x^2 --------------------------------------------------

        TEST(TaylorSpec, MixedQuadratic) {
            auto spec = MakeSpec("x' = y\ny' = x^2\nx(0) = 0\ny(0) = 1\n");
            EXPECT_EQ(spec.n, 2u);
            EXPECT_EQ(spec.u, 3u);   // {1, x, y, x^2}
            // Единственный нелинейный моном — x^2 = x_? * x_? (квадрат)
            ASSERT_GT(spec.scheme.size(), 3u);
            EXPECT_EQ(spec.scheme[3].first, spec.scheme[3].second);
        }

        // --- §6.2: уникализация x*y --------------------------------------------------

        TEST(TaylorSpec, UniquenessOfProduct) {
            auto spec = MakeSpec("x' = x*y\ny' = -x*y\nx(0) = 1\ny(0) = 1\n");
            EXPECT_EQ(spec.n, 2u);
            EXPECT_EQ(spec.u, 3u);   // {1, x, y, x*y} — один моном, не два
            EXPECT_EQ(spec.monomial_keys.size(), 4u);
        }

        // --- §6.2: не-квадратичная система → SolverError ----------------------------

        TEST(TaylorSpec, NonQuadraticThrows) {
            auto sys = ParseSystem("x' = x^3\nx(0) = 1\n");
            EXPECT_THROW(BuildTaylorSpec(sys), SolverError);
        }

        // --- Общая проверка согласованности поля a -----------------------------------

        TEST(TaylorSpec, CoefficientMapConsistent) {
            auto spec = MakeSpec("x' = x^2 + x + 1\nx(0) = 1\n");
            // a[0] — коэффициенты уравнения dx/dt для x.
            // Три монома: 1, x, x^2.
            ASSERT_EQ(spec.a.size(), 1u);
            EXPECT_EQ(spec.a[0].size(), 3u);
        }

        // --- Коэффициенты мономов: 2*x*y --------------------------------------------

        TEST(TaylorSpec, ProductWithCoefficient) {
            auto spec = MakeSpec("x' = 2*x*y\ny' = 0\nx(0)=1\ny(0)=1\n");
            // x' содержит один моном x*y с коэффициентом 2.
            // Найти индекс монома x*y в monomial_keys.
            std::size_t idx = 0;
            for (std::size_t k = 0; k < spec.monomial_keys.size(); ++k) {
                if (spec.monomial_keys[k] == "x*y") idx = k;
            }
            ASSERT_NE(idx, 0u);
            ASSERT_EQ(spec.a.size(), 2u);
            ASSERT_NE(spec.a[0].find(idx), spec.a[0].end());
            EXPECT_DOUBLE_EQ(spec.a[0].at(idx), 2.0);
        }

        // --- Свёртка подобных слагаемых: x + x → один моном -------------------------

        TEST(TaylorSpec, LikeTermsAreCombined) {
            auto spec = MakeSpec("x' = x + x\nx(0) = 1\n");
            ASSERT_EQ(spec.a.size(), 1u);
            // Один линейный моном x с коэффициентом 2; плюс нет константы.
            std::size_t idx_x = 0;
            for (std::size_t k = 0; k < spec.monomial_keys.size(); ++k) {
                if (spec.monomial_keys[k] == "x") idx_x = k;
            }
            ASSERT_NE(idx_x, 0u);
            ASSERT_EQ(spec.a[0].size(), 1u);
            EXPECT_DOUBLE_EQ(spec.a[0].at(idx_x), 2.0);
        }

        // --- Ошибка: Call в RHS -----------------------------------------------------

        TEST(TaylorSpec, CallInRhsThrows) {
            auto sys = ParseSystem("x' = sin(x)\nx(0) = 1\n");
            EXPECT_THROW(BuildTaylorSpec(sys), SolverError);
        }

        // --- Ошибка: Div в RHS ------------------------------------------------------

        TEST(TaylorSpec, DivInRhsThrows) {
            auto sys = ParseSystem("x' = 1 / x\nx(0) = 1\n");
            EXPECT_THROW(BuildTaylorSpec(sys), SolverError);
        }

        // --- Ошибка: Pow с нецелым показателем --------------------------------------

        TEST(TaylorSpec, FractionalPowThrows) {
            auto sys = ParseSystem("x' = x^0.5\nx(0) = 1\n");
            EXPECT_THROW(BuildTaylorSpec(sys), SolverError);
        }

        // --- Ошибка: lhs не Derivative ----------------------------------------------

        TEST(TaylorSpec, NonDerivativeLhsThrows) {
            // Система формально синтаксически валидна, но lhs — не производная.
            RawSystem sys;
            sys.functions = { "x" };
            Equation eq;
            eq.lhs = MakeFunction("x");        // вместо MakeDerivative("x", 1)
            eq.rhs = MakeNumber(0.0);
            sys.equations.push_back(std::move(eq));
            sys.initial_conditions.push_back({ "x", 0, 0.0, 1.0 });
            EXPECT_THROW(BuildTaylorSpec(sys), SolverError);
        }

        // --- Ошибка: Derivative с order != 1 ----------------------------------------

        TEST(TaylorSpec, HigherOrderLhsThrows) {
            auto sys = ParseSystem("x'' = 0\nx(0)=1\nx'(0)=0\n");
            EXPECT_THROW(BuildTaylorSpec(sys), SolverError);
        }

    }  // namespace
}  // namespace diffuri