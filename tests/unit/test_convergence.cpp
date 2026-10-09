// ============================================================================
// tests/unit/test_convergence.cpp
//
// Тесты модуля convergence: масштабирующие множители, радиус сходимости,
// обратные функции остатков ряда u⁻¹, v⁻¹, априорный шаг τ = ComputeTau.
//
// Обязательный property-тест: u(CalculateInverseU(t)) ≈ t на сетке tolerances.
// ============================================================================
#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

#include "solver/convergence.h"
#include "solver/taylor_spec.h"

namespace diffuri {
    namespace {

        TaylorSpec MakeSpec(const char* text) {
            RawSystem s = ParseSystem(text);
            return BuildTaylorSpec(s);
        }

        // Локальные копии u(τ), v(τ) для property-тестов.
        // Должны совпадать с реализацией в convergence.cpp.
        double U(double tau, std::size_t M = 20) {
            double term = 1.0;
            for (std::size_t m = 1; m <= M + 1; ++m) {
                term *= tau / static_cast<double>(m);
            }
            double sum = 0.0;
            for (std::size_t m = M + 1; m <= M + 100; ++m) {
                sum += term;
                term *= tau / static_cast<double>(m + 1);
            }
            return sum;
        }

        // Эталонная v(τ) = b(τ) − T_M b(τ), b(τ) = (1−τ)^(−1/L).
        // Совпадает по формуле с SeriesRemainderB в convergence.cpp.
        double V(double tau, std::size_t M, std::size_t L) {
            if (tau <= 0.0) return 0.0;
            if (L == 1) {
                // Замкнутая форма: v(τ) = τ^(M+1) / (1 − τ).
                double tau_pow = 1.0;
                for (std::size_t m = 0; m <= M; ++m) tau_pow *= tau;
                return tau_pow / (1.0 - tau);
            }
            // L > 1: прямое суммирование хвоста.
            const double inv_L = 1.0 / static_cast<double>(L);
            double c = 1.0;
            double tau_pow = 1.0;
            for (std::size_t m = 1; m <= M + 1; ++m) {
                c *= (inv_L + static_cast<double>(m - 1)) /
                    static_cast<double>(m);
                tau_pow *= tau;
            }
            double term = c * tau_pow;
            double sum = term;
            for (std::size_t m = M + 2; m <= M + 500; ++m) {
                c *= (inv_L + static_cast<double>(m - 1)) /
                    static_cast<double>(m);
                tau_pow *= tau;
                term = c * tau_pow;
                sum += term;
                if (std::abs(term) < 1e-300 * std::abs(sum)) break;
            }
            return sum;
        }

        // Синхронизировать с kMaxSafeTau в convergence.cpp.
        constexpr double kMaxSafeTau = 1.0 - 1e-12;

        // --- CalculateScalingMultipliers ----------------------------------------------------

        TEST(Convergence, ScalingMultipliersZeroVector) {
            auto alpha = CalculateScalingMultipliers({ 0.0, 0.0, 0.0 });
            ASSERT_EQ(alpha.size(), 3u);
            for (double a : alpha) EXPECT_DOUBLE_EQ(a, 1.0);
        }

        TEST(Convergence, ScalingMultipliersBelowOne) {
            auto alpha = CalculateScalingMultipliers({ 0.1, -0.5, 0.0 });
            ASSERT_EQ(alpha.size(), 3u);
            EXPECT_DOUBLE_EQ(alpha[0], 1.0);
            EXPECT_DOUBLE_EQ(alpha[1], 1.0);
            EXPECT_DOUBLE_EQ(alpha[2], 1.0);
        }

        TEST(Convergence, ScalingMultipliersAboveOne) {
            auto alpha = CalculateScalingMultipliers({ 3.0, -2.0, 0.5 });
            ASSERT_EQ(alpha.size(), 3u);
            EXPECT_DOUBLE_EQ(alpha[0], 3.0);
            EXPECT_DOUBLE_EQ(alpha[1], 2.0);
            EXPECT_DOUBLE_EQ(alpha[2], 1.0);
        }

        // --- CalculateConvergenceRadius: линейный случай ------------------------------------

