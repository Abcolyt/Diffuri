// ============================================================================
// src/solver/solver.cpp
//
// Главный цикл метода рядов Тейлора (алгоритм 2.4 статьи, упрощённый MVP).
//
// Точки расширения для адаптации вынесены в отдельные модули:
//   - step_control::PickStep  — выбор h (в MVP — фиксированный);
//   - order_control::PickOrder — выбор M (в MVP — no-op);
//   - error_control::ErrorEstimate — оценка локальной погрешности
//     (вызывается из step_control, не из solver);
//   - convergence::*          — априорные оценки (вызываются из
//     step_control / order_control, не из solver).
//
// Замена тела любой из этих заглушек не требует правок в этом файле.
// ============================================================================
#include "solver/solver.h"

#include <algorithm>
#include <map>

#include "solver/order_control.h"
#include "solver/step_control.h"
#include "solver/taylor_spec.h"
#include "solver/taylor_table.h"

namespace diffuri {

    SolverError::SolverError(const std::string& what)
        : std::runtime_error(what) {}

    Solution Solve(const RawSystem& sys, const SolveOptions& opts) {
        // 1. Спецификация системы (один раз).
        const TaylorSpec spec = BuildTaylorSpec(sys);

        // 2. Начальное время.
        const auto t0s = CollectT0s(sys);
        if (t0s.empty()) {
            throw SolverError("Solve: no initial conditions");
        }
        if (t0s.size() > 1) {
            throw SolverError("Solve: multiple t0 values in initial conditions");
        }
        double t = *t0s.begin();
        const double t_end = opts.t_end;
        if (t_end <= t) {
            throw SolverError("Solve: t_end <= t_0");
        }

        // 3. Вектор начальных значений.
        std::vector<double> x(spec.n, 0.0);
        std::vector<bool>   have_ic(spec.n, false);
        std::map<std::string, std::size_t> findex;
        for (std::size_t j = 0; j < spec.function_names.size(); ++j) {
            findex[spec.function_names[j]] = j;
        }
        for (const auto& ic : sys.initial_conditions) {
            if (ic.order != 0) continue;
            auto it = findex.find(ic.function_name);
            if (it == findex.end()) continue;
            x[it->second] = ic.value;
            have_ic[it->second] = true;
        }
        for (std::size_t j = 0; j < spec.n; ++j) {
            if (!have_ic[j]) {
                throw SolverError("Solve: missing order-0 IC for function '" +
                    spec.function_names[j] + "'");
            }
        }

        // 4. Порядок и шаг.
        std::size_t M = (opts.M == 0) ? 20 : opts.M;
        double h = opts.h_init;
        double H = h;   // шаг на момент последней смены M (алгоритм 2.3 статьи).

        // 5. Результат.
        Solution sol;
        sol.functions = spec.function_names;
        sol.independent_variable = sys.independent_variable;
        sol.order_used = M;
        sol.points.push_back({ t, x });

        std::size_t steps = 0;
        while (t < t_end) {
            if (steps >= opts.max_steps) {
                throw SolverError("Solve: max_steps exceeded");
            }

            // 6a. Таблица Тейлора до порядка M + K.
            TaylorTable table(spec, x, M + opts.K);

            // 6b. Точка расширения: адаптация порядка.
            //     В MVP PickOrder — no-op, возвращает M без изменений.
            //     Если в итерации 3 PickOrder вернёт другое M — пересобираем
            //     таблицу под новый порядок и обновляем H (шаг на момент
            //     последней смены M), чтобы алгоритм 2.3 мог отслеживать
            //     изменение h относительно H.
            const std::size_t M_new = PickOrder(table, M, h, H, opts);
            if (M_new != M) {
                M = M_new;
                H = h;
                table = TaylorTable(spec, x, M + opts.K);
            }
            sol.order_used = M;

            // 6c. Точка расширения: адаптация шага.
            //     В MVP PickStep — фиксированный h из опций, зажатый в границы.
            //     В итерации 2 внутри PickStep появится вызов ErrorEstimate
            //     (и, при необходимости, ConvergenceRadius / InverseU/V).
            //     Вектор состояния x доступен через table.X0().
            //     solver.cpp при этом меняться не будет.
            double h_next = PickStep(table, h, M, opts);

            // 6d. Обрезка по t_end.
            if (t + h_next > t_end) h_next = t_end - t;

            // 6e. Проверка h_min.
            if (h_next < opts.h_min) {
                // Разрешаем «финальный» доворот: если остаток до t_end уже
                // меньше h_min, считаем интегрирование завершённым.
                if (t_end - t <= opts.h_min) break;
                throw SolverError("Solve: step below h_min");
            }

            // 6f. Вычисление нового состояния.
            std::vector<double> x_new = table.Evaluate(h_next, M);

            // 6g. Обновление состояния.
            t += h_next;
            x = std::move(x_new);
            h = h_next;

            // 6h. Фиксация точки.
            sol.points.push_back({ t, x });
            ++steps;
        }

        sol.steps = steps;
        sol.t_final = t;
        return sol;
    }

}  // namespace diffuri