// ============================================================================
// src/pipeline/runner.cpp
// ============================================================================
#include "pipeline/runner.h"

#include <utility>

#include "input/input.h"
#include "input/parser.h"
#include "normalize/normalize.h"
#include "polynomization/polynomization.h"
#include "order_reducer/order_reducer.h"    

namespace diffuri {

    RunResult RunPipeline(const std::string& text) {
        RunResult r;

        RawSystem sys = ParseSystem(text);
        r.trace.Capture(Stage::Parsed, sys);

        Validate(sys);
        r.trace.Capture(Stage::Validated, sys);

        NormalizeSystem(sys);
        r.trace.Capture(Stage::Normalized, sys);

        auto aux_ro = OrderReducer(sys);     // ← было ReduceOrder(sys)
        r.trace.SetAuxiliary(Stage::OrderReduced, std::move(aux_ro));
        r.trace.Capture(Stage::OrderReduced, sys);

        auto aux_p = Polynomize(sys);
        r.trace.SetAuxiliary(Stage::Polynomized, std::move(aux_p));
        r.trace.Capture(Stage::Polynomized, sys);

        return r;
    }

} // namespace diffuri