        TEST(Convergence, RadiusLinearXEqMinusX) {
            // x' = -x: s(α=1) = 1, ρ = 1.
            TaylorSpec spec = MakeSpec("x' = -x\nx(0) = 1\n");
            auto alpha = CalculateScalingMultipliers({ 1.0 });
            EXPECT_DOUBLE_EQ(CalculateConvergenceRadius(spec, alpha), 1.0);
        }

        TEST(Convergence, RadiusLinearXEqMinus2X) {
            // x' = -2x: s(α=1) = 2, ρ = 0.5.
            TaylorSpec spec = MakeSpec("x' = -2*x\nx(0) = 1\n");
            auto alpha = CalculateScalingMultipliers({ 1.0 });
            EXPECT_DOUBLE_EQ(CalculateConvergenceRadius(spec, alpha), 0.5);
        }

        TEST(Convergence, RadiusLinearConstantTermIgnored) {
            // Свободный член в линейном случае не входит в s_i(α).
            TaylorSpec s1 = MakeSpec("x' = -x\nx(0) = 1\n");
            TaylorSpec s2 = MakeSpec("x' = 1 - x\nx(0) = 1\n");
            auto alpha = CalculateScalingMultipliers({ 1.0 });
            EXPECT_DOUBLE_EQ(CalculateConvergenceRadius(s1, alpha),
                CalculateConvergenceRadius(s2, alpha));
        }

        TEST(Convergence, RadiusLinearInvariantUnderAlphaScaling) {
            // s_i(c·α) = s_i(α) ⇒ ρ не зависит от общего масштаба α.
            TaylorSpec spec = MakeSpec("x' = -3*x\nx(0) = 1\n");
            const std::vector<double> a1{ 1.0 };
            const std::vector<double> a2{ 10.0 };
            EXPECT_DOUBLE_EQ(CalculateConvergenceRadius(spec, a1),
                CalculateConvergenceRadius(spec, a2));
        }

        // --- CalculateConvergenceRadius: нелинейный случай ----------------------------------

        TEST(Convergence, RadiusNonlinearXEqXSquared) {
            // x' = x^2: s(α=1) = 1, L = max_deg − 1 = 1, ρ = 1.
            TaylorSpec spec = MakeSpec("x' = x^2\nx(0) = 1\n");
            auto alpha = CalculateScalingMultipliers({ 1.0 });
            EXPECT_DOUBLE_EQ(CalculateConvergenceRadius(spec, alpha), 1.0);
        }

        TEST(Convergence, RadiusNonlinearScalesWithAlpha) {
            // x' = x^2, α = 2: s(α) = 2, L = 1, ρ = 0.5.
            TaylorSpec spec = MakeSpec("x' = x^2\nx(0) = 1\n");
            const std::vector<double> alpha{ 2.0 };
            EXPECT_DOUBLE_EQ(CalculateConvergenceRadius(spec, alpha), 0.5);
        }

        // --- CalculateConvergenceRadius: нулевая система ------------------------------------

        TEST(Convergence, RadiusZeroSystemIsInfinity) {
            TaylorSpec spec = MakeSpec("x' = 0\nx(0) = 1\n");
            auto alpha = CalculateScalingMultipliers({ 1.0 });
            EXPECT_TRUE(std::isinf(CalculateConvergenceRadius(spec, alpha)));
        }

        // --- CalculateInverseU --------------------------------------------------------------

        TEST(Convergence, InverseUZeroOrNegativeTolerance) {
            EXPECT_DOUBLE_EQ(CalculateInverseU(0.0, 20), 0.0);
            EXPECT_DOUBLE_EQ(CalculateInverseU(-1.0, 20), 0.0);
        }

        TEST(Convergence, InverseUSaturatesAtOne) {
            // u(1) ≈ 2e-20; при tolerance ≥ u(1) возвращается τ = 1.
            EXPECT_DOUBLE_EQ(CalculateInverseU(1.0, 20), 1.0);
            EXPECT_DOUBLE_EQ(CalculateInverseU(1e-10, 20), 1.0);
        }

        TEST(Convergence, InverseUMonotonic) {
            double prev = -1.0;
            for (double t : { 1e-30, 1e-27, 1e-25, 1e-23, 1e-22 }) {
                const double tau = CalculateInverseU(t, 20);
                EXPECT_GT(tau, prev);
                prev = tau;
            }
        }

