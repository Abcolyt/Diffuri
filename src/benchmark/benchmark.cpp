// ============================================================================
// src/benchmark/benchmark.cpp
//
// Реализация модуля benchmark.
//
// Структура файла:
//   1. Анонимный namespace: локальные утилиты (парсинг, интерполяция, метрики).
//   2. Реализация исключений (отсутствует).
//   3. Реализация публичных функций (ParseSweepSpec, ParseSweepMetric,
//      RunSweep, WriteSweepCsv).
// ============================================================================
#include "benchmark/benchmark.h"

// --- Стандартная библиотека (по алфавиту) ---
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

// --- Внутренние зависимости (по алфавиту) ---
#include "output/output.h"
#include "pipeline/runner.h"

namespace diffuri {

    // ============================================================================
    // 1. АНОНИМНЫЙ NAMESPACE: локальные утилиты
    // ============================================================================
    namespace {

        // Вычислить L2-норму вектора.
        double CalculateL2Norm(const std::vector<double>& vec) {
            double sum_sq = 0.0;
            for (double v : vec) {
                sum_sq += v * v;
            }
            return std::sqrt(sum_sq);
        }

        // Линейная интерполяция эталонной траектории в точке t.
        // Возвращает вектор значений только для visible_indices.
        std::vector<double> InterpolateRef(const Solution& ref_sol,
            double t,
            const std::vector<std::size_t>& visible_indices) {
            if (ref_sol.points.empty()) {
                return std::vector<double>(visible_indices.size(), 0.0);
            }
            if (t <= ref_sol.points.front().t) {
                std::vector<double> res;
                res.reserve(visible_indices.size());
                for (std::size_t idx : visible_indices) {
                    res.push_back(ref_sol.points.front().x[idx]);
                }
                return res;
            }
            if (t >= ref_sol.points.back().t) {
                std::vector<double> res;
                res.reserve(visible_indices.size());
                for (std::size_t idx : visible_indices) {
                    res.push_back(ref_sol.points.back().x[idx]);
                }
                return res;
            }

            // Поиск интервала [t_i, t_{i+1}], содержащего t
            auto it = std::lower_bound(ref_sol.points.begin(), ref_sol.points.end(), t,
                [](const SolvePoint& pt, double val) { return pt.t < val; });

            auto it_high = it;
            auto it_low = it - 1;

            double t_low = it_low->t;
            double t_high = it_high->t;
            double alpha = (t - t_low) / (t_high - t_low);

            std::vector<double> res;
            res.reserve(visible_indices.size());
            for (std::size_t idx : visible_indices) {
                double v_low = it_low->x[idx];
                double v_high = it_high->x[idx];
                res.push_back(v_low + alpha * (v_high - v_low));
            }
            return res;
        }

        // Вычислить метрику ошибки между sol и ref_sol.
        double CalculateMetric(const Solution& sol,
            const Solution& ref_sol,
            const std::vector<std::size_t>& visible_indices,
            SweepMetric metric,
            double atol) {
            if (metric == SweepMetric::TrajL2) {
                if (sol.points.size() < 2) return 0.0;

                double integral = 0.0;

                // Квадрат разности на предыдущей точке. Инициализируем нулём, но
                // к моменту первого сложения (i == 1) там уже будет значение с i == 0.
                double prev_diff_sq = 0.0;

                for (std::size_t i = 0; i < sol.points.size(); ++i) {
                    const double t = sol.points[i].t;
                    const std::vector<double> x_ref =
                        InterpolateRef(ref_sol, t, visible_indices);

                    double diff_sq = 0.0;
                    for (std::size_t j = 0; j < visible_indices.size(); ++j) {
                        const double diff =
                            sol.points[i].x[visible_indices[j]] - x_ref[j];
                        diff_sq += diff * diff;
                    }

                    if (i > 0) {
                        const double dt = sol.points[i].t - sol.points[i - 1].t;
                        integral += 0.5 * (prev_diff_sq + diff_sq) * dt;
                    }
                    prev_diff_sq = diff_sq;
                }
                return std::sqrt(integral);
            }

            // FinalAbs или FinalRel
            const auto& pt = sol.points.back();
            const auto& ref_pt = ref_sol.points.back();

            double diff_sq = 0.0;
            double ref_sq = 0.0;
            for (std::size_t idx : visible_indices) {
                double d = pt.x[idx] - ref_pt.x[idx];
                diff_sq += d * d;
                ref_sq += ref_pt.x[idx] * ref_pt.x[idx];
            }

            double final_abs = std::sqrt(diff_sq);
            if (metric == SweepMetric::FinalAbs) {
                return final_abs;
            }

            double ref_norm = std::sqrt(ref_sq);
            double denominator = std::max(atol, ref_norm);
            return final_abs / denominator;
        }

    } // namespace

