// ============================================================================
// src/solver/step_control.cpp
//
// Реализация адаптивного выбора шага по статье
// [Бабаджанянц, Большаков 2012] «Реализация метода рядов Тейлора…».
//
// Ключевые отклонения от "буквы" статьи, продиктованные машинной арифметикой:
//
// 1. Обработка nan/inf в апостериорной оценке.
//    Если ряд расходится на пробном шаге h_old, ErrorEstimate возвращает nan.
//    По формуле статьи это дало бы posterior_step = +inf, что сломало бы max().
//    Мы обнуляем posterior_step, заставляя алгоритм опираться только на prior_step.
//
// 2. Использование prior_step как "якоря" в итеративном поиске.
//    Если max(prior, posterior) оказался за радиусом сходимости (ряд расходится),
//    алгоритм тратит десятки итераций на уменьшение шага и не успевает
//    найти границу за отведённые 20 итераций. Поскольку prior_step = τ·ρ (τ < 1)
//    гарантированно лежит внутри радиуса сходимости, мы начинаем поиск с него.
//
// 3. Допуск kEpsTolerance = 1% при поиске границы ε(h) = 1.
//    Дискретный шаг поиска d = h / 5 слишком груб. Без допуска алгоритм
//    бесконечно осциллирует между шагами, дающими ε = 1.001 и ε = 0.999.
//
// 4. Graceful fallback при несходимости за 20 итераций.
//    На сложных системах (Лоренц, Ван дер Поль) при высоких M оценка ε(h)
//    становится шумной из-за сокращения разрядов в double. Вместо исключения
//    возвращаем лучший найденный шаг (минимальное |ε - 1|).
// ============================================================================
#include "solver/step_control.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

#include "solver/convergence.h"
#include "solver/error_control.h"

namespace diffuri {

    namespace {

        // Максимальный множитель роста шага за один вызов PickStep.
        // Нужен как страховка до реализации §2.3 (переключение порядка M).
        constexpr double kMaxGrowthFactor = 2.0;

        /**
         * @brief Итеративная коррекция шага (§2.1.3 статьи).
         *
         * Ищет фактическую границу, где нормированная погрешность ε(h) ≈ 1.
         *
         * @param initial_step      Начальное приближение max(prior, posterior).
         * @param prior_step        Априорный шаг h_a = τ·ρ. Используется как
         *                          "якорь", если initial_step оказался за
         *                          радиусом сходимости (eps = nan/inf).
         */
        double IterativeCorrection(const TaylorTable& table,
            const std::vector<double>& x,
            double initial_step,
            double prior_step,
            std::size_t M,
            const SolveOptions& opts) {
            if (initial_step <= 0.0) return initial_step;

            double base_step = std::isfinite(initial_step)
                ? std::min(initial_step, opts.h_max)
                : opts.h_max;

            constexpr int kSearchDivisions = 5;
            constexpr int kMaxIterations = 20;
            constexpr double kEpsTolerance = 0.01;

            double error_norm = ErrorEstimate(table, x, base_step, M, opts.K, opts);

            if ((std::isnan(error_norm) || std::isinf(error_norm) || error_norm > 1.0)
                && prior_step > 0.0 && std::isfinite(prior_step)
                && prior_step < base_step) {
                base_step = prior_step;
                error_norm = ErrorEstimate(table, x, base_step, M, opts.K, opts);
            }

            if (std::isnan(error_norm) || std::isinf(error_norm)) {
                return opts.h_min;
            }

            if (std::abs(error_norm - 1.0) < kEpsTolerance) return base_step;

            if (base_step >= opts.h_max && error_norm < 1.0) {
                return base_step;
            }

            // Запоминаем максимальный ВАЛИДНЫЙ шаг (error_norm <= 1).
            // Это гарантия того, что принятый шаг удовлетворяет допуску,
            // даже если точная граница eps=1 не найдена за лимит итераций.
            double best_valid_step = (error_norm <= 1.0) ? base_step : 0.0;

            double search_direction = (error_norm < 1.0) ? 1.0 : -1.0;
            double increment = base_step / static_cast<double>(kSearchDivisions);
            if (increment == 0.0) return base_step;

            int iteration_index = 1;
            int total_iterations = 0;

            while (total_iterations < kMaxIterations) {
                total_iterations++;
                double probe_step = base_step + search_direction * iteration_index * increment;

                if (probe_step <= 0.0) {
                    return best_valid_step > 0.0 ? best_valid_step : opts.h_min;
                }

                bool clamped_to_max = false;
                if (probe_step > opts.h_max) {
                    probe_step = opts.h_max;
                    clamped_to_max = true;
                }

                double probe_error_norm = ErrorEstimate(table, x, probe_step, M, opts.K, opts);
                if (std::isnan(probe_error_norm)) {
                    probe_error_norm = std::numeric_limits<double>::infinity();
                }

                // Обновляем лучший валидный шаг
                if (probe_error_norm <= 1.0 && probe_step > best_valid_step) {
                    best_valid_step = probe_step;
                }

                if (std::abs(probe_error_norm - 1.0) < kEpsTolerance) {
                    return probe_step;
                }

                int error_sign = (probe_error_norm < 1.0) ? 1 : -1;

                if (search_direction * error_sign < 0.0) {
                    double rollback_factor = (search_direction > 0.0)
                        ? ((probe_error_norm >= 1.0) ? (iteration_index - 1) : iteration_index)
                        : ((probe_error_norm < 1.0) ? iteration_index : (iteration_index - 1));
                    return base_step + search_direction * rollback_factor * increment;
                }

                if (iteration_index == kSearchDivisions - 1 || clamped_to_max) {
                    base_step = probe_step;
                    error_norm = probe_error_norm;
                    if (std::abs(error_norm - 1.0) < kEpsTolerance) return base_step;
                    if (base_step >= opts.h_max && error_norm < 1.0) return base_step;

                    search_direction = (error_norm < 1.0) ? 1.0 : -1.0;
                    increment = base_step / static_cast<double>(kSearchDivisions);
                    if (increment == 0.0) return best_valid_step > 0.0 ? best_valid_step : base_step;
                    iteration_index = 1;
                }
                else {
                    iteration_index++;
                }
            }

            // Не сошлись: возвращаем максимальный валидный шаг.
            // Для хаотических систем (Лоренц) это критично — завышенный шаг
            // приводит к экспоненциальному накоплению ошибки.
            if (best_valid_step > 0.0) return best_valid_step;
            return (prior_step > 0.0 && std::isfinite(prior_step)) ? prior_step : opts.h_min;
        }

    }  // namespace

