// ============================================================================
// src/solver/error_control.cpp
//
// Реализация оценки локальной погрешности на шаге по норме L² из §2.1.4
// статьи [Бабаджанянц, Большаков 2012]:
//
//   ε(h) = sqrt( (1/n) · Σ_i ( δT_i(h) /
//                              (Δ + ε_rel · max(|x_i|, |T_{M,i}(h)|)) )² )
//
// где Δ = opts.atol, ε_rel = opts.rtol,
//     δT_i(h)    = T_{M+K,i}(h) − T_{M,i}(h),
//     T_{M,i}(h) — полином Тейлора порядка M,
//     n          — число исходных функций (= delta.size()).
//
// Структура файла:
//   1. Анонимный namespace: локальные утилиты (отсутствуют — вся логика
//      в теле функции).
//   2. Реализация исключений (отсутствуют).
//   3. Реализация публичных функций (ErrorEstimate).
// ============================================================================
#include "solver/error_control.h"

// --- Стандартная библиотека (по алфавиту) ---
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace diffuri {

    // ============================================================================
    // 1. АНОНИМНЫЙ NAMESPACE: локальные утилиты
    // ============================================================================
    // (В этом модуле вся логика вычисления сосредоточена в теле ErrorEstimate,
    //  вынесенных хелперов нет.)

    // ============================================================================
    // 2. РЕАЛИЗАЦИЯ ИСКЛЮЧЕНИЙ
    // ============================================================================
    // (В этом модуле исключений нет)

    // ============================================================================
    // 3. РЕАЛИЗАЦИЯ ПУБЛИЧНЫХ ФУНКЦИЙ
    // ============================================================================

    double CalculateErrorEstimate(const TaylorTable& table,
        const std::vector<double>& x,
        double h,
        std::size_t M,
        std::size_t K,
        const SolveOptions& opts) {
        const std::vector<double> delta = table.DiffPoly(h, M, K);
        const std::vector<double> taylor = table.Evaluate(h, M);

        const std::size_t n = delta.size();
        if (n == 0) {
            return 0.0;
        }

        // Защита от вырожденного случая atol = rtol = 0.
        constexpr double kMinDenom = 1e-300;

        double sum_sq = 0.0;
        for (std::size_t i = 0; i < n; ++i) {
            const double xi = (i < x.size()) ? x[i] : 0.0;
            const double ti = (i < taylor.size()) ? taylor[i] : 0.0;

            const double scale = std::max(std::abs(xi), std::abs(ti));
            const double denom =
                std::max(opts.atol + opts.rtol * scale, kMinDenom);

            const double term = delta[i] / denom;
            sum_sq += term * term;
        }

        return std::sqrt(sum_sq / static_cast<double>(n));
    }

} // namespace diffuri