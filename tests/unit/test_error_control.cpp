// ============================================================================
// tests/unit/test_error_control.cpp
//
// Тесты модуля error_control (ТЗ №4.1, §6.4).
// Проверяется норма L² из §2.1.4 статьи:
//
//   ε(h) = sqrt( (1/n) · Σ_i ( δT_i(h) /
//                              (Δ + ε_rel · max(|x_i|, |T_{M,i}(h)|)) )² )
// ============================================================================
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

#include "input/input.h"
#include "solver/error_control.h"
#include "solver/solver.h"
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
                table(spec, x0, 10) {
                opts.rtol = 1e-10;
                opts.atol = 1e-12;
            }
        };

    }  // namespace

    // --- §6.4: монотонность по h -------------------------------------------------

    TEST(ErrorControl, MonotoneInH) {
        Fixture f;
        const double eps_small = ErrorEstimate(f.table, f.x0, 0.01, 5, 1, f.opts);
        const double eps_mid = ErrorEstimate(f.table, f.x0, 0.05, 5, 1, f.opts);
        const double eps_big = ErrorEstimate(f.table, f.x0, 0.10, 5, 1, f.opts);

        EXPECT_GT(eps_small, 0.0);
        EXPECT_GT(eps_mid, eps_small);
        EXPECT_GT(eps_big, eps_mid);
    }

    // --- §6.4: ε(h) → 0 при h → 0 -----------------------------------------------
    //
    // Для x' = -x, M=5, K=1 при малых h доминирует член h^6/6!:
    //   ε ≈ (h^6 / 720) / (atol + rtol · max(|x|, |T_M|)),
    // где max(|x|, |T_M|) = 1, поэтому ε ≈ h^6 / (720 · 1.01e-10).
    TEST(ErrorControl, TendsToZero) {
        Fixture f;
        const double eps_h1e4 = ErrorEstimate(f.table, f.x0, 1e-4, 5, 1, f.opts);
        const double eps_h1e6 = ErrorEstimate(f.table, f.x0, 1e-6, 5, 1, f.opts);
        const double eps_h1e8 = ErrorEstimate(f.table, f.x0, 1e-8, 5, 1, f.opts);

        // Монотонное убывание.
        EXPECT_LT(eps_h1e6, eps_h1e4);
        EXPECT_LT(eps_h1e8, eps_h1e6);

        // Абсолютная малость при малых h (порядок h^6, не ноль).
        EXPECT_LT(eps_h1e6, 1e-25);
        EXPECT_LT(eps_h1e8, 1e-37);
    }

    // --- §6.4: при большом h — большое число -------------------------------------

    TEST(ErrorControl, LargeHIsLarge) {
        Fixture f;
        const double eps = ErrorEstimate(f.table, f.x0, 10.0, 5, 1, f.opts);
        EXPECT_GT(eps, 1.0);
    }

    // --- Нулевое x: знаменатель не вырождается -----------------------------------
    //
    // При x_i = 0 в знаменателе остаётся max(|0|, |T_{M,i}(h)|) = |T_{M,i}(h)|,
    // то есть масштаб определяется полиномом, а не нулём.
    TEST(ErrorControl, ZeroStateUsesAtolOnly) {
        Fixture f;
        f.x0[0] = 0.0;
        // Таблица всё ещё построена для x(0)=1; в x передаётся 0 —
        // в знаменателе будет max(0, |T_5(0.1)|) = |T_5(0.1)|.
        const double eps = ErrorEstimate(f.table, f.x0, 0.1, 5, 1, f.opts);
        EXPECT_GT(eps, 0.0);
        EXPECT_FALSE(std::isinf(eps));
        EXPECT_FALSE(std::isnan(eps));
    }

    // --- §2.1.4: сверка с ручным вычислением -------------------------------------
    //
    // x' = -x, x(0) = 1, M = 5, K = 1, h = 0.1.
    // Коэффициенты Тейлора: c_p = (-1)^p / p!.
    // T_5(0.1) = 1 - 0.1 + 0.1²/2 - 0.1³/6 + 0.1⁴/24 - 0.1⁵/120
    //          ≈ 0.9048374167.
    // δT       = T_6(0.1) - T_5(0.1) = 0.1⁶ / 720 ≈ 1.3888889e-9.
    // max(|x|, |T_5|) = max(1, 0.9048...) = 1.
    // denom    = atol + rtol·1 = 1e-12 + 1e-10 = 1.01e-10.
    // ε        = |δT| / denom ≈ 1.3888889e-9 / 1.01e-10 ≈ 13.7513741.
    TEST(ErrorControl, L2FormulaMatchesManual) {
        Fixture f;
        const double eps = ErrorEstimate(f.table, f.x0, 0.1, 5, 1, f.opts);
        EXPECT_NEAR(eps, 13.7513741, 1e-5);
    }

    // --- §2.1.4: убывание ε(h) с ростом M при фиксированном h --------------------
    //
    // При фиксированном h и K=1 остаток δT ≈ c_{M+1}·h^{M+1}, поэтому
    // с ростом M норма строго убывает (знаменатель меняется слабо).
    TEST(ErrorControl, DecreasesWithOrder) {
        Fixture f;
        const double eps_m5 = ErrorEstimate(f.table, f.x0, 0.1, 5, 1, f.opts);
        const double eps_m9 = ErrorEstimate(f.table, f.x0, 0.1, 9, 1, f.opts);
        EXPECT_GT(eps_m5, eps_m9);
        EXPECT_GT(eps_m9, 0.0);
    }

    // --- §2.1.4: при rtol = 0 норма вырождается в RMS(δT)/atol -------------------
    //
    // Если rtol = 0, знаменатель равен atol для всех i, и
    //   ε(h)·atol = sqrt( (1/n)·Σ_i δT_i² ) = RMS(δT).
    // Для n = 1 это просто |δT|. Проверка размерности.
    TEST(ErrorControl, RtolZeroGivesRmsOfResidual) {
        Fixture f;
        f.opts.rtol = 0.0;

        const double eps = ErrorEstimate(f.table, f.x0, 0.1, 5, 1, f.opts);
        const auto   delta = f.table.DiffPoly(0.1, 5, 1);
        ASSERT_EQ(delta.size(), 1u);

        const double lhs = eps * f.opts.atol;
        const double rhs = std::abs(delta[0]);
        EXPECT_NEAR(lhs, rhs, std::abs(rhs) * 1e-12 + 1e-30);
    }

    // --- §2.1.4: RMS, а не max ---------------------------------------------------
    //
    // Двумерная система x' = y, y' = -x (гармонический осциллятор),
    // x(0) = 1, y(0) = 0. Точное решение: x = cos t, y = -sin t.
    // Ряды Тейлора:
    //   x: 1 - t²/2 + t⁴/24 - t⁶/720 + ...   (чётные степени)
    //   y: -t + t³/6 - t⁵/120 + ...          (нечётные степени)
    // При M = 5, K = 1:
    //   δT_x = -h⁶/720 ≠ 0,
    //   δT_y = 0.
    // Значит term_y = 0, |term_x| > 0, и
    //   RMS = |term_x| / √2 < |term_x| = max_i |term_i|.
    // Если бы реализация осталась на max-норме, тест бы упал.
    TEST(ErrorControl, UsesRmsNormNotMax) {
        const RawSystem sys =
            ParseSystem("x' = y\ny' = -x\nx(0) = 1\ny(0) = 0\n");
        const TaylorSpec spec = BuildTaylorSpec(sys);
        const std::vector<double> x0{ 1.0, 0.0 };
        TaylorTable table(spec, x0, 8);

        SolveOptions opts;
        opts.rtol = 1e-10;
        opts.atol = 1e-12;

        const double h = 0.1;
        const std::size_t M = 5;
        const std::size_t K = 1;

        const double eps = ErrorEstimate(table, x0, h, M, K, opts);
        EXPECT_GT(eps, 0.0);
        EXPECT_FALSE(std::isinf(eps));
        EXPECT_FALSE(std::isnan(eps));

        // Считаем оба варианта нормы рядом и убеждаемся, что функция
        // вернула именно RMS.
        const auto delta = table.DiffPoly(h, M, K);
        const auto taylor = table.Evaluate(h, M);
        ASSERT_EQ(delta.size(), x0.size());
        ASSERT_EQ(taylor.size(), x0.size());

        double sum_sq = 0.0;
        double max_val = 0.0;
        for (std::size_t i = 0; i < delta.size(); ++i) {
            const double scale = std::max(std::abs(x0[i]),
                std::abs(taylor[i]));
            const double denom = opts.atol + opts.rtol * scale;
            const double term = delta[i] / denom;
            sum_sq += term * term;
            max_val = std::max(max_val, std::abs(term));
        }
        const double rms = std::sqrt(sum_sq / static_cast<double>(delta.size()));

        EXPECT_NEAR(eps, rms, rms * 1e-12 + 1e-300);

        // При n ≥ 2 нормы различаются, если компоненты дают разные |term_i|.
        // Здесь term_y = 0, term_x ≠ 0 — различие строгое.
        EXPECT_GT(max_val, 0.0);
        EXPECT_LT(rms, max_val);
    }

    // =========================================================================
