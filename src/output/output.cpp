// ============================================================================
// src/output/output.cpp
//
// Реализация форматирования и вывода результатов пайплайна.
//
// Структура файла:
//   1. Анонимный namespace: локальные утилиты (BuildAuxRenaming).
//   2. Реализация исключений (отсутствуют).
//   3. Реализация публичных функций (MakeSolvedView, FormatSolved,
//      WriteSolved, FormatReport, WriteReportToFile).
// ============================================================================
#include "output/output.h"

// --- Стандартная библиотека (по алфавиту) ---
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <map>
#include <ostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <variant>

namespace diffuri {

    // ============================================================================
    // 1. АНОНИМНЫЙ NAMESPACE: локальные утилиты
    // ============================================================================
    namespace {
        // Собрать карту переименований и множество скрытых имён
        // из метаданных PipelineTrace.
        void BuildAuxRenaming(const PipelineTrace& trace,
            std::map<std::string, std::string>& rename,
            std::set<std::string>& hide) {
            // OrderReduced: x_1 = Derivative{x,1} → переименовать в "x'".
            // Всё остальное (не производные) — скрыть.
            for (const auto& [name, def] : trace.Auxiliary(Stage::OrderReduced)) {
                if (std::holds_alternative<Derivative>(def->value)) {
                    const auto& d = std::get<Derivative>(def->value);
                    std::string disp = d.function_name;
                    for (int i = 0; i < d.order; ++i) disp += "'";
                    rename[name] = disp;
                }
                else {
                    hide.insert(name);
                }
            }
            // Polynomized: v_1 = sin(x) → скрыть.
            // Если имя уже переименовано на этапе OrderReduced — не трогаем.
            for (const auto& [name, def] : trace.Auxiliary(Stage::Polynomized)) {
                if (rename.find(name) == rename.end()) {
                    hide.insert(name);
                }
            }
            // Quadratized: q_1 = Mul(x, x) → скрыть.
            // Все переменные квадратизации — служебные, переименовывать не во что.
            for (const auto& [name, def] : trace.Auxiliary(Stage::Quadratized)) {
                if (rename.find(name) == rename.end()) {
                    hide.insert(name);
                }
            }
        }
    } // namespace

    // ============================================================================
    // 2. РЕАЛИЗАЦИЯ ИСКЛЮЧЕНИЙ
    // ============================================================================
    // (В этом модуле используется std::runtime_error)

    // ============================================================================
    // 3. РЕАЛИЗАЦИЯ ПУБЛИЧНЫХ ФУНКЦИЙ
    // ============================================================================

    SolvedView MakeSolvedView(const Solution& sol, const PipelineTrace& trace) {
        SolvedView v;
        v.t_final = sol.t_final;
        v.steps = sol.steps;
        v.order_used = sol.order_used;

        if (sol.points.empty()) return v;

        std::map<std::string, std::string> rename;
        std::set<std::string> hide;
        BuildAuxRenaming(trace, rename, hide);

        const auto& last = sol.points.back();
        const std::size_t n = std::min(sol.functions.size(), last.x.size());

        for (std::size_t i = 0; i < n; ++i) {
            const std::string& fname = sol.functions[i];
            if (hide.count(fname)) {
                ++v.hidden_count;
                continue;
            }
            auto it = rename.find(fname);
            v.names.push_back((it == rename.end()) ? fname : it->second);
            v.values.push_back(last.x[i]);
        }
        return v;
    }

    std::string FormatSolved(const SolvedView& view) {
        std::ostringstream os;
        os << "[Solved]\n";
        os << "# steps: " << view.steps << "\n";
        os << "# order: " << view.order_used << "\n";
        os << std::setprecision(15);
        os << "# t_final: " << view.t_final << "\n";

        for (std::size_t i = 0; i < view.names.size(); ++i) {
            os << view.names[i] << "(" << view.t_final << ") = "
                << view.values[i] << "\n";
        }
        if (view.hidden_count > 0) {
            os << "# auxiliary hidden: " << view.hidden_count << "\n";
        }
        return os.str();
    }

    std::string FormatSolved(const Solution& sol, const PipelineTrace& trace) {
        return FormatSolved(MakeSolvedView(sol, trace));
    }

    void WriteSolved(const Solution& sol, const PipelineTrace& trace,
        std::ostream& os) {
        os << FormatSolved(sol, trace);
    }

    std::string FormatReport(const RunResult& result, const ReportOptions& opts) {
        std::ostringstream os;
        os << "==== Diffuri pipeline report ====\n\n";

        if (opts.include_input_header) {
            os << "--- stages ---\n";
            for (auto s : result.trace.Stages()) {
                os << "  * " << ToString(s) << "\n";
            }
            os << "\n";
        }

        if (opts.include_trace) {
            for (auto s : result.trace.Stages()) {
                // На стадии Solved не печатаем Format системы — вместо
                // неё идёт отдельный блок (см. include_solved).
                if (s == Stage::Solved && opts.include_solved) continue;
                os << result.trace.Format(s) << "\n";
            }
        }

        if (opts.include_clean_views) {
            for (auto s : result.trace.Stages()) {
                if (s != Stage::OrderReduced
                    && s != Stage::Polynomized
                    && s != Stage::Quadratized
                    && s != Stage::Solved) continue;
                try {
                    auto v = result.trace.View(s);
                    os << "--- " << ToString(s) << " (clean) ---\n"
                        << ToString(v) << "\n";
                }
                catch (const std::logic_error&) {
                    // чистый вид недоступен — пропускаем молча
                }
            }
        }

        if (opts.include_solved) {
            os << FormatSolved(result.solution, result.trace);
        }

        return os.str();
    }

    void WriteReportToFile(const RunResult& result,
        const std::string& path,
        const ReportOptions& opts) {
        std::ofstream out(path, std::ios::out | std::ios::trunc);
        if (!out) {
            throw std::runtime_error("WriteReportToFile: cannot open '" + path + "'");
        }
        out << FormatReport(result, opts);
    }

} // namespace diffuri