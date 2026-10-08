// ============================================================================
// tests/unit/test_solver.cpp
//
// Тесты модуля solver (ТЗ №4.1, §6.6).
// Заменяет Solver.Placeholder.
// ============================================================================
#include <gtest/gtest.h>

#include <cmath>
#include <string>
#include <vector>

#include "autonomize/autonomize.h"
#include "input/input.h"
#include "normalize/normalize.h"
#include "order_reducer/order_reducer.h"
#include "polynomization/polynomization.h"
#include "quadratize/quadratize.h"
#include "solver/solver.h"

namespace diffuri {
    namespace {

        constexpr double kPi = 3.14159265358979323846;

        // Вспомогательный поиск индекса функции в Solution::functions.
        std::size_t IndexOf(const std::vector<std::string>& names, const std::string& target) {
            for (std::size_t i = 0; i < names.size(); ++i) {
                if (names[i] == target) return i;
            }
            return names.size();  // маркер "не найдено"
        }

    }  // namespace

    // ============================================================================
    // End-to-end (прямой вызов Solve на уже канонических системах)
    // ============================================================================

    TEST(Solver, ExponentialDecay) {
        auto sys = ParseSystem("x' = -x\nx(0) = 1\n");
        SolveOptions opts;
        opts.t_end = 1.0;
        opts.M = 20;
        opts.h_init = 1e-4;

        auto sol = Solve(sys, opts);
        ASSERT_FALSE(sol.points.empty());
        EXPECT_NEAR(sol.points.back().x[0], std::exp(-1.0), 1e-10);
    }

    TEST(Solver, QuadraticBlowup) {
        auto sys = ParseSystem("x' = x^2\nx(0) = 1\n");
        SolveOptions opts;
        opts.t_end = 0.9;
        opts.M = 20;
        opts.h_init = 1e-4;

        auto sol = Solve(sys, opts);
        ASSERT_FALSE(sol.points.empty());
        // x(t) = 1/(1-t), x(0.9) = 10
        EXPECT_NEAR(sol.points.back().x[0], 10.0, 1e-8);
    }

    TEST(Solver, HarmonicOscillator) {
        auto sys = ParseSystem("x' = y\ny' = -x\nx(0) = 1\ny(0) = 0\n");
        SolveOptions opts;
        opts.t_end = 2.0 * kPi;
        opts.M = 20;
        opts.h_init = 1e-4;

        auto sol = Solve(sys, opts);
        ASSERT_FALSE(sol.points.empty());
        EXPECT_NEAR(sol.points.back().x[0], 1.0, 1e-8);
        EXPECT_NEAR(sol.points.back().x[1], 0.0, 1e-8);
    }

    // ============================================================================
    // Постусловия
    // ============================================================================

    TEST(Solver, Postconditions) {
        auto sys = ParseSystem("x' = -x\nx(0) = 1\n");
        SolveOptions opts;
        opts.t_end = 1.0;
        opts.M = 10;
        opts.h_init = 1e-3;

        auto sol = Solve(sys, opts);
        ASSERT_FALSE(sol.points.empty());
        EXPECT_DOUBLE_EQ(sol.points.front().t, 0.0);
        EXPECT_NEAR(sol.points.front().x[0], 1.0, 1e-14);
        EXPECT_NEAR(sol.t_final, opts.t_end, opts.h_min);
        EXPECT_NEAR(sol.points.back().t, opts.t_end, opts.h_min);
        EXPECT_GE(sol.steps, 1u);
        EXPECT_EQ(sol.order_used, opts.M);
    }

    // ============================================================================
    // Property-based
    // ============================================================================

    TEST(Solver, Determinism) {
        auto sys = ParseSystem("x' = -x\nx(0) = 1\n");
        SolveOptions opts;
        opts.t_end = 1.0;
        opts.M = 10;
        opts.h_init = 1e-3;

        auto sol1 = Solve(sys, opts);
        auto sol2 = Solve(sys, opts);

        ASSERT_EQ(sol1.points.size(), sol2.points.size());
        for (std::size_t i = 0; i < sol1.points.size(); ++i) {
            EXPECT_DOUBLE_EQ(sol1.points[i].t, sol2.points[i].t);
            ASSERT_EQ(sol1.points[i].x.size(), sol2.points[i].x.size());
            for (std::size_t j = 0; j < sol1.points[i].x.size(); ++j) {
                EXPECT_DOUBLE_EQ(sol1.points[i].x[j], sol2.points[i].x[j]);
            }
        }
    }

    TEST(Solver, AccuracyGrowsWithM) {
        auto sys = ParseSystem("x' = -x\nx(0) = 1\n");
        SolveOptions opts;
        opts.t_end = 1.0;
        opts.h_init = 0.05;

        opts.M = 5;
        auto sol5 = Solve(sys, opts);
        opts.M = 20;
        auto sol20 = Solve(sys, opts);

        const double err5 = std::abs(sol5.points.back().x[0] - std::exp(-1.0));
        const double err20 = std::abs(sol20.points.back().x[0] - std::exp(-1.0));
        EXPECT_LT(err20, err5);
    }

