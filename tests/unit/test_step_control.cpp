// ============================================================================
// tests/unit/test_step_control.cpp
//
// Тесты модуля step_control: адаптивный выбор шага по §2.1.4 статьи
// [Бабаджанянц, Большаков 2012].
// ============================================================================
#include <gtest/gtest.h>

#include <cmath>
#include <string>
#include <vector>

#include "input/input.h"
#include "solver/solver.h"
#include "solver/step_control.h"
#include "solver/taylor_spec.h"
#include "solver/taylor_table.h"

namespace diffuri {
    namespace {

        struct Fixture {
            RawSystem           sys;
            TaylorSpec          spec;
            std::vector<double> x0;
            TaylorTable         table;
            SolveOptions        opts;

            Fixture()
                : sys(ParseSystem("x' = -x\nx(0) = 1\n")),
                spec(BuildTaylorSpec(sys)),
                x0{ 1.0 },
                table(spec, x0, 25) {  // max_order >= M + K = 21
                opts.h_min = 1e-3;
                opts.h_max = 1.0;
                opts.rtol = 1e-10;
                opts.atol = 1e-12;
                opts.K = 1;
            }
        };

    }  // namespace

    // --- Зажим в границы -----------------------------------------------------

    TEST(StepControl, ClampsToMin) {
        Fixture f;
        f.opts.h_min = 1e-2;
        f.opts.h_max = 1.0;
        const double h_next = PickStep(f.table, 1e-9, 20, f.opts);
        EXPECT_DOUBLE_EQ(h_next, f.opts.h_min);
    }

    TEST(StepControl, ClampsToMax) {
        Fixture f;
        f.opts.h_min = 1e-3;
        f.opts.h_max = 1e-1;
        const double h_next = PickStep(f.table, 10.0, 20, f.opts);
        EXPECT_DOUBLE_EQ(h_next, f.opts.h_max);
    }

    TEST(StepControl, NonPositiveHReturnsHMin) {
        Fixture f;
        f.opts.h_min = 1e-4;
        EXPECT_DOUBLE_EQ(PickStep(f.table, 0.0, 20, f.opts), f.opts.h_min);
        EXPECT_DOUBLE_EQ(PickStep(f.table, -1.0, 20, f.opts), f.opts.h_min);
    }

    // --- «Хороший» h не улетает ни в 0, ни в h_max --------------------------

    TEST(StepControl, ReturnsReasonableH) {
        Fixture f;
        f.opts.h_min = 1e-3;
        f.opts.h_max = 1.0;
        const double h = 0.1;
        const double h_next = PickStep(f.table, h, 20, f.opts);
        EXPECT_GT(h_next, 0.0);
        EXPECT_LT(h_next, f.opts.h_max);  // не упирается в верхнюю границу
        EXPECT_LE(h_next, 2.0 * h);       // ограничение роста
        EXPECT_GE(h_next, h);             // eps << 1 → шаг растёт
    }

    // --- Монотонность по rtol -----------------------------------------------

    TEST(StepControl, SmallerRtolSmallerStep) {
        Fixture f;
        f.opts.h_min = 1e-6;
        f.opts.h_max = 1.0;
        const double h = 0.03;

        f.opts.rtol = 1e-10;
        const double h_loose = PickStep(f.table, h, 5, f.opts);
        f.opts.rtol = 1e-12;
        const double h_tight = PickStep(f.table, h, 5, f.opts);

        EXPECT_LT(h_tight, h_loose);
    }

    // --- Монотонность по M --------------------------------------------------

    TEST(StepControl, LargerMAllowsLargerStep) {
        Fixture f;
        f.opts.h_min = 1e-6;
        f.opts.h_max = 1.0;
        const double h = 0.03;

        const double h_low_M = PickStep(f.table, h, 3, f.opts);
        const double h_high_M = PickStep(f.table, h, 5, f.opts);

        EXPECT_LT(h_low_M, h_high_M);
    }

    // --- Ограничение роста --------------------------------------------------

    TEST(StepControl, GrowthIsLimited) {
        Fixture f;
        f.opts.h_min = 1e-12;
        f.opts.h_max = 1.0;
        const double h = 1e-6;
        const double h_next = PickStep(f.table, h, 20, f.opts);
        EXPECT_LE(h_next, 2.0 * h);  // ограничение роста работает
        EXPECT_GT(h_next, h);        // и всё же растёт
    }

    // --- Жёсткая компонента уменьшает шаг ----------------------------------

