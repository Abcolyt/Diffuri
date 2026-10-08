// ============================================================================
// src/solver/step_control.h
//
// Модуль step_control: выбор шага h.
//
// Реализовано (см. step_control.cpp и статью
// [Бабаджанянц, Большаков 2012], «Вычислительные методы и
// программирование», т. 13, с. 497–510):
//   - §2.1.4 — апостериорный пересчёт шага по нормированной
//     локальной погрешности ErrorEstimate:
//         h_b = h · (1 / ε(h))^{1/(M+1)};
//   - ограничение роста h_new ≤ 2 · h_old (компенсация отсутствующего
//     §2.1.3 статьи);
//   - зажим результата в [opts.h_min, opts.h_max].
//
// Не реализовано в MVP и почему:
//   - §2.1.2 (априорный шаг h_a = τ · ρ) требует ρ из
//     ConvergenceRadius(spec, α); spec в сигнатуру PickStep не проброшен;
//   - §2.2 (h = max(h_a, h_b)) — следствие пропуска §2.1.2.
//
// Зависимости:
//   step_control -> error_control  (ErrorEstimate)
//   step_control -> taylor_table   (TaylorTable::X0)
//   step_control -> solver         (SolveOptions)
//
// Что модуль НЕ делает:
//   - не решает про M (это order_control);
//   - не оценивает глобальную погрешность.
// ============================================================================
#pragma once

#include <cstddef>

#include "solver/solver.h"
#include "solver/taylor_table.h"

namespace diffuri {

    /**
     * @brief Выбрать следующий шаг h.
     *
     * Реализация по §2.1.4 статьи [Бабаджанянц, Большаков 2012]:
     *   h_b = h · (1 / ε(h))^{1/(M+1)},
     *   h_new = clamp(min(h_b, 2·h), opts.h_min, opts.h_max).
     *
     * @param table Таблица Тейлора для текущей точки (x берётся как table.X0()).
     * @param h     Пробный (текущий) шаг.
     * @param M     Текущий порядок метода.
     * @param opts  Опции интегрирования (используются opts.rtol, opts.atol,
     *              opts.K, opts.h_min, opts.h_max).
     */
    [[nodiscard]] double PickStep(const TaylorTable& table,
        double h,
        std::size_t M,
        const SolveOptions& opts);

} // namespace diffuri