// Дополнительное покрытие: знак, n ≥ 3, монотонность по rtol,
// fallback на |T_M| при вырожденном |x|.
// =========================================================================

// --- Знак состояния не влияет на ε -----------------------------------------
//
// Все под модулем: |x|, |T_M|, δT². Для x' = -x при x(0) = 1 и x(0) = -1
// δT отличается только знаком, term² один и тот же ⇒ ε одинаков.
// Ловит забытый std::abs в знаменателе.
    TEST(ErrorControl, SignOfStateDoesNotMatter) {
        const auto sys = ParseSystem("x' = -x\nx(0) = 1\n");
        const auto spec = BuildTaylorSpec(sys);

        TaylorTable t_pos(spec, { 1.0 }, 8);
        TaylorTable t_neg(spec, { -1.0 }, 8);

        SolveOptions opts;
        opts.rtol = 1e-10;
        opts.atol = 1e-12;

        const double eps_pos = ErrorEstimate(t_pos, { 1.0 }, 0.1, 5, 1, opts);
        const double eps_neg = ErrorEstimate(t_neg, { -1.0 }, 0.1, 5, 1, opts);

        EXPECT_GT(eps_pos, 0.0);
        EXPECT_DOUBLE_EQ(eps_pos, eps_neg);
    }

    // --- n = 3: нормировка 1/n ---------------------------------------------------
    //
    // Три независимые копии x' = -x. Все компоненты дают одинаковый term,
    // поэтому RMS = sqrt(3·term² / 3) = |term| — то же, что в одномерном случае.
    // Если реализация забыла 1/n, получится √3·|term| — тест упадёт.
    TEST(ErrorControl, RmsNormThreeDimensionalDecoupled) {
        const auto sys3 = ParseSystem(
            "x' = -x\ny' = -y\nz' = -z\nx(0)=1\ny(0)=1\nz(0)=1\n");
        const auto spec3 = BuildTaylorSpec(sys3);
        const std::vector<double> x0_3{ 1.0, 1.0, 1.0 };
        TaylorTable table3(spec3, x0_3, 8);

        SolveOptions opts;
        opts.rtol = 1e-10;
        opts.atol = 1e-12;

        const double eps_3d = ErrorEstimate(table3, x0_3, 0.1, 5, 1, opts);

        // Одномерный аналог с теми же опциями.
        Fixture f;
        const double eps_1d = ErrorEstimate(f.table, f.x0, 0.1, 5, 1, opts);

        EXPECT_GT(eps_1d, 0.0);
        EXPECT_NEAR(eps_3d, eps_1d, eps_1d * 1e-12 + 1e-300);
    }

    // --- Монотонность по rtol ----------------------------------------------------
    //
    // Знаменатель = atol + rtol·scale. При фиксированных x, T_M, h и δT
    // рост rtol увеличивает знаменатель ⇒ ε убывает. Ловит перепутанный
    // знак или забытый множитель при rtol.
    TEST(ErrorControl, RtolMonotonicity) {
        Fixture f;
        f.opts.atol = 1e-12;

        f.opts.rtol = 1e-8;
        const double eps_loose = ErrorEstimate(f.table, f.x0, 0.1, 5, 1, f.opts);

        f.opts.rtol = 1e-10;
        const double eps_mid = ErrorEstimate(f.table, f.x0, 0.1, 5, 1, f.opts);

        f.opts.rtol = 1e-12;
        const double eps_tight = ErrorEstimate(f.table, f.x0, 0.1, 5, 1, f.opts);

        EXPECT_GT(eps_loose, 0.0);
        EXPECT_LT(eps_loose, eps_mid);
        EXPECT_LT(eps_mid, eps_tight);
    }

    // --- Fallback на |T_M| при вырожденном |x| ----------------------------------
    //
    // x' = -x, x(0)=1. Таблица построена для x0=1, но в ErrorEstimate
    // передаётся крошечный x = 1e-20.
    //   Правильно (с max):  denom = atol + rtol·|T_5(0.1)| ≈ 9.15e-11
    //                       ε ≈ 1.39e-9 / 9.15e-11 ≈ 15.
    //   Неправильно (без max): denom = atol + rtol·1e-20 ≈ 1e-12
    //                       ε ≈ 1.39e-9 / 1e-12 ≈ 1389.
    // Тест с порогом 100 отсекает второе.
    TEST(ErrorControl, ZeroStateFallsBackToTaylorMagnitude) {
        Fixture f;
        const std::vector<double> x_tiny{ 1e-20 };
        const double eps = ErrorEstimate(f.table, x_tiny, 0.1, 5, 1, f.opts);
        EXPECT_GT(eps, 0.0);
        EXPECT_LT(eps, 100.0);
    }

}  // namespace diffuri