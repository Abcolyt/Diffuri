// ============================================================================
// tests/unit/test_taylor_table.cpp
//
// Тесты модуля taylor_table (ТЗ №4.1, §6.3).
// ============================================================================
#include <gtest/gtest.h>

#include <cmath>
#include <string>
#include <vector>

#include "input/input.h"
#include "solver/solver.h"
#include "solver/taylor_spec.h"
#include "solver/taylor_table.h"

namespace diffuri {
    namespace {

        TaylorSpec MakeSpec(const std::string& text) {
            auto sys = ParseSystem(text);
            return BuildTaylorSpec(sys);
        }

        // --- §6.3: x' = -x, x(0)=1 → коэффициенты e^{-t} ----------------------------

        TEST(TaylorTable, ExponentialCoefficients) {
            auto spec = MakeSpec("x' = -x\nx(0) = 1\n");
            TaylorTable table(spec, { 1.0 }, 6);

            EXPECT_NEAR(table.Coeff(1, 0), 1.0, 1e-14);
            EXPECT_NEAR(table.Coeff(1, 1), -1.0, 1e-14);
            EXPECT_NEAR(table.Coeff(1, 2), 1.0 / 2.0, 1e-14);
            EXPECT_NEAR(table.Coeff(1, 3), -1.0 / 6.0, 1e-14);
            EXPECT_NEAR(table.Coeff(1, 4), 1.0 / 24.0, 1e-14);
            EXPECT_NEAR(table.Coeff(1, 5), -1.0 / 120.0, 1e-14);
        }

        // --- §6.3: x' = x^2, x(0)=1 → коэффициенты 1/(1-t) --------------------------

        TEST(TaylorTable, GeometricCoefficients) {
            auto spec = MakeSpec("x' = x^2\nx(0) = 1\n");
            TaylorTable table(spec, { 1.0 }, 6);

            for (std::size_t p = 0; p <= 6; ++p) {
                EXPECT_NEAR(table.Coeff(1, p), 1.0, 1e-14) << "p = " << p;
            }
        }

        // --- §6.3: Evaluate(M=3, h=0.1) для x' = -x ---------------------------------

        TEST(TaylorTable, EvaluateCubic) {
            auto spec = MakeSpec("x' = -x\nx(0) = 1\n");
            TaylorTable table(spec, { 1.0 }, 6);

            // 1 - 0.1 + 0.005 - 1/6000
            const double expected = 1.0 - 0.1 + 0.005 - 1.0 / 6000.0;
            auto y = table.Evaluate(0.1, 3);
            ASSERT_EQ(y.size(), 1u);
            EXPECT_NEAR(y[0], expected, 1e-14);
        }

        // --- §6.3: DiffPoly(M=3, K=1) → h^4 / 4! ------------------------------------

        TEST(TaylorTable, DiffPolyOrder4) {
            auto spec = MakeSpec("x' = -x\nx(0) = 1\n");
            TaylorTable table(spec, { 1.0 }, 6);

            const double h = 0.1;
            const double expected = std::pow(h, 4) / 24.0;
            auto d = table.DiffPoly(h, 3, 1);
            ASSERT_EQ(d.size(), 1u);
            EXPECT_NEAR(d[0], expected, 1e-18);
        }

        // --- Металлическая проверка MonomialCount/MaxOrder ---------------------------

        TEST(TaylorTable, MetadataAccessors) {
            auto spec = MakeSpec("x' = x^2\nx(0) = 1\n");
            TaylorTable table(spec, { 1.0 }, 8);

            EXPECT_EQ(table.MaxOrder(), 8u);
            EXPECT_EQ(table.MonomialCount(), 3u);  // u+1 = 3
        }

        // --- Вычисление на многомерной системе --------------------------------------