        TEST(Convergence, InverseUProperty) {
            // Обязательный property: u(InverseU(t)) ≈ t.
            for (double t : { 1e-30, 1e-27, 1e-25, 1e-23, 5e-22 }) {
                const double tau = CalculateInverseU(t, 20);
                ASSERT_GE(tau, 0.0);
                ASSERT_LE(tau, 1.0);
                EXPECT_NEAR(U(tau, 20), t, t * 1e-6);
            }
        }

        // --- CalculateInverseV --------------------------------------------------------------

        TEST(Convergence, InverseVZeroOrNegativeTolerance) {
            EXPECT_DOUBLE_EQ(CalculateInverseV(0.0, 20, 1), 0.0);
            EXPECT_DOUBLE_EQ(CalculateInverseV(-1.0, 20, 1), 0.0);
        }

        TEST(Convergence, InverseVSmallTolerance) {
            // v(τ) = τ^21 / (1 − τ); при малых τ, τ ≈ tol^(1/21).
            const double tau = CalculateInverseV(1e-30, 20, 1);
            EXPECT_GT(tau, 0.0);
            EXPECT_LT(tau, 0.5);
            EXPECT_NEAR(V(tau, 20, 1), 1e-30, 1e-36);
        }

        TEST(Convergence, InverseVSaturatesAtOne) {
            // Для очень больших tolerance возвращается τ ≈ 1.
            EXPECT_NEAR(CalculateInverseV(1e10, 20, 1), 1.0, 1e-9);
        }

        TEST(Convergence, InverseVMonotonic) {
            double prev = -1.0;
            for (double t : { 1e-30, 1e-20, 1e-10, 1.0, 1e5 }) {
                const double tau = CalculateInverseV(t, 20, 1);
                EXPECT_GT(tau, prev);
                prev = tau;
            }
        }

        TEST(Convergence, InverseVProperty) {
            // v(CalculateInverseV(t)) ≈ t на сетке tolerances.
            for (double t : { 1e-30, 1e-25, 1e-20, 1e-15, 1e-10, 1e-5 }) {
                const double tau = CalculateInverseV(t, 20, 1);
                ASSERT_GE(tau, 0.0);
                ASSERT_LT(tau, 1.0);
                EXPECT_NEAR(V(tau, 20, 1), t, t * 1e-6);
            }
        }

        // =========================================================================
        // Дополнительное покрытие: многомерные системы, смешанные случаи,
        // инварианты CalculateScalingMultipliers, защита от NaN на границах.
        // =========================================================================

        // --- CalculateScalingMultipliers: инварианты ---------------------------------------

        TEST(Convergence, ScalingMultipliersSizeMatchesInput) {
            for (std::size_t n : { 1u, 2u, 5u }) {
                std::vector<double> x(n, 1.0);
                EXPECT_EQ(CalculateScalingMultipliers(x).size(), n);
            }
        }

        TEST(Convergence, ScalingMultipliersNeverBelowOne) {
            const std::vector<std::vector<double>> inputs{
                { 0.0 },
                { 0.0, 0.0, 0.0 },
                { 1e-10, -1e-10 },
                { 1e10, -1e10 },
            };
            for (const auto& x : inputs) {
                auto alpha = CalculateScalingMultipliers(x);
                ASSERT_EQ(alpha.size(), x.size());
                for (double a : alpha) EXPECT_GE(a, 1.0);
            }
        }

        // --- CalculateConvergenceRadius: многомерные линейные ------------------------------

        TEST(Convergence, RadiusLinearHarmonicOscillator) {
            // x' = y, y' = -x; A = [[0,1],[-1,0]], α = (1,1)
            //   s_1 = |1·1| = 1, s_2 = |1·1| = 1, s = 1, ρ = 1.
            TaylorSpec spec = MakeSpec("x' = y\ny' = -x\nx(0) = 1\ny(0) = 0\n");
            auto alpha = CalculateScalingMultipliers({ 1.0, 1.0 });
            EXPECT_DOUBLE_EQ(CalculateConvergenceRadius(spec, alpha), 1.0);
        }

