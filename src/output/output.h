// ============================================================================
// src/output/output.h
//
// Модуль output: форматирование и вывод результатов пайплайна.
//
// Зависимости:
//   output -> pipeline/runner   (RunResult)
//   output -> pipeline/trace    (PipelineTrace, Stage)
//   output -> solver/solver     (Solution)
//
// Отвечает за:
//   - чистое представление финального решения (SolvedView):
//     переименование вспомогательных переменных OrderReducer в производные,
//     скрытие внутренних хелперов Polynomize;
//   - человекочитаемый отчёт по стадиям пайплайна для CLI и логов;
//   - запись отчёта в файл (лог).
//
// Что модуль НЕ делает:
//   - не запускает пайплайн (это runner);
//   - не решает систему (это solver);
//   - не зависит от CLI.
// ============================================================================
#pragma once

#include <cstddef>
#include <iosfwd>
#include <string>
#include <vector>

#include "pipeline/runner.h"
#include "pipeline/trace.h"
#include "solver/solver.h"

namespace diffuri {

    // ============================================================================
    // ЧИСТОЕ ПРЕДСТАВЛЕНИЕ РЕШЕНИЯ
    // ============================================================================

    /**
     * @struct SolvedView
     * @brief Финальная точка траектории без вспомогательных переменных,
     *        с переименованиями (x_1 → x', v_1 → скрыт).
     *
     * names[i] — отображаемое имя (совпадает с исходным, если переменная
     *            не была вспомогательной).
     * values[i] — значение в финальной точке.
     * hidden_count — сколько переменных Solution::functions скрыто
     *                (внутренние хелперы Polynomize / Quadratize).
     */
    struct SolvedView {
        double                   t_final = 0.0;
        std::size_t              steps = 0;
        std::size_t              order_used = 0;
        std::size_t              hidden_count = 0;
        std::vector<std::string> names;
        std::vector<double>      values;
    };

    /**
     * @brief Построить «чистое» представление решения.
     *
     * Использует метаданные PipelineTrace на стадиях OrderReduced и Polynomized:
     *   - OrderReduced: x_1 = Derivative{x, 1}  → "x_1" отображается как "x'";
     *   - Polynomized:  v_1 = sin(x)            → переменная скрывается.
     *
     * Если вспомогательных нет — результат совпадает с сырым Solution.
     */
    [[nodiscard]] SolvedView MakeSolvedView(const Solution& sol,
        const PipelineTrace& trace);

    // ============================================================================
    // ФОРМАТИРОВАНИЕ РЕШЕНИЯ
    // ============================================================================

    /**
     * @brief Одна строка отчёта по финальному решению (для CLI).
     *
     * Формат:
     *   [Solved]
     *   # steps: <steps>
     *   # order: <order_used>
     *   # t_final: <t_final>
     *   <name>(<t_final>) = <value>
     *   ...
     *   # auxiliary hidden: <N>        (только если N > 0)
     */
    [[nodiscard]] std::string FormatSolved(const SolvedView& view);
    [[nodiscard]] std::string FormatSolved(const Solution& sol,
        const PipelineTrace& trace);

    /// Печать в произвольный поток (удобно для stdout / file).
    void WriteSolved(const Solution& sol, const PipelineTrace& trace,
        std::ostream& os);

    // ============================================================================
    // ПОЛНЫЙ ОТЧЁТ ПО ПАЙПЛАЙНУ
    // ============================================================================

    /**
     * @struct ReportOptions
     * @brief Что включать в полный отчёт.
     */
    struct ReportOptions {
        bool include_input_header = true;   ///< Заголовок с числом стадий.
        bool include_trace = true;   ///< Все Format(stage) по порядку.
        bool include_clean_views = false;  ///< View(stage) для стадий с aux.
        bool include_solved = true;   ///< FormatSolved в конце.
    };

    /**
     * @brief Полный отчёт: стадии пайплайна + финальное решение.
     *
     * Пример структуры:
     *   ==== Diffuri pipeline report ====
     *   --- stages ---
     *     * Parsed
     *     * Validated
     *     ...
     *   [Parsed]
     *   ...
     *   [Solved]
     *   # steps: ...
     *   ...
     */
    [[nodiscard]] std::string FormatReport(const RunResult& result,
        const ReportOptions& opts = {});

    /// Записать отчёт в файл. Бросает std::runtime_error при ошибке открытия.
    void WriteReportToFile(const RunResult& result,
        const std::string& path,
        const ReportOptions& opts = {});

} // namespace diffuri