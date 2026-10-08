// ============================================================================
// tests/unit/test_convergence.cpp
//
// Тесты модуля convergence: масштабирующие множители, радиус сходимости,
// обратные функции остатков ряда u⁻¹, v⁻¹.
//
// Обязательный property-тест: u(InverseU(t)) ≈ t на сетке tolerances.
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
        double U(double tau) {
            constexpr std::size_t M = 20;
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

        double V(double tau) {
            constexpr std::size_t M = 20;
            double tau_pow = 1.0;
            for (std::size_t m = 0; m <= M; ++m) tau_pow *= tau;
            return tau_pow / (1.0 - tau);
        }

    }  // namespace

    // --- ScalingMultipliers ----------------------------------------------------

    TEST(Convergence, ScalingMultipliersZeroVector) {
        auto alpha = ScalingMultipliers({ 0.0, 0.0, 0.0 });
        ASSERT_EQ(alpha.size(), 3u);
        for (double a : alpha) EXPECT_DOUBLE_EQ(a, 1.0);
    }

    TEST(Convergence, ScalingMultipliersBelowOne) {
        auto alpha = ScalingMultipliers({ 0.1, -0.5, 0.0 });
        ASSERT_EQ(alpha.size(), 3u);
        EXPECT_DOUBLE_EQ(alpha[0], 1.0);
        EXPECT_DOUBLE_EQ(alpha[1], 1.0);
        EXPECT_DOUBLE_EQ(alpha[2], 1.0);
    }

    TEST(Convergence, ScalingMultipliersAboveOne) {
        auto alpha = ScalingMultipliers({ 3.0, -2.0, 0.5 });
        ASSERT_EQ(alpha.size(), 3u);
        EXPECT_DOUBLE_EQ(alpha[0], 3.0);
        EXPECT_DOUBLE_EQ(alpha[1], 2.0);
        EXPECT_DOUBLE_EQ(alpha[2], 1.0);
    }

    // --- ConvergenceRadius: линейный случай ------------------------------------

    TEST(Convergence, RadiusLinearXEqMinusX) {
        // x' = -x: s(α=1) = 1, ρ = 1.
        TaylorSpec spec = MakeSpec("x' = -x\nx(0) = 1\n");
        auto alpha = ScalingMultipliers({ 1.0 });
        EXPECT_DOUBLE_EQ(ConvergenceRadius(spec, alpha), 1.0);
    }

    TEST(Convergence, RadiusLinearXEqMinus2X) {
        // x' = -2x: s(α=1) = 2, ρ = 0.5.
        TaylorSpec spec = MakeSpec("x' = -2*x\nx(0) = 1\n");
        auto alpha = ScalingMultipliers({ 1.0 });
        EXPECT_DOUBLE_EQ(ConvergenceRadius(spec, alpha), 0.5);
    }

    TEST(Convergence, RadiusLinearConstantTermIgnored) {
        // Свободный член в линейном случае не входит в s_i(α).
        TaylorSpec s1 = MakeSpec("x' = -x\nx(0) = 1\n");
        TaylorSpec s2 = MakeSpec("x' = 1 - x\nx(0) = 1\n");
        auto alpha = ScalingMultipliers({ 1.0 });
        EXPECT_DOUBLE_EQ(ConvergenceRadius(s1, alpha),
            ConvergenceRadius(s2, alpha));
    }

    TEST(Convergence, RadiusLinearInvariantUnderAlphaScaling) {
        // s_i(c·α) = s_i(α) ⇒ ρ не зависит от общего масштаба α.
        TaylorSpec spec = MakeSpec("x' = -3*x\nx(0) = 1\n");
        const std::vector<double> a1{ 1.0 };
        const std::vector<double> a2{ 10.0 };
        EXPECT_DOUBLE_EQ(ConvergenceRadius(spec, a1),
            ConvergenceRadius(spec, a2));
    }

    // --- ConvergenceRadius: нелинейный случай ----------------------------------

    TEST(Convergence, RadiusNonlinearXEqXSquared) {
        // x' = x^2: s(α=1) = 1, L = max_deg − 1 = 1, ρ = 1.
        TaylorSpec spec = MakeSpec("x' = x^2\nx(0) = 1\n");
        auto alpha = ScalingMultipliers({ 1.0 });
        EXPECT_DOUBLE_EQ(ConvergenceRadius(spec, alpha), 1.0);
    }

    TEST(Convergence, RadiusNonlinearScalesWithAlpha) {
        // x' = x^2, α = 2: s(α) = 2, L = 1, ρ = 0.5.
        TaylorSpec spec = MakeSpec("x' = x^2\nx(0) = 1\n");
        const std::vector<double> alpha{ 2.0 };
        EXPECT_DOUBLE_EQ(ConvergenceRadius(spec, alpha), 0.5);
    }

    // --- ConvergenceRadius: нулевая система ------------------------------------

    TEST(Convergence, RadiusZeroSystemIsInfinity) {
        TaylorSpec spec = MakeSpec("x' = 0\nx(0) = 1\n");
        auto alpha = ScalingMultipliers({ 1.0 });
        EXPECT_TRUE(std::isinf(ConvergenceRadius(spec, alpha)));
    }

    // --- InverseU --------------------------------------------------------------

    TEST(Convergence, InverseUZeroOrNegativeTolerance) {
        EXPECT_DOUBLE_EQ(InverseU(0.0), 0.0);
        EXPECT_DOUBLE_EQ(InverseU(-1.0), 0.0);
    }

    TEST(Convergence, InverseUSaturatesAtOne) {
        // u(1) ≈ 2e-20; при tolerance ≥ u(1) возвращается τ = 1.
        EXPECT_DOUBLE_EQ(InverseU(1.0), 1.0);
        EXPECT_DOUBLE_EQ(InverseU(1e-10), 1.0);
    }

    TEST(Convergence, InverseUMonotonic) {
        double prev = -1.0;
        for (double t : { 1e-30, 1e-27, 1e-25, 1e-23, 1e-22 }) {
            const double tau = InverseU(t);
            EXPECT_GT(tau, prev);
            prev = tau;
        }
    }

    TEST(Convergence, InverseUProperty) {
        // Обязательный property: u(InverseU(t)) ≈ t.
        for (double t : { 1e-30, 1e-27, 1e-25, 1e-23, 5e-22 }) {
            const double tau = InverseU(t);
            ASSERT_GE(tau, 0.0);
            ASSERT_LE(tau, 1.0);
            EXPECT_NEAR(U(tau), t, t * 1e-6);
        }
    }

    // --- InverseV --------------------------------------------------------------

    TEST(Convergence, InverseVZeroOrNegativeTolerance) {
        EXPECT_DOUBLE_EQ(InverseV(0.0), 0.0);
        EXPECT_DOUBLE_EQ(InverseV(-1.0), 0.0);
    }

    TEST(Convergence, InverseVSmallTolerance) {
        // v(τ) = τ^21 / (1 − τ); при малых τ, τ ≈ tol^(1/21).
        const double tau = InverseV(1e-30);
        EXPECT_GT(tau, 0.0);
        EXPECT_LT(tau, 0.5);
        EXPECT_NEAR(V(tau), 1e-30, 1e-36);
    }

    TEST(Convergence, InverseVSaturatesAtOne) {
        // Для очень больших tolerance возвращается τ ≈ 1.
        EXPECT_NEAR(InverseV(1e10), 1.0, 1e-9);
    }

    TEST(Convergence, InverseVMonotonic) {
        double prev = -1.0;
        for (double t : { 1e-30, 1e-20, 1e-10, 1.0, 1e5 }) {
            const double tau = InverseV(t);
            EXPECT_GT(tau, prev);
            prev = tau;
        }
    }

    TEST(Convergence, InverseVProperty) {
        // v(InverseV(t)) ≈ t на сетке tolerances.
        for (double t : { 1e-30, 1e-25, 1e-20, 1e-15, 1e-10, 1e-5 }) {
            const double tau = InverseV(t);
            ASSERT_GE(tau, 0.0);
            ASSERT_LT(tau, 1.0);
            EXPECT_NEAR(V(tau), t, t * 1e-6);
        }
    }

    // =========================================================================
