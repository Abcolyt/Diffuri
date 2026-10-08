// ============================================================================
// src/pipeline/runner.cpp
// ============================================================================
#include "pipeline/runner.h"

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <utility>

#include "autonomize/autonomize.h"
#include "input/input.h"
#include "input/parser.h"
#include "normalize/normalize.h"
#include "order_reducer/order_reducer.h"
#include "polynomization/polynomization.h"
#include "quadratize/quadratize.h"
#include "solver/solver.h"

namespace diffuri {

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
        auto aux_ro = OrderReducer(sys);
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