        TEST(Convergence, RadiusLinearNonUniformAlpha) {
            // Тот же осциллятор, α = (2, 1):
            //   s_1 = (1/2)·|1·1| = 0.5
            //   s_2 = (1/1)·|1·2| = 2
            //   s = 2, ρ = 0.5.
            // Ловит перепутывание α_i и α_j.
            TaylorSpec spec = MakeSpec("x' = y\ny' = -x\nx(0) = 1\ny(0) = 0\n");
            const std::vector<double> alpha{ 2.0, 1.0 };
            EXPECT_DOUBLE_EQ(CalculateConvergenceRadius(spec, alpha), 0.5);
        }

        TEST(Convergence, RadiusLinearLargeCoefficient) {
            // x' = -1000·x: s = 1000, ρ = 0.001.
            TaylorSpec spec = MakeSpec("x' = -1000*x\nx(0) = 1\n");
            auto alpha = CalculateScalingMultipliers({ 1.0 });
            EXPECT_DOUBLE_EQ(CalculateConvergenceRadius(spec, alpha), 0.001);
        }

        // --- CalculateConvergenceRadius: смешанные и многомерные нелинейные ----------------

        TEST(Convergence, RadiusMixedLinearNonlinear) {
            // x' = -x + x^2; α = 1:
            //   s = |−1| + |1|·1^2 = 2, L = 1, ρ = 0.5.
            TaylorSpec spec = MakeSpec("x' = -x + x^2\nx(0) = 1\n");
            auto alpha = CalculateScalingMultipliers({ 1.0 });
            EXPECT_DOUBLE_EQ(CalculateConvergenceRadius(spec, alpha), 0.5);
        }

        TEST(Convergence, Radius2DNonlinear) {
            // x' = y, y' = x^2; α = 1:
            //   s_1 = 1 (только y), s_2 = 1 (только x^2), s = 1, L = 1, ρ = 1.
            TaylorSpec spec = MakeSpec("x' = y\ny' = x^2\nx(0) = 0\ny(0) = 1\n");
            auto alpha = CalculateScalingMultipliers({ 1.0, 1.0 });
            EXPECT_DOUBLE_EQ(CalculateConvergenceRadius(spec, alpha), 1.0);
        }

        // --- CalculateInverseU / CalculateInverseV: защита от NaN и границы ------------------------

        TEST(Convergence, InverseUNoNaN) {
            for (double t : { 0.0, 1e-200, 1e-30, 1e-20, 1e-10, 1.0, 1e10 }) {
                const double tau = CalculateInverseU(t, 20);
                EXPECT_FALSE(std::isnan(tau));
                EXPECT_GE(tau, 0.0);
                EXPECT_LE(tau, 1.0);
            }
        }

        TEST(Convergence, InverseVNoNaN) {
            for (double t : { 0.0, 1e-200, 1e-30, 1e-10, 1.0, 1e10 }) {
                const double tau = CalculateInverseV(t, 20, 1);
                EXPECT_FALSE(std::isnan(tau));
                EXPECT_GE(tau, 0.0);
                EXPECT_LE(tau, 1.0);
            }
        }

        TEST(Convergence, InverseUTinyToleranceNearZero) {
            // Эталон получен независимо через scipy.optimize.brentq
            // при M = 20, u(τ) = e^τ − T_20 e^τ.
            constexpr double kTol = 1e-30;
            constexpr double kExpectedTau = 0.3233070419894332;

            const double tau = CalculateInverseU(kTol, 20);
            EXPECT_NEAR(tau, kExpectedTau, 1e-12);
        }

        // =========================================================================
        // Дополнительное покрытие 2: знак, постоянный член, экстремальные α,
        // билинейные мономы, плотный property-тест.
        // =========================================================================

        TEST(Convergence, RadiusLinearSignDoesNotMatter) {
            // Модуль в формуле: x' = x и x' = -x дают одинаковый ρ.
            TaylorSpec s_pos = MakeSpec("x' = x\nx(0) = 1\n");
            TaylorSpec s_neg = MakeSpec("x' = -x\nx(0) = 1\n");
            auto alpha = CalculateScalingMultipliers({ 1.0 });
            EXPECT_DOUBLE_EQ(CalculateConvergenceRadius(s_pos, alpha),
                CalculateConvergenceRadius(s_neg, alpha));
        }