// Дополнительное покрытие: многомерные системы, смешанные случаи,
// инварианты ScalingMultipliers, защита от NaN на границах.
// =========================================================================

// --- ScalingMultipliers: инварианты ---------------------------------------

    TEST(Convergence, ScalingMultipliersSizeMatchesInput) {
        for (std::size_t n : { 1u, 2u, 5u }) {
            std::vector<double> x(n, 1.0);
            EXPECT_EQ(ScalingMultipliers(x).size(), n);
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
            auto alpha = ScalingMultipliers(x);
            ASSERT_EQ(alpha.size(), x.size());
            for (double a : alpha) EXPECT_GE(a, 1.0);
        }
    }

    // --- ConvergenceRadius: многомерные линейные ------------------------------

    TEST(Convergence, RadiusLinearHarmonicOscillator) {
        // x' = y, y' = -x; A = [[0,1],[-1,0]], α = (1,1)
        //   s_1 = |1·1| = 1, s_2 = |1·1| = 1, s = 1, ρ = 1.
        TaylorSpec spec = MakeSpec("x' = y\ny' = -x\nx(0) = 1\ny(0) = 0\n");
        auto alpha = ScalingMultipliers({ 1.0, 1.0 });
        EXPECT_DOUBLE_EQ(ConvergenceRadius(spec, alpha), 1.0);
    }

    TEST(Convergence, RadiusLinearNonUniformAlpha) {
        // Тот же осциллятор, α = (2, 1):
        //   s_1 = (1/2)·|1·1| = 0.5
        //   s_2 = (1/1)·|1·2| = 2
        //   s = 2, ρ = 0.5.
        // Ловит перепутывание α_i и α_j.
        TaylorSpec spec = MakeSpec("x' = y\ny' = -x\nx(0) = 1\ny(0) = 0\n");
        const std::vector<double> alpha{ 2.0, 1.0 };
        EXPECT_DOUBLE_EQ(ConvergenceRadius(spec, alpha), 0.5);
    }

    TEST(Convergence, RadiusLinearLargeCoefficient) {
        // x' = -1000·x: s = 1000, ρ = 0.001.
        TaylorSpec spec = MakeSpec("x' = -1000*x\nx(0) = 1\n");
        auto alpha = ScalingMultipliers({ 1.0 });
        EXPECT_DOUBLE_EQ(ConvergenceRadius(spec, alpha), 0.001);
    }

    // --- ConvergenceRadius: смешанные и многомерные нелинейные ----------------

    TEST(Convergence, RadiusMixedLinearNonlinear) {
        // x' = -x + x^2; α = 1:
        //   s = |−1| + |1|·1^2 = 2, L = 1, ρ = 0.5.
        TaylorSpec spec = MakeSpec("x' = -x + x^2\nx(0) = 1\n");
        auto alpha = ScalingMultipliers({ 1.0 });
        EXPECT_DOUBLE_EQ(ConvergenceRadius(spec, alpha), 0.5);
    }

    TEST(Convergence, Radius2DNonlinear) {
        // x' = y, y' = x^2; α = 1:
        //   s_1 = 1 (только y), s_2 = 1 (только x^2), s = 1, L = 1, ρ = 1.
        TaylorSpec spec = MakeSpec("x' = y\ny' = x^2\nx(0) = 0\ny(0) = 1\n");
        auto alpha = ScalingMultipliers({ 1.0, 1.0 });
        EXPECT_DOUBLE_EQ(ConvergenceRadius(spec, alpha), 1.0);
    }

    // --- InverseU / InverseV: защита от NaN и границы ------------------------

    TEST(Convergence, InverseUNoNaN) {
        for (double t : { 0.0, 1e-200, 1e-30, 1e-20, 1e-10, 1.0, 1e10 }) {
            const double tau = InverseU(t);
            EXPECT_FALSE(std::isnan(tau));
            EXPECT_GE(tau, 0.0);
            EXPECT_LE(tau, 1.0);
        }
    }

    TEST(Convergence, InverseVNoNaN) {
        for (double t : { 0.0, 1e-200, 1e-30, 1e-10, 1.0, 1e10 }) {
            const double tau = InverseV(t);
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

        const double tau = InverseU(kTol);
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
        auto alpha = ScalingMultipliers({ 1.0 });
        EXPECT_DOUBLE_EQ(ConvergenceRadius(s_pos, alpha),
            ConvergenceRadius(s_neg, alpha));
    }

    TEST(Convergence, RadiusNonlinearSignDoesNotMatter) {
        // То же для нелинейного: x' = x^2 и x' = -x^2.
        TaylorSpec s_pos = MakeSpec("x' = x^2\nx(0) = 1\n");
        TaylorSpec s_neg = MakeSpec("x' = -x^2\nx(0) = 1\n");
        auto alpha = ScalingMultipliers({ 1.0 });
        EXPECT_DOUBLE_EQ(ConvergenceRadius(s_pos, alpha),
            ConvergenceRadius(s_neg, alpha));
    }

    TEST(Convergence, RadiusNonlinearConstantTermEntersS) {
        // В нелинейном случае (§1.3.2.2) |a_j| входит в s_j.
        // x' = 2 + x^2, α = 1: s = |2| + |1| = 3, L = 1, ρ = 1/3.
        TaylorSpec spec = MakeSpec("x' = 2 + x^2\nx(0) = 1\n");
        auto alpha = ScalingMultipliers({ 1.0 });
        EXPECT_DOUBLE_EQ(ConvergenceRadius(spec, alpha), 1.0 / 3.0);
    }

    TEST(Convergence, RadiusConstantOnlySystemIsInfinity) {
        // x' = 5: система линейна, линейных коэффициентов нет,
        // s = 0 ⇒ ρ = +∞ (решение — полином, сходится всюду).
        TaylorSpec spec = MakeSpec("x' = 5\nx(0) = 0\n");
        auto alpha = ScalingMultipliers({ 1.0 });
        EXPECT_TRUE(std::isinf(ConvergenceRadius(spec, alpha)));
    }

    TEST(Convergence, RadiusExtremeAlphaNoOverflow) {
        // α = 1e100, x' = x^2: s = α = 1e100, ρ = 1e-100 — без overflow.
        TaylorSpec spec = MakeSpec("x' = x^2\nx(0) = 1\n");
        const std::vector<double> alpha{ 1e100 };
        const double rho = ConvergenceRadius(spec, alpha);
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
        EXPECT_DOUBLE_EQ(ConvergenceRadius(spec, alpha), 1.0 / 3.0);
    }

    TEST(Convergence, InverseUDenseProperty) {
        // Плотная сетка tolerances: u(InverseU(t)) ≈ t.
        // Медленно растёт от u(1e-40) до u(1e-22); 30 точек.
        for (int k = 40; k >= 22; --k) {
            const double t = std::pow(10.0, -k);
            const double tau = InverseU(t);
            ASSERT_GE(tau, 0.0);
            ASSERT_LE(tau, 1.0);
            EXPECT_NEAR(U(tau), t, t * 1e-6) << "tolerance = " << t;
        }
    }

}  // namespace diffuri