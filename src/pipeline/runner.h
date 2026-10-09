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
//   runner -> order_reducer       (ReduceOrder)
//   runner -> autonomize          (Autonomize)
//   runner -> polynomization      (Polynomize)
//   runner -> quadratize          (Quadratize)
//   runner -> solver              (Solve, Solution)
//
// Отвечает за:
//   - единый вход в пайплайн (RunPipeline);
//   - последовательный вызов этапов в правильном порядке;
//   - фиксацию состояния после каждого этапа в PipelineTrace;
//   - сохранение результата Solve в RunResult.
//
// Что модуль НЕ делает:
//   - не форматирует результаты (это output);
//   - не разбирает аргументы CLI (это cli);
//   - не реализует сами этапы (это отдельные модули).
//
// Пайплайн (ТЗ №4.1): Parsed → Validated → Normalized → OrderReduced →
// Polynomized → Solved. Autonomize и Quadratize модифицируют систему
// in-place между этапами, но отдельных стадий в PipelineTrace не имеют.
// ============================================================================
#pragma once

// --- Стандартная библиотека ---
#include <string>

// --- Внутренние зависимости ---
#include "pipeline/trace.h"
#include "solver/solver.h"

namespace diffuri {

    // ============================================================================
    // 1. ОПЦИИ И КОНФИГУРАЦИЯ
    // ============================================================================
    // (В этом модуле нет структур опций; используются SolveOptions из solver.h)

    // ============================================================================
    // 2. СТРУКТУРЫ ДАННЫХ
    // ============================================================================

    /**
     * @struct RunResult
     * @brief Всё, что пайплайн произвёл: история состояний по стадиям
     *        плюс результат численного интегрирования.
     *
     * Move-only (внутри PipelineTrace с unique_ptr).
     */
    struct RunResult {
        PipelineTrace trace = {};    ///< История состояний по стадиям.
        Solution      solution = {}; ///< Траектория, полученная Solver'ом.
    };

    // ============================================================================
    // 3. ИСКЛЮЧЕНИЯ
    // ============================================================================
    // (В этом модуле исключения не объявляются; пробрасываются из других модулей:
    //  ParseError, InputError, NormalizeError, OrderReducerError,
    //  AutonomizeError, PolynomizeError, QuadratizeError, SolverError)

    // ============================================================================
    // 4. ПУБЛИЧНЫЙ API (свободные функции)
    // ============================================================================

    /**
     * @brief Прогнать текст через пайплайн с дефолтными опциями Solver'а.
     *
     * Эквивалентно RunPipeline(text, SolveOptions{}).
     *
     * @param text Текст системы ОДУ.
     * @return     Результат прогона пайплайна.
     * @throws     Исключения этапов (см. RunPipeline с opts).
     */
    [[nodiscard]] RunResult RunPipeline(const std::string& text);

    /**
     * @brief Прогнать текст через пайплайн с явными опциями Solver'а.
     *
     * Заполняются стадии Parsed, Validated, Normalized, OrderReduced,
     * Polynomized, Solved. Ошибки этапов не заворачиваются: вызывающий
     * получает исходное исключение и может понять, где именно сломалось.
     *
     * @param text Текст системы ОДУ.
     * @param opts Параметры интегрирования.
     * @return     Результат прогона пайплайна.
     * @throws ParseError, InputError, NormalizeError, OrderReducerError,
     *         AutonomizeError, PolynomizeError, QuadratizeError, SolverError
     *         в зависимости от того, на каком этапе упало.
     */
    [[nodiscard]] RunResult RunPipeline(const std::string& text,
        const SolveOptions& opts);

} // namespace diffuri