        TEST(Convergence, RadiusNonlinearSignDoesNotMatter) {
            // То же для нелинейного: x' = x^2 и x' = -x^2.
            TaylorSpec s_pos = MakeSpec("x' = x^2\nx(0) = 1\n");
            TaylorSpec s_neg = MakeSpec("x' = -x^2\nx(0) = 1\n");
            auto alpha = CalculateScalingMultipliers({ 1.0 });
            EXPECT_DOUBLE_EQ(CalculateConvergenceRadius(s_pos, alpha),
                CalculateConvergenceRadius(s_neg, alpha));
        }

        TEST(Convergence, RadiusNonlinearConstantTermEntersS) {
            // В нелинейном случае (§1.3.2.2) |a_j| входит в s_j.
            // x' = 2 + x^2, α = 1: s = |2| + |1| = 3, L = 1, ρ = 1/3.
            TaylorSpec spec = MakeSpec("x' = 2 + x^2\nx(0) = 1\n");
            auto alpha = CalculateScalingMultipliers({ 1.0 });
            EXPECT_DOUBLE_EQ(CalculateConvergenceRadius(spec, alpha), 1.0 / 3.0);
        }

        TEST(Convergence, RadiusConstantOnlySystemIsInfinity) {
            // x' = 5: система линейна, линейных коэффициентов нет,
            // s = 0 ⇒ ρ = +∞ (решение — полином, сходится всюду).
            TaylorSpec spec = MakeSpec("x' = 5\nx(0) = 0\n");
            auto alpha = CalculateScalingMultipliers({ 1.0 });
            EXPECT_TRUE(std::isinf(CalculateConvergenceRadius(spec, alpha)));
        }

        TEST(Convergence, RadiusExtremeAlphaNoOverflow) {
            // α = 1e100, x' = x^2: s = α = 1e100, ρ = 1e-100 — без overflow.
            TaylorSpec spec = MakeSpec("x' = x^2\nx(0) = 1\n");
            const std::vector<double> alpha{ 1e100 };
            const double rho = CalculateConvergenceRadius(spec, alpha);
            EXPECT_FALSE(std::isnan(rho));
            EXPECT_FALSE(std::isinf(rho));
            EXPECT_GT(rho, 0.0);
            EXPECT_LT(rho, 1e-50);
        }

        TEST(Convergence, RadiusAsymmetricBilinear) {
            // x' = x*y, y' = -x; α = (2, 3).
            //   mon_alpha[x*y] = 2·3 = 6
            //   s_1 = α_1^{-1} · |1| · 6 = 6/2 = 3
            //   s_2 = α_2^{-1} · |1| · 2 = 2/3
            //   s = 3, L = 1, ρ = 1/3.
            // Ловит ошибку «перепутаны α_p и α_q» в mon_alpha.
            TaylorSpec spec = MakeSpec("x' = x*y\ny' = -x\nx(0)=1\ny(0)=1\n");
            const std::vector<double> alpha{ 2.0, 3.0 };
            EXPECT_DOUBLE_EQ(CalculateConvergenceRadius(spec, alpha), 1.0 / 3.0);
        }

        TEST(Convergence, InverseUDenseProperty) {
            // Плотная сетка tolerances: u(InverseU(t)) ≈ t.
            // Медленно растёт от u(1e-40) до u(1e-22); 30 точек.
            for (int k = 40; k >= 22; --k) {
                const double t = std::pow(10.0, -k);
                const double tau = CalculateInverseU(t, 20);
                ASSERT_GE(tau, 0.0);
                ASSERT_LE(tau, 1.0);
                EXPECT_NEAR(U(tau, 20), t, t * 1e-6) << "tolerance = " << t;
            }
        }

        // =========================================================================
        // Задача 2.1: параметризация InverseU порядком M.
        // =========================================================================

        // --- 1. Регрессия: при M = 20 результат детерминирован --------------------
        TEST(ConvergenceInverseUParam, RegressionM20IsDeterministic) {
            for (double tol : { 1e-25, 1e-30, 1e-40 }) {
                EXPECT_DOUBLE_EQ(CalculateInverseU(tol, 20), CalculateInverseU(tol, 20));
            }
        }

