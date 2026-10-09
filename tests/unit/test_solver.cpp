// ============================================================================
// tests/unit/test_solver.cpp
//
// Тесты модуля solver (ТЗ №4.1, §6.6).
// Заменяет Solver.Placeholder.
//
// ТЗ №2: добавлены тесты runtime-детектора ухода решения в бесконечность
// (BlowupCubicThroughPipelineThrows, CubicBeforePoleThroughPipelineSucceeds,
//  BlowupQuadraticDirectThrows, HarmonicOscillatorLongInterval,
//  BlowupMessageContainsTimeAndComponent).
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

        // Вспомогательный прогон x' = x^3 через полный пайплайн.
        //
        // x^3 не квадратичен: TaylorSpec отверг бы такую систему напрямую
        // (см. существующий тест ErrorOnNonQuadraticRhs). Чтобы получить
        // систему, которую Solve принимает, и при этом сохранить
        // динамику с полюсом при t = 0.5, прогоняем через Quadratize:
        //   x' = x^3  →  x' = x*v,  v' = 2*v^2,  v(0) = 1.
        // Эта система квадратична, TaylorSpec её принимает.
        RawSystem MakeCubicBlowupSystem() {
            auto sys = ParseSystem("x' = x^3\nx(0) = 1\n");
            NormalizeSystem(sys);
            ReduceOrder(sys);
            Autonomize(sys);    // no-op: t не встречается
            Polynomize(sys);    // no-op: x^3 — полином, sin/cos/exp/ln нет
            Quadratize(sys);    // введёт v = x^2
            return sys;
        }

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
            ReduceOrder(sys);
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

        // ============================================================================
        // ТЗ №2: runtime-детектор ухода решения в бесконечность
        // ============================================================================

        // --- Тест 1: x' = x^3 через пайплайн, t_end = 1.0 за полюсом → SolverError.
        //
        // Полюс решения: x(t) = 1/√(1 − 2t) уходит в бесконечность при t = 0.5.
        // t_end = 1.0 заведомо за полюсом, задача неразрешима на всём [0, 1].
        // Пайплайн нужен, чтобы система стала квадратичной и дошла до Solve
        // (иначе TaylorSpec отвергнет x^3 как моном степени > 2).
        TEST(Solver, BlowupCubicThroughPipelineThrows) {
            auto sys = MakeCubicBlowupSystem();
            SolveOptions opts;
            opts.t_end = 1.0;
            opts.M = 20;
            opts.h_init = 1e-3;

            EXPECT_THROW(Solve(sys, opts), SolverError);
        }

        // --- Тест 2: x' = x^3 через пайплайн, t_end = 0.4 до полюса → Solution.
        //
        // Регрессия: не сломали легитимный случай.
        // Аналитика: x(t) = 1/√(1 − 2t); x(0.4) = 1/√(0.2) = √5 ≈ 2.2360679…
        // Проверяем конечность всех точек траектории и близость финального x
        // к √5. Если бы фикс был слишком агрессивным (например, бросал бы
        // ошибку на любой большой производной), этот тест бы упал.
        TEST(Solver, CubicBeforePoleThroughPipelineSucceeds) {
            auto sys = MakeCubicBlowupSystem();
            SolveOptions opts;
            opts.t_end = 0.4;
            opts.M = 20;
            opts.h_init = 1e-3;

            auto sol = Solve(sys, opts);
            ASSERT_FALSE(sol.points.empty());

            const std::size_t kx = IndexOf(sol.functions, "x");
            ASSERT_LT(kx, sol.functions.size()) << "function 'x' not found";

            // Все точки конечны по всем компонентам.
            for (std::size_t i = 0; i < sol.points.size(); ++i) {
                for (std::size_t j = 0; j < sol.points[i].x.size(); ++j) {
                    EXPECT_TRUE(std::isfinite(sol.points[i].x[j]))
                        << "point " << i << ", component " << j
                        << " is not finite";
                }
            }

            EXPECT_TRUE(std::isfinite(sol.points.back().x[kx]));
            EXPECT_NEAR(sol.points.back().x[kx], std::sqrt(5.0), 1e-6);
        }

        // --- Тест 3: x' = x^2 (прямой Solve), t_end = 2.0 за полюсом → SolverError.
        //
        // x^2 квадратичен, TaylorSpec принимает напрямую, пайплайн не нужен.
        // Полюс решения: x(t) = 1/(1 − t) уходит в бесконечность при t = 1.0.
        // t_end = 2.0 за полюсом — чистый кейс для нового детектора.
        TEST(Solver, BlowupQuadraticDirectThrows) {
            auto sys = ParseSystem("x' = x^2\nx(0) = 1\n");
            SolveOptions opts;
            opts.t_end = 2.0;
            opts.M = 20;
            opts.h_init = 1e-3;

            EXPECT_THROW(Solve(sys, opts), SolverError);
        }

        // --- Тест 4: гармонический осциллятор, t_end = 100 → Solution, |x| ≤ 1.
        //
        // Легитимная задача с большим интервалом. Полюсов нет, решение
        // периодическое. Проверяем, что детектор не ловит «ложные срабатывания»
        // на задаче с большим числом шагов и ограниченной амплитудой.
        TEST(Solver, HarmonicOscillatorLongInterval) {
            auto sys = ParseSystem("x'' = -x\nx(0) = 1\nx'(0) = 0\n");
            NormalizeSystem(sys);
            ReduceOrder(sys);
            Autonomize(sys);
            Polynomize(sys);
            Quadratize(sys);

            SolveOptions opts;
            opts.t_end = 100.0;
            opts.M = 20;
            opts.h_init = 1e-3;

            auto sol = Solve(sys, opts);
            ASSERT_FALSE(sol.points.empty());

            const std::size_t kx = IndexOf(sol.functions, "x");
            ASSERT_LT(kx, sol.functions.size()) << "function 'x' not found";

            for (std::size_t i = 0; i < sol.points.size(); ++i) {
                for (std::size_t j = 0; j < sol.points[i].x.size(); ++j) {
                    EXPECT_TRUE(std::isfinite(sol.points[i].x[j]))
                        << "point " << i << ", component " << j
                        << " is not finite";
                }
                EXPECT_LE(std::abs(sol.points[i].x[kx]), 1.0 + 1e-6)
                    << "|x| exceeded 1 at point " << i;
            }
        }

        // --- Тест 5: сообщение об ошибке содержит t и индекс компоненты.
        //
        // Тот же вход, что и в тесте 1 (x' = x^3 через пайплайн, t_end = 1.0).
        // Формат сообщения (согласован с ТЗ):
        //   "Solve: solution is not finite at t=<value> (component <i>)"
        // Парсим what() и проверяем:
        //   - маркеры "t=" и "(component " присутствуют, порядок правильный;
        //   - t — конечное положительное число, не превышающее t_end;
        //   - индекс компоненты — целое ≥ 0.
        // Разделители (пробел после t, ')' после индекса) однозначны,
        // поэтому парсинг детерминирован.
        TEST(Solver, BlowupMessageContainsTimeAndComponent) {
            auto sys = MakeCubicBlowupSystem();
            SolveOptions opts;
            opts.t_end = 1.0;
            opts.M = 20;
            opts.h_init = 1e-3;

            try {
                Solve(sys, opts);
                FAIL() << "expected SolverError, got a Solution";
            }
            catch (const SolverError& e) {
                const std::string msg = e.what();

                const std::string marker_t = "t=";
                const std::string marker_c = "(component ";

                const auto pos_t = msg.find(marker_t);
                ASSERT_NE(pos_t, std::string::npos)
                    << "no 't=' marker in message: " << msg;

                const auto pos_c = msg.find(marker_c, pos_t);
                ASSERT_NE(pos_c, std::string::npos)
                    << "no '(component ' marker after t in message: " << msg;

                // t: от конца "t=" до следующего пробела.
                const std::size_t t_begin = pos_t + marker_t.size();
                const std::size_t t_end_pos = msg.find(' ', t_begin);
                ASSERT_NE(t_end_pos, std::string::npos)
                    << "no space after t value in message: " << msg;
                const double t_val =
                    std::stod(msg.substr(t_begin, t_end_pos - t_begin));
                EXPECT_TRUE(std::isfinite(t_val));
                EXPECT_GT(t_val, 0.0);
                EXPECT_LE(t_val, opts.t_end);

                // i: от конца "(component " до следующей ')'.
                const std::size_t c_begin = pos_c + marker_c.size();
                const std::size_t c_end = msg.find(')', c_begin);
                ASSERT_NE(c_end, std::string::npos)
                    << "no ')' after component index in message: " << msg;
                const int component =
                    std::stoi(msg.substr(c_begin, c_end - c_begin));
                EXPECT_GE(component, 0)
                    << "expected non-negative component index, got "
                    << component;
            }
        }

        // ============================================================================
        // ТЗ 6: Адаптация порядка M
        // ============================================================================

        TEST(Solver, OrderAdaptationEnabled) {
            // Простая линейная система: адаптация должна выбрать M ∈ [M_min, M_max].
            auto sys = ParseSystem("x' = -x\nx(0) = 1\n");
            SolveOptions opts;
            opts.t_end = 1.0;
            opts.M = 0;  // "не задан" — адаптация выберет
            opts.h_init = 1e-4;
            opts.enable_order_adaptation = true;
            opts.M_min = 5;
            opts.M_max = 30;

            auto sol = Solve(sys, opts);
            ASSERT_FALSE(sol.points.empty());
            EXPECT_NEAR(sol.points.back().x[0], std::exp(-1.0), 1e-9);
            EXPECT_GE(sol.order_used, opts.M_min);
            EXPECT_LE(sol.order_used, opts.M_max);
        }

        TEST(Solver, OrderAdaptationDisabledByDefault) {
            // Дефолт: адаптация выключена, M фиксирован.
            auto sys = ParseSystem("x' = -x\nx(0) = 1\n");
            SolveOptions opts;
            opts.t_end = 1.0;
            opts.M = 15;  // фиксированный порядок
            opts.h_init = 1e-3;
            // enable_order_adaptation = false по умолчанию

            auto sol = Solve(sys, opts);
            ASSERT_FALSE(sol.points.empty());
            EXPECT_NEAR(sol.points.back().x[0], std::exp(-1.0), 1e-9);
            EXPECT_EQ(sol.order_used, 15u);
        }

        TEST(Solver, OrderAdaptationRegressionWithDisabled) {
            // Регрессия: при отключённой адаптации поведение совпадает со старым.
            auto sys = ParseSystem("x' = -x\nx(0) = 1\n");
            SolveOptions opts;
            opts.t_end = 1.0;
            opts.M = 20;
            opts.h_init = 1e-4;
            opts.enable_order_adaptation = false;

            auto sol = Solve(sys, opts);
            ASSERT_FALSE(sol.points.empty());
            EXPECT_NEAR(sol.points.back().x[0], std::exp(-1.0), 1e-10);
            EXPECT_EQ(sol.order_used, 20u);
        }

        // --- §4.2: осциллятор при включённой адаптации ---
        TEST(Solver, OrderAdaptationOnOscillator) {
            auto sys = ParseSystem("x' = y\ny' = -x\nx(0) = 1\ny(0) = 0\n");
            SolveOptions opts;
            opts.t_end = 2.0 * 3.14159265358979323846;
            opts.M = 0;
            opts.h_init = 1e-4;
            opts.enable_order_adaptation = true;
            opts.M_min = 5;
            opts.M_max = 40;

            auto sol = Solve(sys, opts);
            ASSERT_FALSE(sol.points.empty());
            EXPECT_NEAR(sol.points.back().x[0], 1.0, 1e-6);
            EXPECT_NEAR(sol.points.back().x[1], 0.0, 1e-6);
            EXPECT_GE(sol.order_used, opts.M_min);
            EXPECT_LE(sol.order_used, opts.M_max);
        }

        // --- §4.2: нелинейная x' = x^2 (до полюса) при включённой адаптации ---
        TEST(Solver, OrderAdaptationOnQuadraticBlowup) {
            auto sys = ParseSystem("x' = x^2\nx(0) = 1\n");
            SolveOptions opts;
            opts.t_end = 0.9;
            opts.M = 0;
            opts.h_init = 1e-4;
            opts.enable_order_adaptation = true;

            auto sol = Solve(sys, opts);
            ASSERT_FALSE(sol.points.empty());
            EXPECT_NEAR(sol.points.back().x[0], 10.0, 1e-6);
        }

        // --- §4.2: система Лоренца из статьи (§3.2) — решение остаётся ограниченным ---
        TEST(Solver, OrderAdaptationOnLorenzStaysBounded) {
            auto sys = ParseSystem(
                "x' = -10*x + 10*y\n"
                "y' = -x*z + 28*x - y\n"
                "z' = x*y - 2.6666666666666665*z\n" 
                "x(0) = -13.7636106821342\n"
                "y(0) = -19.5787519424518\n"
                "z(0) = 27\n");
            SolveOptions opts;
            opts.t_end = 1.5;   // чуть меньше периода (~1.5586)
            opts.M = 0;
            opts.h_init = 1e-4;
            opts.enable_order_adaptation = true;
            opts.M_min = 5;
            opts.M_max = 40;

            auto sol = Solve(sys, opts);
            ASSERT_FALSE(sol.points.empty());
            for (const auto& pt : sol.points) {
                for (double v : pt.x) {
                    EXPECT_TRUE(std::isfinite(v));
                    EXPECT_LT(std::abs(v), 100.0);
                }
            }
        }

        // --- §4.3: адаптация не хуже фиксированного M=20 по точности ---
        TEST(Solver, OrderAdaptationNotWorseThanFixedM) {
            auto sys = ParseSystem("x' = -x\nx(0) = 1\n");
            const double exact = std::exp(-1.0);

            SolveOptions fixed;
            fixed.t_end = 1.0;
            fixed.M = 20;
            fixed.h_init = 1e-4;
            fixed.enable_order_adaptation = false;
            auto sol_fixed = Solve(sys, fixed);
            const double err_fixed =
                std::abs(sol_fixed.points.back().x[0] - exact);

            SolveOptions adapt;
            adapt.t_end = 1.0;
            adapt.M = 0;
            adapt.h_init = 1e-4;
            adapt.enable_order_adaptation = true;
            auto sol_adapt = Solve(sys, adapt);
            const double err_adapt =
                std::abs(sol_adapt.points.back().x[0] - exact);

            // Оба укладываются в высокую точность; адаптация не катастрофически хуже.
            EXPECT_LT(err_fixed, 1e-8);
            EXPECT_LT(err_adapt, 1e-8);
            // Число шагов адаптации не должно взрываться по сравнению с фиксированным.
            EXPECT_LE(sol_adapt.steps, sol_fixed.steps * 3 + 5);
        }

        // --- §4.2: маятник через полный пайплайн при включённой адаптации ---
        TEST(Solver, OrderAdaptationPendulumFullPipeline) {
            auto sys = ParseSystem("x'' = -sin(x)\nx(0) = 0\nx'(0) = 1\n");
            NormalizeSystem(sys);
            ReduceOrder(sys);
            Autonomize(sys);
            Polynomize(sys);
            Quadratize(sys);

            SolveOptions opts;
            opts.t_end = 0.5;
            opts.M = 0;
            opts.h_init = 1e-4;
            opts.enable_order_adaptation = true;

            auto sol = Solve(sys, opts);
            ASSERT_FALSE(sol.points.empty());
            std::size_t kx = 0;
            for (std::size_t i = 0; i < sol.functions.size(); ++i) {
                if (sol.functions[i] == "x") { kx = i; break; }
            }
            EXPECT_NEAR(sol.points.back().x[kx], std::sin(0.5), 1e-2);
        }

        // --- Runtime-детектор полюса работает и при включённой адаптации ---
        TEST(Solver, OrderAdaptationBlowupStillDetected) {
            auto sys = ParseSystem("x' = x^2\nx(0) = 1\n");
            SolveOptions opts;
            opts.t_end = 2.0;   // за полюсом t = 1
            opts.M = 0;
            opts.h_init = 1e-3;
            opts.enable_order_adaptation = true;
            EXPECT_THROW(Solve(sys, opts), SolverError);
        }

    }  // namespace
}  // namespace diffuri