    double PickStep(const TaylorTable& table,
        const TaylorSpec& spec,
        double h_old,
        std::size_t M,
        const SolveOptions& opts) {
        if (!(h_old > 0.0)) return opts.h_min;
        if (M == 0)         return std::clamp(h_old, opts.h_min, opts.h_max);

        const std::vector<double>& x = table.X0();

        // --- §2.1.2: Априорный шаг (гарантированно внутри радиуса сходимости) ---
        const std::vector<double> alpha = ScalingMultipliers(x);
        const double rho = ConvergenceRadius(spec, alpha);

        if (std::isinf(rho)) {
            return opts.h_max;
        }

        const double tau = ComputeTau(spec, x, alpha, opts.rtol, M);
        const double prior_step = tau * rho;

        // --- §2.1.4: Апостериорный шаг (по фактической погрешности) ---
        const double error_norm = ErrorEstimate(table, x, h_old, M, opts.K, opts);

        double posterior_step;
        if (std::isnan(error_norm) || std::isinf(error_norm)) {
            // Ряд расходится на пробном шаге h_old (например, h_old >> rho).
            // Апостериорная формула теряет смысл. Отбрасываем posterior_step
            // (делаем его 0), чтобы max(prior, posterior) опирался только на
            // безопасный prior_step.
            posterior_step = 0.0;
        }
        else if (error_norm > 0.0) {
            const double inv_exponent = 1.0 / static_cast<double>(M + 1);
            posterior_step = h_old * std::pow(1.0 / error_norm, inv_exponent);
        }
        else {
            // Ошибка тождественно равна нулю (например, x' = 0). Шаг можно
            // растить бесконечно, но это будет ограничено h_max и §2.1.3.
            posterior_step = std::numeric_limits<double>::infinity();
        }

        // --- §2.2: Сборка и §2.1.3: Коррекция ---
        double initial_step = std::max(prior_step, posterior_step);

        double corrected_step = IterativeCorrection(
            table, x, initial_step, prior_step, M, opts);

        // Финальная страховка от резких скачков (пока не реализован §2.3).
        double capped_step = std::min(corrected_step, kMaxGrowthFactor * h_old);
        return std::clamp(capped_step, opts.h_min, opts.h_max);
    }

}  // namespace diffuri