        // --- 2. M реально участвует в вычислении ----------------------------------
        TEST(ConvergenceInverseUParam, MDiffers) {
            const double tol = 1e-40;
            const double t10 = CalculateInverseU(tol, 10);
            const double t20 = CalculateInverseU(tol, 20);
            const double t30 = CalculateInverseU(tol, 30);

            EXPECT_GT(t10, 0.0); EXPECT_LT(t10, 1.0);
            EXPECT_GT(t20, 0.0); EXPECT_LT(t20, 1.0);
            EXPECT_GT(t30, 0.0); EXPECT_LT(t30, 1.0);

            EXPECT_NE(t10, t20);
            EXPECT_NE(t20, t30);
            EXPECT_NE(t10, t30);
        }

        // --- 3. Монотонность по M -------------------------------------------------
        // При M₁ < M₂: CalculateInverseU(tol, M₁) < CalculateInverseU(tol, M₂) — см. комментарий
        // в реализационном плане (противоположно букве ТЗ §2.1, тест 3).
        TEST(ConvergenceInverseUParam, MonotoneInM) {
            const double tol = 1e-40;
            const double t10 = CalculateInverseU(tol, 10);
            const double t20 = CalculateInverseU(tol, 20);
            const double t30 = CalculateInverseU(tol, 30);

            EXPECT_LT(t10, t20);
            EXPECT_LT(t20, t30);
        }

        // --- 4. Границы -----------------------------------------------------------
        TEST(ConvergenceInverseUParam, Bounds) {
            EXPECT_DOUBLE_EQ(CalculateInverseU(0.0, 20), 0.0);
            EXPECT_DOUBLE_EQ(CalculateInverseU(-1.0, 20), 0.0);
            EXPECT_DOUBLE_EQ(CalculateInverseU(1.0, 20), 1.0);
            EXPECT_DOUBLE_EQ(CalculateInverseU(1e10, 20), 1.0);
        }

        // =========================================================================
        // Задача 2.2: параметризация CalculateInverseV порядком M и степенью L.
        // =========================================================================

        TEST(ConvergenceInverseVParam, RegressionM20L1IsDeterministic) {
            for (double tol : { 1e-30, 1e-20, 1e-10, 1e-3 }) {
                EXPECT_DOUBLE_EQ(CalculateInverseV(tol, 20, 1),
                    CalculateInverseV(tol, 20, 1));
            }
        }

        TEST(ConvergenceInverseVParam, LDiffers) {
            const double tol = 1e-6;
            const double tL1 = CalculateInverseV(tol, 20, 1);
            const double tL2 = CalculateInverseV(tol, 20, 2);
            const double tL3 = CalculateInverseV(tol, 20, 3);

            EXPECT_GT(tL1, 0.0); EXPECT_LT(tL1, 1.0);
            EXPECT_GT(tL2, 0.0); EXPECT_LT(tL2, 1.0);
            EXPECT_GT(tL3, 0.0); EXPECT_LT(tL3, 1.0);

            EXPECT_NE(tL1, tL2);
            EXPECT_NE(tL2, tL3);
            EXPECT_NE(tL1, tL3);
        }

        TEST(ConvergenceInverseVParam, MDiffers) {
            const double tol = 1e-6;
            const double t5 = CalculateInverseV(tol, 5, 1);
            const double t30 = CalculateInverseV(tol, 30, 1);

            EXPECT_GT(t5, 0.0);  EXPECT_LT(t5, 1.0);
            EXPECT_GT(t30, 0.0); EXPECT_LT(t30, 1.0);
            EXPECT_NE(t5, t30);
            EXPECT_LT(t5, t30);
        }

