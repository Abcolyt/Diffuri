// ============================================================================
// src/solver/convergence.cpp
//
// Реализация априорных оценок радиуса сходимости и обратных функций
// остатков ряда Тейлора по статье [Бабаджанянц, Большаков 2012]:
//   - §1.3.2.1: масштабирующие множители α_i и ρ(α) для линейной задачи
//     (формулы (6), (7));
//   - §1.3.2.2: ρ(α) для нелинейной полиномиальной задачи (формула (8)),
//     L = max_deg − 1;
//   - §2.1.1:   обратные функции u⁻¹, v⁻¹ (численный поиск корня);
//   - §2.1.2:   масштабирующие множители α_i (MVP-компромисс).
// ============================================================================
#include "solver/convergence.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

#include "solver/taylor_spec.h"

namespace diffuri {

    namespace {

        // Порядок усечения ряда для u(τ) и v(τ) (§2.1.1 статьи).
        // В статье M и L — параметры алгоритма; в текущей сигнатуре их нет,
        // поэтому фиксируем типичные значения. Солвер работает с
        // квадратичными системами (после Quadratize), для которых L = 1.
        constexpr std::size_t kSeriesOrder = 20;
        constexpr std::size_t kBisectIters = 60;

        // Максимальная степень монома в RHS (по spec.monomial_keys).
        // Ключ "1" — константа (степень 0); иначе степень = число '*' + 1.
        std::size_t MaxDegree(const TaylorSpec& spec) {
            std::size_t max_deg = 0;
            for (const auto& key : spec.monomial_keys) {
                if (key == "1") continue;
                std::size_t deg = 1;
                for (char c : key) {
                    if (c == '*') ++deg;
                }
                max_deg = std::max(max_deg, deg);
            }
            return max_deg;
        }

        // u(τ) = e^τ − T_M e^τ (§1.3.2.1, формула (7)).
        // Прямое суммирование хвоста ряда Σ_{m=M+1}^∞ τ^m / m!:
        // вычитание exp(τ) − T_M e^τ в double теряет всю точность.
        double SeriesRemainderExp(double tau) {
            if (tau <= 0.0) return 0.0;
            // term = τ^(M+1) / (M+1)!
            double term = 1.0;
            for (std::size_t m = 1; m <= kSeriesOrder + 1; ++m) {
                term *= tau / static_cast<double>(m);
            }
            double sum = 0.0;
            for (std::size_t m = kSeriesOrder + 1;
                m <= kSeriesOrder + 100; ++m) {
                sum += term;
                term *= tau / static_cast<double>(m + 1);
            }
            return sum;
        }

        // v(τ) = b(τ) − T_M b(τ), b(τ) = (1−τ)^(−1/L)
        // (§1.3.2.2, формула (8)). Для L = 1 (квадратичная система)
        // имеем замкнутую форму v(τ) = τ^(M+1) / (1 − τ), численно
        // устойчивую при малых τ (в отличие от вычитания b − T_M b).
        double SeriesRemainderB(double tau) {
            if (tau <= 0.0) return 0.0;
            if (tau >= 1.0) return std::numeric_limits<double>::infinity();
            // L = 1: v(τ) = τ^(M+1) / (1 − τ).
            double tau_pow = 1.0;
            for (std::size_t m = 0; m <= kSeriesOrder; ++m) {
                tau_pow *= tau;
            }
            return tau_pow / (1.0 - tau);
        }

        // Бисекция монотонно возрастающей f на [lo, hi] для f(τ) = tolerance.
        template <typename F>
        double Bisect(double tolerance, double lo, double hi, F&& f) {
            for (std::size_t i = 0; i < kBisectIters; ++i) {
                const double mid = 0.5 * (lo + hi);
                if (f(mid) < tolerance) {
                    lo = mid;
                }
                else {
                    hi = mid;
                }
            }
            return 0.5 * (lo + hi);
        }

    }  // namespace

    // --------------------------------------------------------------------------
    // ScalingMultipliers
    // --------------------------------------------------------------------------

