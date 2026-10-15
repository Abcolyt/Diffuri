// ============================================================================
// tests/unit/test_benchmark.cpp
//
// Юнит-тесты модуля benchmark.
// ============================================================================
#include <gtest/gtest.h>
#include <cmath>
#include <fstream>
#include <string>

#include "benchmark/benchmark.h"
#include "solver/solver.h"

namespace diffuri {
    namespace {

        TEST(BenchmarkTest, ParseSweepSpec_Valid) {
            auto spec_m = ParseSweepSpec("M=10:20:3");
            EXPECT_EQ(spec_m.variable, SweepVariable::M);
            EXPECT_EQ(spec_m.values.size(), 3);
            EXPECT_DOUBLE_EQ(spec_m.values[0], 10.0);
            EXPECT_DOUBLE_EQ(spec_m.values[1], 15.0);
            EXPECT_DOUBLE_EQ(spec_m.values[2], 20.0);

            auto spec_rtol = ParseSweepSpec("rtol=1e-4:1e-6:3");
            EXPECT_EQ(spec_rtol.variable, SweepVariable::Rtol);
            EXPECT_EQ(spec_rtol.values.size(), 3);
            EXPECT_DOUBLE_EQ(spec_rtol.values[0], 1e-4);
            EXPECT_DOUBLE_EQ(spec_rtol.values[1], 1e-5);
            EXPECT_DOUBLE_EQ(spec_rtol.values[2], 1e-6);
        }

        TEST(BenchmarkTest, ParseSweepSpec_Invalid) {
            EXPECT_THROW(ParseSweepSpec("M=10:20"), std::runtime_error);
            EXPECT_THROW(ParseSweepSpec("X=10:20:3"), std::runtime_error);
            EXPECT_THROW(ParseSweepSpec("M=10:20:1"), std::runtime_error);
        }

        TEST(BenchmarkTest, ParseSweepMetric_Valid) {
            EXPECT_EQ(ParseSweepMetric("final_abs"), SweepMetric::FinalAbs);
            EXPECT_EQ(ParseSweepMetric("final_rel"), SweepMetric::FinalRel);
            EXPECT_EQ(ParseSweepMetric("traj_l2"), SweepMetric::TrajL2);
        }

        TEST(BenchmarkTest, ParseSweepMetric_Invalid) {
            EXPECT_THROW(ParseSweepMetric("unknown"), std::runtime_error);
        }

        TEST(BenchmarkTest, RunSweep_HarmonicOscillator) {
            std::string sys = "x'' = -x\nx(0) = 1\nx'(0) = 0";
            SolveOptions base;
            base.t_end = 3.141592653589793;
            base.M = 20;
            base.rtol = 1e-10;
            base.atol = 1e-12;

            SweepSpec spec;
            spec.variable = SweepVariable::M;
            spec.values = { 10.0, 15.0, 20.0, 25.0 };

            SweepResult res = RunSweep(sys, base, spec, SweepMetric::FinalRel);

            EXPECT_TRUE(res.reference_ok);
            EXPECT_EQ(res.points.size(), 4);

            // Ошибка должна монотонно убывать (или быть очень маленькой) с ростом M
            for (size_t i = 1; i < res.points.size(); ++i) {
                EXPECT_TRUE(res.points[i].ok);
                if (!std::isnan(res.points[i - 1].error) && !std::isnan(res.points[i].error)) {
                    EXPECT_LE(res.points[i].error, res.points[i - 1].error * 1.1); // 1.1 for floating point tolerance
                }
            }
        }

        TEST(BenchmarkTest, RunSweep_BlowupSystem) {
            // x' = x^2, x(0) = 1. Blowup at t = 1.
            std::string sys = "x' = x * x\nx(0) = 1";
            SolveOptions base;
            base.t_end = 2.0; // За пределами точки взрыва
            base.M = 20;

            SweepSpec spec;
            spec.variable = SweepVariable::M;
            spec.values = { 10.0, 20.0 };

            SweepResult res = RunSweep(sys, base, spec, SweepMetric::FinalAbs);

            EXPECT_TRUE(res.reference_ok); // Эталон может упасть или нет, но sweep должен обработать
            for (const auto& pt : res.points) {
                EXPECT_FALSE(pt.ok);
                EXPECT_TRUE(std::isnan(pt.error));
                EXPECT_FALSE(pt.status.empty());
            }
        }

        TEST(BenchmarkTest, WriteSweepCsv) {
            SweepResult res;
            res.variable = SweepVariable::M;
            res.metric = SweepMetric::FinalRel;
            res.reference_ok = true;

            SweepPoint pt;
            pt.value = 20.0;
            pt.error = 1e-12;
            pt.steps = 100;
            pt.order_used = 20;
            pt.ok = true;
            res.points.push_back(pt);

            std::string test_path = "test_sweep_output.csv";
            WriteSweepCsv(res, test_path);

            std::ifstream f(test_path);
            ASSERT_TRUE(f.is_open());

            std::string line;
            bool has_header = false;
            bool has_data = false;
            bool has_comment = false;

            while (std::getline(f, line)) {
                if (line.empty()) continue;
                if (line[0] == '#') {
                    has_comment = true;
                }
                else if (line.find("value,error,steps,order_used,ok,status") != std::string::npos) {
                    has_header = true;
                }
                else if (line.find("20,1e-12") != std::string::npos || line.find("20,1e-12,100,20,true") != std::string::npos) {
                    has_data = true;
                }
            }

            EXPECT_TRUE(has_comment);
            EXPECT_TRUE(has_header);
            EXPECT_TRUE(has_data);

            std::remove(test_path.c_str());
        }

    } // namespace
} // namespace diffuri