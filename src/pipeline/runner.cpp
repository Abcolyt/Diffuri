// ============================================================================
// src/pipeline/runner.cpp
//
// Реализация модуля pipeline/runner: связывание этапов пайплайна.
//
// Структура файла:
//   1. Анонимный namespace: локальные утилиты (отсутствуют).
//   2. Реализация исключений (отсутствуют — исключения пробрасываются
//      из нижележащих модулей).
//   3. Реализация публичных функций (RunPipeline).
// ============================================================================
#include "pipeline/runner.h"

// --- Стандартная библиотека (по алфавиту) ---
#include <algorithm>
#include <string>
#include <utility>

// --- Внутренние зависимости (по алфавиту) ---
#include "autonomize/autonomize.h"
#include "input/input.h"
#include "normalize/normalize.h"
#include "order_reducer/order_reducer.h"
#include "polynomization/polynomization.h"
#include "quadratize/quadratize.h"
#include "solver/solver.h"

namespace diffuri {

    // ============================================================================
    // 1. АНОНИМНЫЙ NAMESPACE: локальные утилиты
    // ============================================================================
    // (В этом модуле нет локальных утилит)

    // ============================================================================
    // 2. РЕАЛИЗАЦИЯ ИСКЛЮЧЕНИЙ
    // ============================================================================
    // (В этом модуле исключения пробрасываются из нижележащих модулей)

    // ============================================================================
    // 3. РЕАЛИЗАЦИЯ ПУБЛИЧНЫХ ФУНКЦИЙ
    // ============================================================================

    RunResult RunPipeline(const std::string& text) {
        return RunPipeline(text, SolveOptions{});
    }

    RunResult RunPipeline(const std::string& text, const SolveOptions& opts) {
        RunResult r;

        // --- Parsed ------------------------------------------------------
        RawSystem sys = ParseSystem(text);
        r.trace.Capture(Stage::Parsed, sys);

        // --- Validated ---------------------------------------------------
        Validate(sys);
        r.trace.Capture(Stage::Validated, sys);

        // --- Normalized --------------------------------------------------
        NormalizeSystem(sys);
        r.trace.Capture(Stage::Normalized, sys);

        // --- OrderReduced ------------------------------------------------
        auto aux_ro = ReduceOrder(sys);
        r.trace.SetAuxiliary(Stage::OrderReduced, std::move(aux_ro));
        r.trace.Capture(Stage::OrderReduced, sys);

        // --- Autonomize (in-place, без отдельной стадии) -----------------
        // Должно идти до Polynomize: Polynomize ожидает, что система
        // автономна (иначе переменная t попадёт под FindTarget как
        // обычная Function и будет полиномизирована как неизвестная).
        Autonomize(sys);

        // --- Polynomized -------------------------------------------------
        auto aux_p = Polynomize(sys);
        r.trace.SetAuxiliary(Stage::Polynomized, std::move(aux_p));
        r.trace.Capture(Stage::Polynomized, sys);

        // --- Quadratize (in-place, без отдельной стадии) -----------------
        // После Polynomize система полиномиальна; Quadratize делает её
        // квадратичной — предупреждение для Solver.
        Quadratize(sys);

        // --- Solved ------------------------------------------------------
        r.solution = Solve(sys, opts);
        r.trace.Capture(Stage::Solved, sys);

        return r;
    }

} // namespace diffuri