    // ============================================================================
    // 2. РЕАЛИЗАЦИЯ ИСКЛЮЧЕНИЙ
    // ============================================================================
    // (Не требуется)

    // ============================================================================
    // 3. РЕАЛИЗАЦИЯ ПУБЛИЧНЫХ ФУНКЦИЙ
    // ============================================================================

    SweepSpec ParseSweepSpec(const std::string& text) {
        SweepSpec spec;
        auto eq_pos = text.find('=');
        if (eq_pos == std::string::npos) {
            throw std::runtime_error("Invalid sweep spec format: missing '='");
        }

        std::string var_str = text.substr(0, eq_pos);
        std::string range_str = text.substr(eq_pos + 1);

        if (var_str == "M") {
            spec.variable = SweepVariable::M;
        }
        else if (var_str == "rtol") {
            spec.variable = SweepVariable::Rtol;
        }
        else if (var_str == "atol") {
            spec.variable = SweepVariable::Atol;
        }
        else {
            throw std::runtime_error("Unknown sweep variable: " + var_str);
        }

        std::stringstream ss(range_str);
        std::string token;
        std::vector<double> parts;
        while (std::getline(ss, token, ':')) {
            parts.push_back(std::stod(token));
        }

        if (parts.size() != 3) {
            throw std::runtime_error("Sweep range must have exactly 3 parts: A:B:N");
        }

        double a = parts[0];
        double b = parts[1];
        std::size_t n = static_cast<std::size_t>(parts[2]);

        if (n < 2) {
            throw std::runtime_error("Number of points N must be >= 2");
        }

        spec.values.reserve(n);
        if (spec.variable == SweepVariable::M) {
            double step = (b - a) / static_cast<double>(n - 1);
            for (std::size_t i = 0; i < n; ++i) {
                spec.values.push_back(a + i * step);
            }
        }
        else {
            if (a <= 0.0 || b <= 0.0) {
                throw std::runtime_error("Logarithmic sweep bounds must be > 0");
            }
            double log_a = std::log10(a);
            double log_b = std::log10(b);
            double step = (log_b - log_a) / static_cast<double>(n - 1);
            for (std::size_t i = 0; i < n; ++i) {
                spec.values.push_back(std::pow(10.0, log_a + i * step));
            }
        }

        return spec;
    }

    SweepMetric ParseSweepMetric(const std::string& text) {
        if (text == "final_abs") return SweepMetric::FinalAbs;
        if (text == "final_rel") return SweepMetric::FinalRel;
        if (text == "traj_l2") return SweepMetric::TrajL2;
        throw std::runtime_error("Unknown sweep metric: " + text);
    }

