// ============================================================================
// src/pipeline/runner.h
//
// Модуль pipeline/runner: связывает этапы пайплайна в одну цепочку,
// фиксируя состояние системы между этапами в PipelineTrace.
//
// Зависимости:
//   runner -> pipeline/trace
//   runner -> input               (ParseSystem, Validate)
//   runner -> normalize           (NormalizeSystem)
//   runner -> order_reducer       (OrderReducer)
//   runner -> autonomize          (Autonomize)
//   runner -> polynomization      (Polynomize)
//   runner -> quadratize          (Quadratize)
//   runner -> solver              (Solve, Solution)
//
// Пайплайн (ТЗ №4.1): Parsed → Validated → Normalized → OrderReduced →
// Polynomized → Solved. Autonomize и Quadratize модифицируют систему
// in-place между этапами, но отдельных стадий в PipelineTrace не имеют.
//
// Форматирование результатов вынесено в модуль output (output/output.h).
// ============================================================================
#pragma once

#include <string>

#include "pipeline/trace.h"
#include "solver/solver.h"

namespace diffuri {

    /**
     * @struct RunResult
     * @brief Всё, что пайплайн произвёл: история состояний по стадиям
     *        плюс результат численного интегрирования.
     *
     * Move-only (внутри PipelineTrace с unique_ptr).
     */
    struct RunResult {
        PipelineTrace trace;

        /// Траектория, полученная Solver'ом.
        Solution solution;
    };

    /**
     * @brief Прогнать текст через пайплайн с дефолтными опциями Solver'а.
     *
     * Эквивалентно RunPipeline(text, SolveOptions{}).
     */
    [[nodiscard]] RunResult RunPipeline(const std::string& text);

    /**
     * @brief Прогнать текст через пайплайн с явными опциями Solver'а.
     *
     * Заполняются стадии Parsed, Validated, Normalized, OrderReduced,
     * Polynomized, Solved. Ошибки этапов не заворачиваются: вызывающий
     * получает исходное исключение и может понять, где именно сломалось.
     *
     * @throws ParseError, InputError, NormalizeError, OrderReducerError,
     *         AutonomizeError, PolynomizeError, QuadratizeError, SolverError
     *         в зависимости от того, на каком этапе упало.
     */
    [[nodiscard]] RunResult RunPipeline(const std::string& text,
        const SolveOptions& opts);

} // namespace diffuri