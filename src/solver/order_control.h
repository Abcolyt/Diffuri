// ============================================================================
// src/solver/order_control.h
//
// Модуль order_control: выбор порядка M.
//
// Реализует §2.1.6 (выбор M на первом шаге) и §2.3 (переключение M по
// триггеру |h/H| > m) из статьи [Бабаджанянц, Большаков 2012].
// §2.1.5 (градуировка t(p)) выполняется в solver.cpp и передаётся сюда
// через параметр t_p.
//
// Зависимости:
//   order_control -> taylor_table   (TaylorTable)
//   order_control -> taylor_spec    (TaylorSpec)
//   order_control -> step_control   (PickStep)
//   order_control -> solver         (SolveOptions)
//
// MVP-поведение (при enable_order_adaptation = false):
//   - всегда возвращает M_current;
//   - если M_current == 0, возвращает литерал 20.
//
// При включённой адаптации:
//   - на первом шаге (is_first_step = true) перебирает все p ∈ [M_min, M_max],
//     вычисляет V(p) = h(p)/t(p), выбирает argmax V(p);
//   - на последующих шагах проверяет триггер |h/H| > m_factor; если сработал —
//     спускается от M-1 до M_min, затем поднимается от M+1 до M_max, ищет
//     первое p с V(p) >= V(M).
// ============================================================================
#pragma once

#include <cstddef>
#include <vector>

#include "solver/solver.h"
#include "solver/taylor_spec.h"
#include "solver/taylor_table.h"

namespace diffuri {

    /**
     * @brief Результат работы алгоритма выбора порядка.
     *
     * @field M   Новый порядок метода.
     * @field h   Новый шаг (вычисленный под новый M). 0.0 означает
     *            "не вычислен, Solve должен вызвать PickStep самостоятельно"
     *            (используется при отключённой адаптации).
     * @field H   Новый якорь для триггера §2.3. Равен h при смене M или на
     *            первом шаге; равен старому H, если M не изменился.
     */
    struct OrderDecision {
        std::size_t M;
        double h;
        double H;
    };

    /**
     * @brief Выбрать порядок M и шаг h для следующего шага интегрирования.
     *
     * @param spec          Спецификация системы.
     * @param table         Таблица Тейлора для текущей точки (порядка M + K).
     * @param M             Текущий порядок метода.
     * @param h             Текущий шаг (с предыдущего шага интегрирования).
     * @param H             Шаг на момент последней смены M (якорь для §2.3).
     * @param is_first_step true если это первый шаг интегрирования (включает §2.1.6).
     * @param opts          Опции интегрирования.
     * @param t_p           Массив процессорного времени t(p) для p ∈ [M_min, M_max].
     *                      Индексация: t_p[p - M_min]. Пустой вектор означает
     *                      отключённую адаптацию.
     */
    [[nodiscard]] OrderDecision PickOrder(const TaylorSpec& spec,
        const TaylorTable& table,
        std::size_t M,
        double h,
        double H,
        bool is_first_step,
        const SolveOptions& opts,
        const std::vector<double>& t_p);

} // namespace diffuri