    SweepResult RunSweep(const std::string& system_text,
        const SolveOptions& base,
        const SweepSpec& spec,
        SweepMetric metric) {
        SweepResult result;
        result.variable = spec.variable;
        result.metric = metric;

        // 1. Настройка эталонного прогона
        SolveOptions ref_opts = base;
        ref_opts.M = 60;
        ref_opts.rtol = 1e-14;
        ref_opts.atol = 1e-16;
        ref_opts.h_min = 1e-15;
        ref_opts.enable_order_adaptation = false;
        ref_opts.max_steps = 10'000'000; // Страховка для эталона

        RunResult ref_run;
        try {
            ref_run = RunPipeline(system_text, ref_opts);
            result.reference_ok = true;
        }
        catch (const std::exception& e) {
            result.reference_ok = false;
            result.points.clear();
            return result;
        }

        // 2. Определение видимых функций для метрик
        auto solved_view = MakeSolvedView(ref_run.solution, ref_run.trace);
        std::vector<std::size_t> visible_indices;
        // Находим индексы видимых функций в полном списке ref_run.solution.functions
        for (const auto& name : solved_view.names) {
            auto it = std::find(ref_run.solution.functions.begin(),
                ref_run.solution.functions.end(),
                name);
            if (it != ref_run.solution.functions.end()) {
                visible_indices.push_back(static_cast<std::size_t>(
                    std::distance(ref_run.solution.functions.begin(), it)));
            }
        }

        // 3. Цикл по значениям sweep
        for (double val : spec.values) {
            SweepPoint pt;
            pt.value = val;

            SolveOptions current_opts = base;
            if (spec.variable == SweepVariable::M) {
                current_opts.M = static_cast<std::size_t>(std::round(val));
            }
            else if (spec.variable == SweepVariable::Rtol) {
                current_opts.rtol = val;
            }
            else if (spec.variable == SweepVariable::Atol) {
                current_opts.atol = val;
            }
            // Принудительно отключаем адаптацию порядка для корректности sweep по M
            current_opts.enable_order_adaptation = false;

            try {
                RunResult current_run = RunPipeline(system_text, current_opts);
                pt.ok = true;
                pt.steps = current_run.solution.steps;
                pt.order_used = current_run.solution.order_used;
                pt.error = CalculateMetric(current_run.solution,
                    ref_run.solution,
                    visible_indices,
                    metric,
                    current_opts.atol);
            }
            catch (const std::exception& e) {
                pt.ok = false;
                pt.error = std::numeric_limits<double>::quiet_NaN();
                pt.steps = 0;
                pt.order_used = 0;
                pt.status = e.what();
            }

            result.points.push_back(pt);
        }

        return result;
    }

    void WriteSweepCsv(const SweepResult& r, const std::string& path) {
        std::ofstream f(path);
        if (!f) {
            throw std::runtime_error("Cannot open file for writing: " + path);
        }

        f << std::setprecision(17);

        f << "# Diffuri sweep\n";
        f << "# variable: ";
        if (r.variable == SweepVariable::M) f << "M\n";
        else if (r.variable == SweepVariable::Rtol) f << "rtol\n";
        else f << "atol\n";

        f << "# metric: ";
        if (r.metric == SweepMetric::FinalAbs) f << "final_abs\n";
        else if (r.metric == SweepMetric::FinalRel) f << "final_rel\n";
        else f << "traj_l2\n";

        f << "# reference_ok: " << (r.reference_ok ? "true" : "false") << "\n";

        f << "value,error,steps,order_used,ok,status\n";

        for (const auto& pt : r.points) {
            f << pt.value << ",";
            if (std::isnan(pt.error)) {
                f << "nan,";
            }
            else {
                f << pt.error << ",";
            }
            f << pt.steps << ","
                << pt.order_used << ","
                << (pt.ok ? "true" : "false") << ",";

            if (!pt.ok) {
                // Экранирование кавычек в статусе, если они есть
                std::string safe_status = pt.status;
                std::replace(safe_status.begin(), safe_status.end(), '"', '\'');
                f << "\"" << safe_status << "\"";
            }
            f << "\n";
        }

        if (!f) {
            throw std::runtime_error("Write failed for: " + path);
        }
    }

} // namespace diffuri