    TEST(StepControl, StiffComponentReducesStep) {
        std::vector<double> x0{ 1.0 };
        SolveOptions opts;
        opts.h_min = 1e-12;
        opts.h_max = 1.0;
        opts.rtol = 1e-10;
        opts.atol = 1e-12;
        opts.K = 1;

        const double h = 0.1;

        // x' = -1000·x: h = 0.1 >> 1/1000, eps >> 1 → шаг должен уменьшиться.
        auto sys_stiff = ParseSystem("x' = -1000*x\nx(0) = 1\n");
        TaylorSpec spec_stiff = BuildTaylorSpec(sys_stiff);
        TaylorTable table_stiff(spec_stiff, x0, 25);
        const double h_stiff = PickStep(table_stiff, h, 20, opts);
        EXPECT_LT(h_stiff, h);

        // x' = -x: тот же h даёт eps << 1 → шаг должен вырасти.
        auto sys_mild = ParseSystem("x' = -x\nx(0) = 1\n");
        TaylorSpec spec_mild = BuildTaylorSpec(sys_mild);
        TaylorTable table_mild(spec_mild, x0, 25);
        const double h_mild = PickStep(table_mild, h, 20, opts);
        EXPECT_GT(h_mild, h);
    }

    // --- Property: адаптив быстрее фиксированного h = 1e-4 ------------------

    TEST(StepControl, AdaptiveBeatsFixedOnExponentialDecay) {
        auto sys = ParseSystem("x' = -x\nx(0) = 1\n");
        SolveOptions opts;
        opts.t_end = 1.0;
        opts.M = 20;
        opts.h_init = 1e-4;
        // h_max = 10 >> «естественного» масштаба шага (~2.9) — иначе
        // последний шаг упрётся в h_max и проверка ниже станет фикцией.
        opts.h_max = 10.0;
        opts.rtol = 1e-10;
        opts.atol = 1e-12;
        opts.K = 1;

        auto sol = Solve(sys, opts);
        ASSERT_GE(sol.points.size(), 2u);

        // Точность не хуже 1e-10.
        EXPECT_NEAR(sol.points.back().x[0], std::exp(-1.0), 1e-10);

        // Фиксированный h = 1e-4 дал бы 10000 шагов; адаптив с ростом 2×
        // доходит до натурального масштаба ~2.9 примерно за 15 шагов.
        EXPECT_LT(sol.steps, 1000u);

        // Последний шаг НЕ упёрся в h_max — значит адаптивность реальна,
        // а не является следствием клампа.
        const double h_last = sol.points.back().t
            - sol.points[sol.points.size() - 2].t;
        EXPECT_LT(h_last, opts.h_max);
    }

    // =========================================================================
// Дополнительное покрытие: bounds как property, влияние atol,
// устойчивость повторного применения, нелинейная система,
// крайние rtol.
// =========================================================================

// --- Property: при любых входных h результат в [h_min, h_max] -------------
//
// Сетка h от «абсурдно маленького» до «абсурдно большого». Все
// результаты должны лежать в границах, независимо от того, что
// произошло внутри (рост, уменьшение, ограничение роста).
    TEST(StepControl, ResultAlwaysInBounds) {
        Fixture f;
        f.opts.h_min = 1e-4;
        f.opts.h_max = 0.5;

        for (double h : {1e-20, 1e-10, 1e-6, 1e-3, 1e-1, 1.0, 100.0}) {
            const double h_next = PickStep(f.table, h, 20, f.opts);
            EXPECT_GE(h_next, f.opts.h_min) << "h = " << h;
            EXPECT_LE(h_next, f.opts.h_max) << "h = " << h;
        }
    }

    // --- Влияние atol: рост atol → рост шага --------------------------------
    //
    // Знаменатель в ErrorEstimate = atol + rtol·scale. При фиксированном h
    // и rtol больший atol увеличивает знаменатель, уменьшает eps и,
    // следовательно, увеличивает h_b. Для x' = -x при h = 0.05 и M = 5:
    //   atol = 1e-12 → eps ≈ 0.2 → h_b ≈ 0.065
    //   atol = 1e-3  → eps ≈ 2e-8 → h_b упрётся в growth limit (2·h = 0.1)
    // Оба варианта остаются внутри границ, но первое — строго меньше.
    TEST(StepControl, LargerAtolAllowsLargerStep) {
        Fixture f;
        f.opts.h_min = 1e-6;
        f.opts.h_max = 1.0;
        f.opts.rtol = 1e-10;
        const double h = 0.05;

        f.opts.atol = 1e-12;
        const double h_tight = PickStep(f.table, h, 5, f.opts);
        f.opts.atol = 1e-3;
        const double h_loose = PickStep(f.table, h, 5, f.opts);

        EXPECT_GT(h_loose, h_tight);
    }

