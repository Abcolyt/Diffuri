// ============================================================================
// tests/unit/test_pipeline_trace.cpp
//
// Тесты PipelineTrace: Capture/At/Has/Stages, View, Format.
// ============================================================================
#include <gtest/gtest.h>

#include <map>
#include <stdexcept>
#include <string>
#include <utility>

#include "input/input.h"
#include "input/parser.h"
#include "normalize/normalize.h"
#include "order_reducer/order_reducer.h"
#include "pipeline/trace.h"

namespace diffuri {
    namespace {

        const char* kSimpleSys =
            "x'' = -x\n"
            "x(0) = 1\n"
            "x'(0) = 0\n";

        // Ручная сборка полного трейса (Parsed..OrderReduced).
        PipelineTrace MakeFullTrace(const std::string& text) {
            PipelineTrace tr;
            RawSystem sys = ParseSystem(text);
            tr.Capture(Stage::Parsed, sys);
            Validate(sys);
            tr.Capture(Stage::Validated, sys);
            NormalizeSystem(sys);
            tr.Capture(Stage::Normalized, sys);
            auto aux = OrderReducer(sys);
            tr.SetAuxiliary(Stage::OrderReduced, std::move(aux));
            tr.Capture(Stage::OrderReduced, sys);
            return tr;
        }

    } // namespace

    // ========================================================================
    // Capture / At / Has / Stages
    // ========================================================================

    TEST(PipelineTrace, CaptureAndAtRoundTrip) {
        PipelineTrace tr;
        RawSystem sys = ParseSystem(kSimpleSys);
        const std::string before = ToString(sys);
        tr.Capture(Stage::Parsed, sys);
        EXPECT_EQ(ToString(tr.At(Stage::Parsed)), before);
    }

    TEST(PipelineTrace, AtMissingThrows) {
        PipelineTrace tr;
        EXPECT_THROW(tr.At(Stage::OrderReduced), std::out_of_range);
    }

    TEST(PipelineTrace, HasBeforeAndAfterCapture) {
        PipelineTrace tr;
        EXPECT_FALSE(tr.Has(Stage::Parsed));
        EXPECT_FALSE(tr.Has(Stage::Normalized));
        RawSystem sys = ParseSystem(kSimpleSys);
        tr.Capture(Stage::Parsed, sys);
        EXPECT_TRUE(tr.Has(Stage::Parsed));
        EXPECT_FALSE(tr.Has(Stage::Normalized));
    }

    TEST(PipelineTrace, StagesInPipelineOrder) {
        auto tr = MakeFullTrace(kSimpleSys);
        auto stages = tr.Stages();
        ASSERT_GE(stages.size(), 4u);
        EXPECT_EQ(stages[0], Stage::Parsed);
        EXPECT_EQ(stages[1], Stage::Validated);
        EXPECT_EQ(stages[2], Stage::Normalized);
        EXPECT_EQ(stages[3], Stage::OrderReduced);
    }

    // ========================================================================
    // View
    // ========================================================================

    TEST(PipelineTrace, ViewWithoutParsedThrows) {
        PipelineTrace tr;
        EXPECT_THROW(tr.View(Stage::Parsed), std::logic_error);
        EXPECT_THROW(tr.View(Stage::OrderReduced), std::logic_error);
    }

    TEST(PipelineTrace, ViewParsedEqualsAt) {
        auto tr = MakeFullTrace(kSimpleSys);
        EXPECT_EQ(ToString(tr.View(Stage::Parsed)),
            ToString(tr.At(Stage::Parsed)));
    }

    TEST(PipelineTrace, ViewNormalizedEqualsAt) {
        auto tr = MakeFullTrace(kSimpleSys);
        EXPECT_EQ(ToString(tr.View(Stage::Normalized)),
            ToString(tr.At(Stage::Normalized)));
    }

    TEST(PipelineTrace, ViewOrderReducedWithoutNormalizedThrows) {
        PipelineTrace tr;
        RawSystem sys = ParseSystem(kSimpleSys);
        tr.Capture(Stage::Parsed, sys);
        EXPECT_THROW(tr.View(Stage::OrderReduced), std::logic_error);
    }

    TEST(PipelineTrace, ViewOrderReducedEqualsNormalized) {
        auto tr = MakeFullTrace(kSimpleSys);
        EXPECT_EQ(ToString(tr.View(Stage::OrderReduced)),
            ToString(tr.At(Stage::Normalized)));
    }

    TEST(PipelineTrace, ViewOrderReducedHidesAuxiliary) {
        auto tr = MakeFullTrace(kSimpleSys);
        auto view = tr.View(Stage::OrderReduced);
        ASSERT_EQ(view.functions.size(), 1u);
        EXPECT_EQ(view.functions[0], "x");
    }

    // ========================================================================
    // Format
    // ========================================================================

    TEST(PipelineTrace, FormatHeaders) {
        auto tr = MakeFullTrace(kSimpleSys);
        EXPECT_NE(tr.Format(Stage::Parsed).find("[Parsed]"),
            std::string::npos);
        EXPECT_NE(tr.Format(Stage::Validated).find("[Validated]"),
            std::string::npos);
        EXPECT_NE(tr.Format(Stage::Normalized).find("[Normalized]"),
            std::string::npos);
        EXPECT_NE(tr.Format(Stage::OrderReduced).find("[OrderReduced]"),
            std::string::npos);
    }

    TEST(PipelineTrace, FormatAuxiliaryCommentPresent) {
        auto tr = MakeFullTrace(kSimpleSys);
        const std::string s = tr.Format(Stage::OrderReduced);
        EXPECT_NE(s.find("# Source functions: x"), std::string::npos);
        EXPECT_NE(s.find("# Auxiliary: x_1"), std::string::npos);
        EXPECT_NE(s.find("(from x')"), std::string::npos);
    }

    TEST(PipelineTrace, FormatDoesNotShowAuxAsEquation) {
        auto tr = MakeFullTrace(kSimpleSys);
        const std::string s = tr.Format(Stage::OrderReduced);
        EXPECT_EQ(s.find("x_1 = x'"), std::string::npos);
    }

    TEST(PipelineTrace, FormatCustomAuxiliaryCall) {
        // Симулируем PolynomizeAux вручную: v_1 = sin(x).
        PipelineTrace tr;
        RawSystem sys = ParseSystem(kSimpleSys);
        tr.Capture(Stage::Parsed, sys);
        Validate(sys);
        tr.Capture(Stage::Validated, sys);
        NormalizeSystem(sys);
        tr.Capture(Stage::Normalized, sys);

        std::map<std::string, ExprPtr> aux;
        aux["v_1"] = MakeCallArgs("sin", MakeFunction("x"));
        tr.SetAuxiliary(Stage::OrderReduced, std::move(aux));
        tr.Capture(Stage::OrderReduced, sys);

        const std::string s = tr.Format(Stage::OrderReduced);
        EXPECT_NE(s.find("v_1 = sin(x)"), std::string::npos);
    }

    TEST(PipelineTrace, SetAuxiliaryReplacesPrevious) {
        auto tr = MakeFullTrace(kSimpleSys);
        std::map<std::string, ExprPtr> aux;
        aux["v_1"] = MakeCallArgs("cos", MakeFunction("x"));
        tr.SetAuxiliary(Stage::OrderReduced, std::move(aux));

        const std::string s = tr.Format(Stage::OrderReduced);
        EXPECT_NE(s.find("v_1 = cos(x)"), std::string::npos);
        EXPECT_EQ(s.find("x_1 (из x')"), std::string::npos);
    }

} // namespace diffuri