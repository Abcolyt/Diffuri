// ============================================================================
// src/solver/solver.cpp
//
// Главный цикл метода рядов Тейлора (алгоритм 2.4 статьи).
//
// Точки расширения:
//   - step_control::PickStep  — выбор h;
//   - order_control::PickOrder — выбор M (адаптивный или pass-through);
//   - error_control::ErrorEstimate — оценка локальной погрешности
//     (вызывается из step_control);
//   - convergence::*          — априорные оценки (вызываются из step_control).
//
// ТЗ №2: в главном цикле после Evaluate стоит runtime-детектор ухода
// решения в бесконечность (inf/nan).
//
// ТЗ №6: при enable_order_adaptation = true выполняется градуировка t(p)
// (§2.1.5 статьи) перед главным циклом, и PickOrder получает доступ к t_p.
// ============================================================================
#include "solver/solver.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <map>
#include <string>
#include <vector>

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

        // === САНИТАРНАЯ ОБРАБОТКА ОПЦИЙ ===
        // Защита от мусора в новых полях (если они не инициализированы
        // в тестах или при ручном создании SolveOptions). Если M_max
        // выглядит как неинициализированное огромное число, принудительно
        // отключаем адаптацию, чтобы избежать std::length_error.
        SolveOptions safe_opts = opts;
        if (safe_opts.M_max < safe_opts.M_min || safe_opts.M_max > 200) {
            safe_opts.enable_order_adaptation = false;
        }

        // 2. Начальное время.
        const auto t0s = CollectT0s(sys);
        if (t0s.empty()) {
            throw SolverError("Solve: no initial conditions");
        }
        if (t0s.size() > 1) {
            throw SolverError("Solve: multiple t0 values in initial conditions");
        }
        double t = *t0s.begin();
        const double t_end = safe_opts.t_end;
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
        std::size_t M = (safe_opts.M == 0) ? 20 : safe_opts.M;
        double h = safe_opts.h_init;
        double H = h;

        // --- §2.1.5: Градуировка t(p) ---
        // Выполняется один раз до главного цикла. Для каждого p ∈ [M_min, M_max]
        // замеряем процессорное время построения таблицы Тейлора.
        // Используем несколько прогонов для снижения шума таймера.
        std::vector<double> t_p;
        if (safe_opts.enable_order_adaptation &&
            safe_opts.M_max >= safe_opts.M_min) {

            t_p.resize(safe_opts.M_max - safe_opts.M_min + 1, 0.0);
            constexpr int kGraduationRuns = 10;

            for (std::size_t p = safe_opts.M_min; p <= safe_opts.M_max; ++p) {
                auto start = std::chrono::steady_clock::now();
                for (int i = 0; i < kGraduationRuns; ++i) {
                    TaylorTable table_grade(spec, x, p + safe_opts.K);
                }
                auto end = std::chrono::steady_clock::now();
                std::chrono::duration<double> elapsed =
                    (end - start) / static_cast<double>(kGraduationRuns);
                double measured = elapsed.count();
                t_p[p - safe_opts.M_min] = (measured > 0.0) ? measured : 1e-9;
            }
        }

        // 5. Результат.
        Solution sol;
        sol.functions = spec.function_names;
        sol.independent_variable = sys.independent_variable;
        sol.order_used = M;
        sol.points.push_back({ t, x });

        std::size_t steps = 0;
        while (t < t_end) {
            if (steps >= safe_opts.max_steps) {
                throw SolverError("Solve: max_steps exceeded");
            }

            // 6a. Таблица Тейлора до порядка M + K.
            TaylorTable table(spec, x, M + safe_opts.K);

            // 6b. Точка расширения: адаптация порядка (§2.1.6, §2.3).
            OrderDecision dec = PickOrder(
                spec, table, M, h, H, (steps == 0), safe_opts, t_p);

            bool changed = (dec.M != M);
            if (changed) {
                M = dec.M;
                // Пересобираем таблицу под новый порядок.
                table = TaylorTable(spec, x, M + safe_opts.K);
            }
            // H обновляется при смене M или на первом шаге (§2.3 статьи:
            // "H изменяется после каждого изменения величины порядка M,
            // а на первом шаге полагается равной величине первого шага").
            if (changed || steps == 0) {
                H = dec.H;
            }
            sol.order_used = M;

            // 6c. Точка расширения: адаптация шага.
            // Если PickOrder уже вычислил h под новый M — используем его.
            // Иначе (адаптация отключена) — вызываем PickStep.
            double h_next = (dec.h > 0.0) ? dec.h
                : PickStep(table, spec, h, M, safe_opts);

            // 6d. Обрезка по t_end.
            if (t + h_next > t_end) h_next = t_end - t;

            // 6e. Проверка h_min.
            if (h_next < safe_opts.h_min) {
                if (t_end - t <= safe_opts.h_min) break;
                throw SolverError("Solve: step below h_min");
            }

            // 6f. Вычисление нового состояния.
            std::vector<double> x_new = table.Evaluate(h_next, M);

            // 6f'. ТЗ №2: runtime-детектор ухода решения в бесконечность.
            const double t_new = t + h_next;
            if (!std::isfinite(t_new)) {
                throw SolverError(
                    "Solve: time is not finite at t=" +
                    std::to_string(t_new));
            }
            for (std::size_t i = 0; i < x_new.size(); ++i) {
                if (!std::isfinite(x_new[i])) {
                    throw SolverError(
                        "Solve: solution is not finite at t=" +
                        std::to_string(t_new) + " (component " +
                        std::to_string(i) + ")");
                }
            }

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