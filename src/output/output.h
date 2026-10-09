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

// --- Стандартная библиотека ---
#include <cstddef>
#include <iosfwd>
#include <string>
#include <vector>

// --- Внутренние зависимости ---
#include "pipeline/runner.h"
#include "pipeline/trace.h"
#include "solver/solver.h"

namespace diffuri {

    // ============================================================================
    // 1. ОПЦИИ И КОНФИГУРАЦИЯ
    // ============================================================================

    /**
     * @struct ReportOptions
     * @brief Что включать в полный отчёт.
     */
    struct ReportOptions {
        bool include_input_header = true;   ///< Заголовок с числом стадий.
        bool include_trace = true;          ///< Все Format(stage) по порядку.
        bool include_clean_views = false;   ///< View(stage) для стадий с aux.
        bool include_solved = true;         ///< FormatSolved в конце.
    };

    // ============================================================================
    // 2. СТРУКТУРЫ ДАННЫХ
    // ============================================================================

    /**
     * @struct SolvedView
     * @brief Финальная точка траектории без вспомогательных переменных,
     *        с переименованиями (x_1 → x', v_1 → скрыт).
     */
    struct SolvedView {
        double                   t_final = 0.0;      ///< Финальное время.
        std::size_t              steps = 0;          ///< Число шагов.
        std::size_t              order_used = 0;     ///< Фактически использованный порядок.
        std::size_t              hidden_count = 0;   ///< Сколько переменных Solution::functions скрыто.
        std::vector<std::string> names = {};         ///< Отображаемые имена.
        std::vector<double>      values = {};        ///< Значения в финальной точке.
    };

    // ============================================================================
    // 3. ИСКЛЮЧЕНИЯ
    // ============================================================================
    // (В этом модуле используется std::runtime_error)

    // ============================================================================
    // 4. ПУБЛИЧНЫЙ API (свободные функции)
    // ============================================================================

    /**
     * @brief Построить «чистое» представление решения.
     *
     * Использует метаданные PipelineTrace на стадиях OrderReduced и Polynomized:
     *   - OrderReduced: x_1 = Derivative{x, 1}  → "x_1" отображается как "x'";
     *   - Polynomized:  v_1 = sin(x)            → переменная скрывается.
     *
     * Если вспомогательных нет — результат совпадает с сырым Solution.
     *
     * @param sol   Результат интегрирования.
     * @param trace Трассировка пайплайна.
     * @return      Структура SolvedView с финальной точкой.
     */
    [[nodiscard]] SolvedView MakeSolvedView(const Solution& sol,
        const PipelineTrace& trace);

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
     *
     * @param view Чистое представление решения.
     * @return     Отформатированная строка.
     */
    [[nodiscard]] std::string FormatSolved(const SolvedView& view);

    /**
     * @brief Одна строка отчёта по финальному решению (обёртка с построением View).
     *
     * @param sol   Результат интегрирования.
     * @param trace Трассировка пайплайна.
     * @return      Отформатированная строка.
     */
    [[nodiscard]] std::string FormatSolved(const Solution& sol,
        const PipelineTrace& trace);

    /**
     * @brief Печать в произвольный поток (удобно для stdout / file).
     *
     * @param sol   Результат интегрирования.
     * @param trace Трассировка пайплайна.
     * @param os    Выходной поток.
     */
    void WriteSolved(const Solution& sol, const PipelineTrace& trace,
        std::ostream& os);

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
     *
     * @param result Результат прогона пайплайна.
     * @param opts   Опции форматирования отчёта.
     * @return       Отформатированный отчёт.
     */
    [[nodiscard]] std::string FormatReport(const RunResult& result,
        const ReportOptions& opts = {});

    /**
     * @brief Записать отчёт в файл.
     *
     * @param result Результат прогона пайплайна.
     * @param path   Путь к файлу.
     * @param opts   Опции форматирования отчёта.
     * @throws std::runtime_error при ошибке открытия файла.
     */
    void WriteReportToFile(const RunResult& result,
        const std::string& path,
        const ReportOptions& opts = {});

} // namespace diffuri