        TEST(ConvergenceInverseVParam, Bounds) {
            EXPECT_DOUBLE_EQ(CalculateInverseV(0.0, 20, 1), 0.0);
            EXPECT_DOUBLE_EQ(CalculateInverseV(-1.0, 20, 2), 0.0);

            // v(τ) → ∞ только при τ → 1⁻. Для L = 1, M = 20:
            //   v(nextafter(1, 0)) ≈ 1 / (1 − nextafter(1, 0)) ≈ 4.5e15.
            // Значит saturation к τ = 1 достигается для tol > ~4.5e15.
            // tol = 1e20 заведомо больше ⇒ CalculateInverseV возвращает ровно 1.
            EXPECT_DOUBLE_EQ(CalculateInverseV(1e20, 20, 1), 1.0);
            EXPECT_DOUBLE_EQ(CalculateInverseV(1e20, 20, 2), 1.0);
            EXPECT_DOUBLE_EQ(CalculateInverseV(1e20, 20, 3), 1.0);

            // А при умеренных tol (например, 1.0) решение лежит строго в (0, 1):
            const double t_mid = CalculateInverseV(1.0, 20, 1);
            EXPECT_GT(t_mid, 0.0);
            EXPECT_LT(t_mid, 1.0);
        }

        TEST(ConvergenceInverseVParam, PropertyL2) {
            for (double t : { 1e-20, 1e-15, 1e-10, 1e-7, 1e-5 }) {
                const double tau = CalculateInverseV(t, 20, 2);
                ASSERT_GE(tau, 0.0);
                ASSERT_LT(tau, 1.0);
                EXPECT_NEAR(V(tau, 20, 2), t, t * 1e-6)
                    << "tolerance = " << t;
            }
        }

        TEST(ConvergenceInverseVParam, MonotoneInTolerance) {
            double prev = -1.0;
            for (double t : { 1e-30, 1e-20, 1e-10, 1.0, 1e5 }) {
                const double tau = CalculateInverseV(t, 20, 2);
                EXPECT_GT(tau, prev) << "t = " << t;
                prev = tau;
            }
        }

        // =========================================================================
        // Задача 2.3: CalculateTau — априорный безразмерный шаг §2.1.2.
        // =========================================================================

        // --- 1. Линейная система: τ ∈ (0, 1) -----------------------------------
        // ВНИМАНИЕ к параметрам. При M = 20 u(1, 20) ≈ 2.1e-20, поэтому
        // любой rtol ≥ 2.1e-20 упирается в верхнюю границу. Чтобы получить
        // τ строго в (0, 1), берём M = 5: u(1, 5) ≈ 1.4e-3 > 1e-6.
        TEST(ConvergenceComputeTau, LinearTauInOpenInterval) {
            TaylorSpec spec = MakeSpec("x' = -x\nx(0) = 1\n");
            const std::vector<double> x{ 1.0 };
            const std::vector<double> alpha{ 1.0 };
            const double rtol = 1e-6;
            const std::size_t M = 5;

            const double tau = CalculateTau(spec, x, alpha, rtol, M);
            EXPECT_GT(tau, 0.0);
            EXPECT_LT(tau, 1.0);
        }

        // --- 2. Совпадение с CalculateInverseU (бит-в-бит) ------------------------------
        // CalculateTau для линейной системы — тонкая обёртка над InverseU,
        // никакой арифметики сверху кроме клампа в kMaxSafeTau. При rtol ниже
        // u_M(1) кламп не срабатывает ⇒ точное совпадение с InverseU.
        //   u_3(1) ≈ 5.2e-2, u_5(1) ≈ 1.4e-3, u_10(1) ≈ 2.5e-8.
        // rtol = 1e-15 / 1e-20 / 1e-25 меньше всех трёх.
        TEST(ConvergenceComputeTau, LinearMatchesInverseUBitwise) {
            TaylorSpec spec = MakeSpec("x' = -x\nx(0) = 1\n");
            const std::vector<double> x{ 1.0 };
            const std::vector<double> alpha{ 1.0 };

            for (std::size_t M : { 3u, 5u, 10u }) {
                for (double rtol : { 1e-15, 1e-20, 1e-25 }) {
                    EXPECT_DOUBLE_EQ(
                        CalculateTau(spec, x, alpha, rtol, M),
                        CalculateInverseU(rtol, M))
                        << "M = " << M << ", rtol = " << rtol;
                }
            }
        }

        // --- 3. Нелинейная система: τ ∈ (0, 1) ---------------------------------
        TEST(ConvergenceComputeTau, NonlinearTauInOpenInterval) {
            TaylorSpec spec = MakeSpec("x' = x^2\nx(0) = 1\n");
            const std::vector<double> x{ 1.0 };
            const std::vector<double> alpha{ 1.0 };
            const double rtol = 1e-6;
            const std::size_t M = 20;

            const double tau = CalculateTau(spec, x, alpha, rtol, M);
            EXPECT_GT(tau, 0.0);
            EXPECT_LT(tau, 1.0);
        }

