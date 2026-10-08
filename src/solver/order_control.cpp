// ============================================================================
// src/solver/order_control.cpp
//
// Реализация адаптивного выбора порядка по статье
// [Бабаджанянц, Большаков 2012] «Реализация метода рядов Тейлора…»,
// Вычислительные методы и программирование, 2012, т. 13, с. 497–510.
//
// Реализованы разделы:
//   - §2.1.6 (выбор M на первом шаге);
//   - §2.3 (переключение M по триггеру |h/H| > m).
//
// §2.1.5 (градуировка t(p)) выполняется в solver.cpp; сюда t_p передаётся
// готовым массивом.
//
// Отклонения от буквы статьи:
//   - При отключённой адаптации функция ведёт себя как MVP-заглушка
//     (pass-through с правилом M == 0 → 20).
//   - Для вычисления V(p) = h(p)/t(p) используется PickStep, что соответствует
//     §2.1.3 (итеративная коррекция) + §2.2 (сборка h = max(h_a, h_b)).
//   - При поиске нового M спуск и подъём останавливаются на первом p, для
//     которого V(p) >= V(M) (а не на глобальном максимуме). Это соответствует
//     формулировке «как только окажется, что V(p) >= V(M)» в §2.3 статьи
//     и ТЗ №6 (используется >= вместо строгого >).
// ============================================================================
#include "solver/order_control.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

#include "solver/step_control.h"

namespace diffuri {

    OrderDecision PickOrder(const TaylorSpec& spec,
        const TaylorTable& table,
        std::size_t M,
        double h,
        double H,
        bool is_first_step,
        const SolveOptions& opts,
        const std::vector<double>& t_p) {

        // MVP: адаптация отключена, диапазон некорректен, или нет данных
        // градуировки — pass-through.
        if (!opts.enable_order_adaptation ||
            opts.M_max < opts.M_min ||
            t_p.empty()) {
            if (M == 0) M = 20;
            return { M, 0.0, H };
        }

        // Кламп в допустимый диапазон [M_min, M_max].
        M = std::clamp(M, opts.M_min, opts.M_max);

        // Вспомогательная функция: вычислить V(p) = h(p) / t(p).
        // Строит таблицу порядка p, вызывает PickStep, нормирует на t_p.
        auto get_V = [&](std::size_t p) -> double {
            if (p < opts.M_min || p > opts.M_max) return -1.0;

            TaylorTable table_p(spec, table.X0(), p + opts.K);
            double h_p = PickStep(table_p, spec, h, p, opts);

            std::size_t idx = p - opts.M_min;
            double t = (idx < t_p.size()) ? t_p[idx] : 1e-9;
            if (t <= 0.0) t = 1e-9;  // страховка от нулевого времени

            return h_p / t;
            };

        // --- §2.1.6: Выбор M на первом шаге ---
        // Перебираем все p ∈ [M_min, M_max], выбираем argmax V(p).
        if (is_first_step) {
            std::size_t best_M = M;
            double max_V = -1.0;

            for (std::size_t p = opts.M_min; p <= opts.M_max; ++p) {
                double V_p = get_V(p);
                if (V_p > max_V) {
                    max_V = V_p;
                    best_M = p;
                }
            }

            // Пересобираем таблицу и вычисляем шаг для выбранного M.
            TaylorTable best_table(spec, table.X0(), best_M + opts.K);
            double best_h = PickStep(best_table, spec, h, best_M, opts);

            // H на первом шаге устанавливается равным первому шагу (§2.3).
            return { best_M, best_h, best_h };
        }

        // --- §2.3: Проверка триггера ---
        if (H <= 0.0) H = h;  // страховка от неинициализированного H

        double ratio = (H > 0.0) ? (h / H) : 1.0;
        if (ratio < 1.0) ratio = 1.0 / ratio;  // абсолютное отношение

        if (ratio <= opts.m_factor) {
            // Триггер не сработал: M не меняется, вычисляем только h.
            double h_next = PickStep(table, spec, h, M, opts);
            return { M, h_next, H };
        }

        // --- §2.3: Триггер сработал, ищем новый M ---
        double V_M = get_V(M);
        std::size_t best_M = M;
        bool found = false;

        // 1. Спуск: p = M-1, M-2, ..., M_min.
         //    Безопасная форма: декремент только если p > M_min,
         //    чтобы избежать unsigned underflow при M_min == 0.
        if (M > opts.M_min) {
            std::size_t p = M - 1;
            for (;;) {
                double V_p = get_V(p);
                if (V_p >= V_M) {
                    best_M = p;
                    found = true;
                    break;
                }
                if (p == opts.M_min) break;  // дошли до границы, не декрементируем
                --p;
            }
        }

        // 2. Подъём: p = M+1, M+2, ..., M_max (если спуск ничего не нашёл).
        if (!found) {
            for (std::size_t p = M + 1; p <= opts.M_max; ++p) {
                double V_p = get_V(p);
                if (V_p >= V_M) {
                    best_M = p;
                    found = true;
                    break;
                }
            }
        }

        if (found) {
            // Нашли лучший M: пересобираем таблицу и вычисляем шаг.
            TaylorTable best_table(spec, table.X0(), best_M + opts.K);
            double best_h = PickStep(best_table, spec, h, best_M, opts);
            // H обновляется на значение нового шага (§2.3).
            return { best_M, best_h, best_h };
        }

        // Ничего не нашли: M не меняется (§2.3: "порядок не корректируется").
        double h_next = PickStep(table, spec, h, M, opts);
        return { M, h_next, H };
    }

}  // namespace diffuri