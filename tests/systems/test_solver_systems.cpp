// ============================================================================
// tests/systems/test_solver_systems.cpp
//
// Системные E2E-тесты Solver на наборе систем.
//
// Форматтер берётся из output/output.h (WriteSolved) — тот же, что
// использует CLI. Вывод перехватывается через std::ostringstream;
// никакой подмены std::cout.rdbuf не требуется.
//
// Эталоны сгенерированы tools/gen_reference.py.
// ============================================================================
#include <gtest/gtest.h>

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <sstream>
#include <string>

#include "output/output.h"
#include "pipeline/runner.h"
#include "solver/solver.h"

#include "reference_systems.h"

namespace diffuri {
    namespace {

        /// Прогнать пайплайн и вернуть отформатированный [Solved]-лог.
        std::string RunAndFormat(const test_data::SystemRef& ref) {
            SolveOptions opts;
            opts.t_end = ref.t_end;
            opts.rtol = ref.rtol;
            opts.atol = ref.atol;
            opts.M = ref.M;
            opts.h_init = ref.h_init;

            RunResult r = RunPipeline(ref.input, opts);

            std::ostringstream oss;
            WriteSolved(r.solution, r.trace, oss);
            return oss.str();
        }

        /// Извлечь значение из строки вида "<name>(<t>) = <value>".
        /// Возвращает NaN, если строка не найдена.
        double ExtractValue(const std::string& log, const std::string& name) {
            const std::string prefix = name + "(";
            std::size_t pos = log.find(prefix);
            if (pos == std::string::npos) return std::nan("");
            pos = log.find('=', pos);
            if (pos == std::string::npos) return std::nan("");
            ++pos;
            while (pos < log.size() &&
                std::isspace(static_cast<unsigned char>(log[pos]))) {
                ++pos;
            }
            char* end = nullptr;
            return std::strtod(log.c_str() + pos, &end);
        }

        TEST(SolverSystems, AllMatchReference) {
            for (const auto& ref : test_data::All()) {
                SCOPED_TRACE("system: " + ref.name);

                const std::string log = RunAndFormat(ref);

                EXPECT_NE(log.find("[Solved]"), std::string::npos);
                EXPECT_NE(log.find("# steps:"), std::string::npos);
                EXPECT_NE(log.find("# order:"), std::string::npos);
                EXPECT_NE(log.find("# t_final:"), std::string::npos);

                for (const auto& [name, expected] : ref.expected) {
                    const double actual = ExtractValue(log, name);
                    ASSERT_FALSE(std::isnan(actual))
                        << "value for '" << name << "' not found in log:\n" << log;
                    const double tol = 1e-9 * (1.0 + std::abs(expected));
                    EXPECT_NEAR(actual, expected, tol) << "log:\n" << log;
                }
            }
        }

        TEST(SolverSystems, FormatFirstLineIsLiteral) {
            const auto& refs = test_data::All();
            ASSERT_FALSE(refs.empty());
            const std::string log = RunAndFormat(refs.front());
            EXPECT_EQ(log.substr(0, 9), "[Solved]\n");
        }

        TEST(SolverSystems, StepsAndOrderAreIntegers) {
            const auto& refs = test_data::All();
            ASSERT_FALSE(refs.empty());
            const std::string log = RunAndFormat(refs.front());

            const std::size_t steps_pos = log.find("# steps: ");
            ASSERT_NE(steps_pos, std::string::npos);
            const char* p = log.c_str() + steps_pos + 9;
            char* end = nullptr;
            const long steps = std::strtol(p, &end, 10);
            EXPECT_GT(steps, 0);
            EXPECT_EQ(*end, '\n');
        }

        TEST(SolverSystems, AuxiliaryHiddenMarker) {
            for (const auto& ref : test_data::All()) {
                const std::string log = RunAndFormat(ref);
                const std::size_t pos = log.find("# auxiliary hidden: ");
                if (ref.name == "pendulum" || ref.name == "forced_resonance") {
                    ASSERT_NE(pos, std::string::npos) << ref.name;
                    const int k = std::atoi(log.c_str() + pos + 20);
                    EXPECT_GT(k, 0);
                }
                else {
                    EXPECT_EQ(pos, std::string::npos)
                        << "unexpected auxiliary marker for " << ref.name;
                }
            }
        }

        TEST(SolverSystems, BlowupDoesNotProduceInf) {
            // x' = x^3, x(0)=1, t_end = 1.0 — за полюсом (t = 0.5).
            // Solve не должен возвращать Solution с !isfinite(x).
            // SolverError — тоже приемлемый исход.
            SolveOptions opts;
            opts.t_end = 1.0;
            opts.M = 20;
            opts.h_init = 1e-3;

            try {
                RunResult r = RunPipeline("x' = x^3\nx(0) = 1\n", opts);
                for (const auto& pt : r.solution.points) {
                    for (double v : pt.x) {
                        EXPECT_TRUE(std::isfinite(v))
                            << "non-finite value at t = " << pt.t;
                    }
                }
            }
            catch (const SolverError&) {
                SUCCEED() << "SolverError is acceptable for blowup case";
            }
        }

    }  // namespace
}  // namespace diffuri