    std::vector<double> ScalingMultipliers(const std::vector<double>& x) {
        std::vector<double> alpha(x.size());
        double max_abs = 0.0;
        for (double xi : x) {
            max_abs = std::max(max_abs, std::abs(xi));
        }
        if (max_abs == 0.0) {
            std::fill(alpha.begin(), alpha.end(), 1.0);
            return alpha;
        }
        for (std::size_t i = 0; i < x.size(); ++i) {
            alpha[i] = std::max(std::abs(x[i]), 1.0);
        }
        return alpha;
    }

    // --------------------------------------------------------------------------
    // ConvergenceRadius
    // --------------------------------------------------------------------------

    double ConvergenceRadius(const TaylorSpec& spec,
        const std::vector<double>& alpha) {
        const std::size_t n = spec.n;
        const std::size_t u = spec.u;

        // Мономиальные множители α^i, вычисляемые по схеме S.
        // Инвариант схемы: p, q < l для всех l > n (см. taylor_table.h),
        // поэтому рекурсия по возрастанию l корректна.
        std::vector<double> mon_alpha(u + 1, 1.0);
        mon_alpha[0] = 1.0;
        for (std::size_t l = 1; l <= n; ++l) {
            mon_alpha[l] = alpha[l - 1];
        }
        for (std::size_t l = n + 1; l <= u; ++l) {
            const std::size_t p = spec.scheme[l].first;
            const std::size_t q = spec.scheme[l].second;
            mon_alpha[l] = mon_alpha[p] * mon_alpha[q];
        }

        const bool is_linear = (u == n);

        // s_j(α) = α_j^{-1} · Σ_l |a_{jl}| · α^l.
        // Линейный случай (§1.3.2.1): суммируем только по линейным членам
        //   l ∈ [1 : n]; свободный член исключён.
        // Нелинейный случай (§1.3.2.2): суммируем по всем мономам,
        //   включая свободный член l = 0.
        double s_max = 0.0;
        for (std::size_t j = 0; j < n; ++j) {
            double sj = 0.0;
            for (const auto& entry : spec.a[j]) {
                const std::size_t l = entry.first;
                const double coeff = entry.second;
                if (is_linear && (l < 1 || l > n)) continue;
                sj += std::abs(coeff) * mon_alpha[l];
            }
            sj /= alpha[j];
            s_max = std::max(s_max, sj);
        }

        if (s_max == 0.0) {
            return std::numeric_limits<double>::infinity();
        }

        if (is_linear) {
            return 1.0 / s_max;
        }

        // Нелинейный случай: L = max_deg − 1.
        const std::size_t max_deg = MaxDegree(spec);
        const double L = static_cast<double>(max_deg > 0 ? max_deg - 1 : 0);
        if (L <= 0.0) {
            return std::numeric_limits<double>::infinity();
        }
        return 1.0 / (L * s_max);
    }

    // --------------------------------------------------------------------------
    // InverseU
    // --------------------------------------------------------------------------

    double InverseU(double tolerance) {
        if (tolerance <= 0.0) return 0.0;
        // u(τ) монотонно возрастает на [0, 1]; насыщение при τ = 1.
        const double u_max = SeriesRemainderExp(1.0);
        if (tolerance >= u_max) return 1.0;
        return Bisect(tolerance, 0.0, 1.0, SeriesRemainderExp);
    }

    // --------------------------------------------------------------------------
    // InverseV
    // --------------------------------------------------------------------------

    double InverseV(double tolerance) {
        if (tolerance <= 0.0) return 0.0;
        // v(τ) → ∞ при τ → 1⁻. Наибольшее представимое τ < 1 — это
        // nextafter(1, 0); если tolerance превышает v в этой точке,
        // решение в double не представимо, возвращаем 1 как границу.
        const double hi = std::nextafter(1.0, 0.0);
        if (SeriesRemainderB(hi) <= tolerance) return 1.0;
        return Bisect(tolerance, 0.0, hi, SeriesRemainderB);
    }

}  // namespace diffuri