        TEST(TaylorTable, HarmonicOscillatorFirstCoeffs) {
            // x' = y, y' = -x; x(0)=1, y(0)=0 → x = cos(t), y = -sin(t)
            auto spec = MakeSpec("x' = y\ny' = -x\nx(0) = 1\ny(0) = 0\n");
            TaylorTable table(spec, { 1.0, 0.0 }, 6);

            // Ищем индексы x и y в spec.function_names
            std::size_t kx = 0, ky = 0;
            for (std::size_t i = 0; i < spec.function_names.size(); ++i) {
                if (spec.function_names[i] == "x") kx = i + 1;
                if (spec.function_names[i] == "y") ky = i + 1;
            }
            ASSERT_NE(kx, 0u);
            ASSERT_NE(ky, 0u);

            // cos(t): 1, 0, -1/2, 0, 1/24, ...
            EXPECT_NEAR(table.Coeff(kx, 0), 1.0, 1e-14);
            EXPECT_NEAR(table.Coeff(kx, 1), 0.0, 1e-14);
            EXPECT_NEAR(table.Coeff(kx, 2), -0.5, 1e-14);
            EXPECT_NEAR(table.Coeff(kx, 3), 0.0, 1e-14);

            // y = -sin(t): 0, -1, 0, 1/6, ...
            EXPECT_NEAR(table.Coeff(ky, 0), 0.0, 1e-14);
            EXPECT_NEAR(table.Coeff(ky, 1), -1.0, 1e-14);
            EXPECT_NEAR(table.Coeff(ky, 2), 0.0, 1e-14);
            EXPECT_NEAR(table.Coeff(ky, 3), 1.0 / 6.0, 1e-14);
        }

        // --- Нелинейная многомерная система: x' = x*y, y' = -x ----------------------
    //
    // Проверяем, что моном x*y вычисляется по схеме корректно.
        TEST(TaylorTable, NonlinearCrossTerm) {
            auto spec = MakeSpec("x' = x*y\ny' = -x\nx(0)=1\ny(0)=1\n");
            TaylorTable table(spec, { 1.0, 1.0 }, 3);

            // Ряд для x(t) при x0=y0=1: коэффициенты от x*y = x_1*x_2.
            // x_{k,0} = 1, x_{k,1} = 1 (RHS = x*y = 1*1),
            // x_{k,2} = производная x*y = x'*y + x*y' = 1*1 + 1*(-1) = 0 → /2 = 0.
            // Точный ряд: x(t) = 1 + t + 0·t² + ...
            std::size_t idx_x = 0, idx_y = 0;
            for (std::size_t i = 0; i < spec.function_names.size(); ++i) {
                if (spec.function_names[i] == "x") idx_x = i + 1;
                if (spec.function_names[i] == "y") idx_y = i + 1;
            }
            EXPECT_NEAR(table.Coeff(idx_x, 0), 1.0, 1e-14);
            EXPECT_NEAR(table.Coeff(idx_x, 1), 1.0, 1e-14);
            EXPECT_NEAR(table.Coeff(idx_x, 2), 0.0, 1e-14);
            EXPECT_NEAR(table.Coeff(idx_y, 0), 1.0, 1e-14);
            EXPECT_NEAR(table.Coeff(idx_y, 1), -1.0, 1e-14);
        }

        // --- Coeff вне диапазона → std::out_of_range --------------------------------

        TEST(TaylorTable, CoeffOutOfRangeThrows) {
            auto spec = MakeSpec("x' = -x\nx(0) = 1\n");
            TaylorTable table(spec, { 1.0 }, 4);
            EXPECT_THROW(table.Coeff(99, 0), std::out_of_range);
            EXPECT_THROW(table.Coeff(1, 99), std::out_of_range);
        }

        // --- Evaluate / DiffPoly на границе диапазона -------------------------------

        TEST(TaylorTable, EvaluateAtMaxOrderBoundary) {
            auto spec = MakeSpec("x' = -x\nx(0) = 1\n");
            TaylorTable table(spec, { 1.0 }, 5);
            // M = MaxOrder() — не должно бросать.
            EXPECT_NO_THROW(table.Evaluate(0.1, 5));
            // M > MaxOrder() — бросает.
            EXPECT_THROW(table.Evaluate(0.1, 6), std::out_of_range);
        }

        TEST(TaylorTable, DiffPolyAtMaxOrderBoundary) {
            auto spec = MakeSpec("x' = -x\nx(0) = 1\n");
            TaylorTable table(spec, { 1.0 }, 5);
            // M + K == MaxOrder() — на границе.
            EXPECT_NO_THROW(table.DiffPoly(0.1, 3, 2));
            // M + K > MaxOrder() — бросает.
            EXPECT_THROW(table.DiffPoly(0.1, 3, 3), std::out_of_range);
        }

        // --- x0.size() != spec.n → SolverError --------------------------------------

        TEST(TaylorTable, ConstructorRejectsMismatchedX0) {
            auto spec = MakeSpec("x' = -x\nx(0) = 1\n");   // n = 1
            EXPECT_THROW(TaylorTable(spec, { 1.0, 2.0 }, 4), SolverError);
        }


    }  // namespace
}  // namespace diffuri