    TEST(Solver, AccuracyGrowsWithSmallerH) {
        auto sys = ParseSystem("x' = -x\nx(0) = 1\n");
        SolveOptions opts;
        opts.t_end = 1.0;
        opts.M = 5;

        // Отключаем адаптацию: h_max = h_init заставляет PickStep держать
        // фиксированный шаг, и тогда зависимость точности от h_init
        // проверяема в чистом виде.
        opts.h_init = 0.1;
        opts.h_max = opts.h_init;
        auto sol1 = Solve(sys, opts);

        opts.h_init = 0.05;
        opts.h_max = opts.h_init;
        auto sol2 = Solve(sys, opts);

        const double err1 = std::abs(sol1.points.back().x[0] - std::exp(-1.0));
        const double err2 = std::abs(sol2.points.back().x[0] - std::exp(-1.0));
        EXPECT_LT(err2, err1);
    }

    // ============================================================================
    // Ошибки
    // ============================================================================

    TEST(Solver, ErrorOnNonPositiveInterval) {
        auto sys = ParseSystem("x' = -x\nx(0) = 1\n");
        SolveOptions opts;
        opts.t_end = -1.0;
        EXPECT_THROW(Solve(sys, opts), SolverError);
    }

    TEST(Solver, ErrorOnMaxStepsExceeded) {
        auto sys = ParseSystem("x' = -x\nx(0) = 1\n");
        SolveOptions opts;
        opts.t_end = 1.0;
        opts.h_init = 1e-6;
        opts.max_steps = 10;
        EXPECT_THROW(Solve(sys, opts), SolverError);
    }

    TEST(Solver, ErrorOnNonQuadraticRhs) {
        auto sys = ParseSystem("x' = x^3\nx(0) = 1\n");
        SolveOptions opts;
        opts.t_end = 0.1;
        opts.M = 10;
        opts.h_init = 1e-3;
        EXPECT_THROW(Solve(sys, opts), SolverError);
    }

    // ============================================================================
    // Полный пайплайн
    // ============================================================================

    TEST(Solver, FullPipelinePendulum) {
        // x'' = -sin(x), x(0)=0, x'(0)=1 — маятник.
        // При t=0.1 аналитика x(t) ≈ sin(t) с поправкой O(1e-5).
        auto sys = ParseSystem("x'' = -sin(x)\nx(0) = 0\nx'(0) = 1\n");
        NormalizeSystem(sys);
        OrderReducer(sys);
        Autonomize(sys);       // no-op: t не встречается
        Polynomize(sys);       // введёт v = sin(x)
        Quadratize(sys);       // возможно, введёт q-переменные

        SolveOptions opts;
        opts.t_end = 0.1;
        opts.M = 20;
        opts.h_init = 1e-4;

        auto sol = Solve(sys, opts);
        ASSERT_FALSE(sol.points.empty());

        // Найти индекс исходной x в списке функций
        const std::size_t kx = IndexOf(sol.functions, "x");
        ASSERT_LT(kx, sol.functions.size());

        EXPECT_NEAR(sol.points.back().x[kx], std::sin(0.1), 1e-3);
    }

    TEST(Solver, MultipleT0Throws) {
        // Разные t0 в IC разных функций — неоднозначно.
        RawSystem sys;
        sys.functions = { "x", "y" };
        {
            Equation eq;
            eq.lhs = MakeDerivative("x", 1);
            eq.rhs = MakeFunction("y");
            sys.equations.push_back(std::move(eq));
        }
        {
            Equation eq;
            eq.lhs = MakeDerivative("y", 1);
            eq.rhs = MakeBinary(Binary::Op::Mul,
                MakeNumber(-1.0),
                MakeFunction("x"));
            sys.equations.push_back(std::move(eq));
        }
        sys.initial_conditions.push_back({ "x", 0, 0.0, 1.0 });
        sys.initial_conditions.push_back({ "y", 0, 0.5, 0.0 });  // другой t0
        SolveOptions opts;
        opts.t_end = 0.1;
        opts.M = 5;
        opts.h_init = 1e-3;
        EXPECT_THROW(Solve(sys, opts), SolverError);
    }

    TEST(Solver, FunctionsAndVariableStored) {
        auto sys = ParseSystem("x' = y\ny' = -x\nx(0) = 1\ny(0) = 0\n");
        SolveOptions opts;
        opts.t_end = 0.5;
        opts.M = 10;
        opts.h_init = 1e-3;
        auto sol = Solve(sys, opts);
        EXPECT_EQ(sol.independent_variable, "t");
        ASSERT_EQ(sol.functions.size(), 2u);
        EXPECT_EQ(sol.functions[0], "x");
        EXPECT_EQ(sol.functions[1], "y");
    }

    TEST(Solver, ShortIntervalOneStep) {
        // t_end - t_0 < h_init → один обрезанный шаг.
        auto sys = ParseSystem("x' = -x\nx(0) = 1\n");
        SolveOptions opts;
        opts.t_end = 1e-5;
        opts.M = 10;
        opts.h_init = 1e-2;   // >> t_end
        auto sol = Solve(sys, opts);
        EXPECT_EQ(sol.steps, 1u);
        EXPECT_NEAR(sol.t_final, 1e-5, 1e-15);
    }

    TEST(Solver, ZeroMMeansTwenty) {
        auto sys = ParseSystem("x' = -x\nx(0) = 1\n");
        SolveOptions opts;
        opts.t_end = 0.1;
        opts.M = 0;         // "не задан" → должно стать 20
        opts.h_init = 1e-3;
        auto sol = Solve(sys, opts);
        EXPECT_EQ(sol.order_used, 20u);
    }

}  // namespace diffuri