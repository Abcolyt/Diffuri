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
//   runner -> normalize/reduce_order (ReduceOrder)
//
// На этом этапе (ТЗ №2.5) пайплайн обрывается на Stage::OrderReduced.
// Polynomize (ТЗ №3) и Solver — следующие итерации; их стадии объявлены
// в Stage, но раннер их пока не заполняет.
// ============================================================================
#pragma once

#include <string>

#include "pipeline/trace.h"

namespace diffuri {

    /**
     * @struct RunResult
     * @brief Всё, что пайплайн произвёл: история состояний по стадиям.
     *
     * Move-only (внутри PipelineTrace с unique_ptr).
     */
    struct RunResult {
        PipelineTrace trace;
        // TODO(ТЗ №3): добавить сюда PolynomizeAuxiliary, Solution и пр.
    };

    /**
     * @brief Прогнать текст через пайплайн, фиксируя стадии в trace.
     *
     * На текущем этапе заполняются стадии Parsed, Validated, Normalized,
     * OrderReduced. Ошибки этапов не заворачиваются: вызывающий получает
     * исходное исключение и может понять, где именно сломалось.
     *
     * @throws ParseError, InputError, NormalizeError, ReduceOrderError
     *         в зависимости от того, на каком этапе упало.
     */
    [[nodiscard]] RunResult RunPipeline(const std::string& text);

} // namespace diffuri