        // --- 4. Нелинейная: совпадение с CalculateInverseV с L = max_deg − 1 ------------
        TEST(ConvergenceComputeTau, NonlinearMatchesInverseVWithDerivedL) {
            // x' = x^2 → max_deg = 2, L = 1.
            TaylorSpec spec = MakeSpec("x' = x^2\nx(0) = 1\n");
            const std::vector<double> x{ 1.0 };
            const std::vector<double> alpha{ 1.0 };

            for (std::size_t M : { 5u, 20u }) {
                for (double rtol : { 1e-6, 1e-10, 1e-20 }) {
                    EXPECT_DOUBLE_EQ(
                        CalculateTau(spec, x, alpha, rtol, M),
                        CalculateInverseV(rtol, M, 1))
                        << "M = " << M << ", rtol = " << rtol;
                }
            }
        }

        // --- 5. Монотонность по rtol -------------------------------------------
        TEST(ConvergenceComputeTau, MonotoneInRtol) {
            const std::vector<double> x{ 1.0 };
            const std::vector<double> alpha{ 1.0 };
            const std::size_t M = 20;

            // Линейная.
            {
                TaylorSpec spec = MakeSpec("x' = -x\nx(0) = 1\n");
                double prev = -1.0;
                for (double rtol : { 1e-40, 1e-30, 1e-20, 1e-10, 1.0 }) {
                    const double tau = CalculateTau(spec, x, alpha, rtol, M);
                    EXPECT_GE(tau, prev) << "линейная, rtol = " << rtol;
                    prev = tau;
                }
            }
            // Нелинейная.
            {
                TaylorSpec spec = MakeSpec("x' = x^2\nx(0) = 1\n");
                double prev = -1.0;
                for (double rtol : { 1e-40, 1e-30, 1e-20, 1e-10, 1e-6 }) {
                    const double tau = CalculateTau(spec, x, alpha, rtol, M);
                    EXPECT_GE(tau, prev) << "нелинейная, rtol = " << rtol;
                    prev = tau;
                }
            }
        }

        // --- 6. Границы: rtol = 0 → τ = 0; большой rtol → τ = kMaxSafeTau ------
        TEST(ConvergenceComputeTau, Bounds) {
            const std::vector<double> x{ 1.0 };
            const std::vector<double> alpha{ 1.0 };

            // Линейная.
            {
                TaylorSpec spec = MakeSpec("x' = -x\nx(0) = 1\n");
                EXPECT_DOUBLE_EQ(
                    CalculateTau(spec, x, alpha, 0.0, 20), 0.0);
                EXPECT_DOUBLE_EQ(
                    CalculateTau(spec, x, alpha, 1e10, 20), kMaxSafeTau);
            }
            // Нелинейная.
            {
                TaylorSpec spec = MakeSpec("x' = x^2\nx(0) = 1\n");
                EXPECT_DOUBLE_EQ(
                    CalculateTau(spec, x, alpha, 0.0, 20), 0.0);
                EXPECT_DOUBLE_EQ(
                    CalculateTau(spec, x, alpha, 1e20, 20), kMaxSafeTau);
            }
        }

        // --- 7. x и alpha игнорируются (документированное поведение) ----------
        TEST(ConvergenceComputeTau, IgnoresXAndAlpha) {
            TaylorSpec spec = MakeSpec("x' = -x\nx(0) = 1\n");

            const double baseline = CalculateTau(
                spec, { 1.0 }, { 1.0 }, 1e-25, 20);

            EXPECT_DOUBLE_EQ(CalculateTau(
                spec, { 0.0 }, { 1.0 }, 1e-25, 20), baseline);
            EXPECT_DOUBLE_EQ(CalculateTau(
                spec, { 1e10 }, { 1e-10 }, 1e-25, 20), baseline);
            EXPECT_DOUBLE_EQ(CalculateTau(
                spec, { -5.0, 100.0 }, { 2.0, 3.0 }, 1e-25, 20),
                baseline);
        }

    }  // namespace
}  // namespace diffuri