    // --- Устойчивость повторного применения ---------------------------------
    //
    // Начиная с заведомо малого h, применяем PickStep итеративно, подавая
    // результат обратно. Последовательность должна сойтись к фиксированной
    // точке (eps ≈ 1). Через 20 итераций разность двух последних шагов
    // должна быть мала в относительном выражении.
    TEST(StepControl, StepStabilizesUnderRepeatedPick) {
        Fixture f;
        f.opts.h_min = 1e-12;
        f.opts.h_max = 10.0;
        f.opts.M = 20;

        double h = 1e-3;
        for (int i = 0; i < 20; ++i) {
            h = PickStep(f.table, h, 20, f.opts);
        }
        const double h_prev = h;
        const double h_next = PickStep(f.table, h, 20, f.opts);

        // Относительное изменение на итерации < 5%.
        EXPECT_LT(std::abs(h_next - h_prev), 0.05 * h_prev);
    }

    // --- Нелинейная система тоже адаптируется -------------------------------
    //
    // x' = x^2, x(0) = 1. Решение растёт; радиус сходимости при x = 1 равен
    // примерно 1, и при разумных опциях h_b должен лежать в границах.
    // Дополнительно: уменьшение rtol уменьшает шаг (та же монотонность,
    // что и для x' = -x, но на нелинейной системе).
    TEST(StepControl, NonlinearQuadraticAlsoAdapts) {
        auto sys = ParseSystem("x' = x^2\nx(0) = 1\n");
        TaylorSpec spec = BuildTaylorSpec(sys);
        std::vector<double> x0{ 1.0 };
        TaylorTable table(spec, x0, 25);

        SolveOptions opts;
        opts.h_min = 1e-6;
        opts.h_max = 1.0;
        opts.atol = 1e-12;
        opts.K = 1;

        opts.rtol = 1e-8;
        const double h_loose = PickStep(table, 0.1, 5, opts);
        opts.rtol = 1e-12;
        const double h_tight = PickStep(table, 0.1, 5, opts);

        EXPECT_GE(h_loose, opts.h_min);
        EXPECT_LE(h_loose, opts.h_max);
        EXPECT_GE(h_tight, opts.h_min);
        EXPECT_LE(h_tight, opts.h_max);
        EXPECT_GT(h_loose, h_tight);
    }

    // --- Крайне жёсткий rtol: шаг упирается в h_min -------------------------
    //
    // rtol и atol, стремящиеся к нулю, дают eps → ∞ ⇒ h_b → 0 ⇒ clamp
    // до h_min. Проверка, что нижняя граница реально работает не только
    // при h < h_min на входе, но и при крошечном h_b на выходе.
    TEST(StepControl, ExtremeTightRtolSaturatesAtHMin) {
        Fixture f;
        f.opts.h_min = 1e-3;
        f.opts.h_max = 1.0;
        f.opts.rtol = 1e-300;
        f.opts.atol = 1e-300;

        const double h_next = PickStep(f.table, 0.1, 20, f.opts);
        EXPECT_DOUBLE_EQ(h_next, f.opts.h_min);
    }

    // --- Крайне свободный rtol: шаг растёт до h_max за несколько итераций ----
    //
    // rtol = atol = 1 дают eps << 1 ⇒ h_b >> h_max. Ограничение роста
    // h_new ≤ 2·h не даёт прыгнуть в h_max за один вызов; при повторном
    // применении PickStep (таблица не меняется — точка x(t_0) та же) шаг
    // удваивается и через несколько итераций достигает h_max.
    TEST(StepControl, ExtremeLooseRtolSaturatesAtHMax) {
        Fixture f;
        f.opts.h_min = 1e-6;
        f.opts.h_max = 0.5;
        f.opts.rtol = 1.0;
        f.opts.atol = 1.0;

        double h = 0.01;
        for (int i = 0; i < 100; ++i) {
            const double h_next = PickStep(f.table, h, 20, f.opts);
            if (h_next == h) break;  // насыщение
            h = h_next;
        }
        EXPECT_DOUBLE_EQ(h, f.opts.h_max);
    }

}  // namespace diffuri