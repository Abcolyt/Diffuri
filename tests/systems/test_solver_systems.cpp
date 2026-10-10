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

#include <iomanip>

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

            // Для сверки с эталонами допечатываем все функции решения,
            // включая скрытые вспомогательные переменные (q_*, v_*).
            // Пользовательский вывод при этом остаётся прежним: маркер
            // "# auxiliary hidden: N" сохраняется выше.
            if (!r.solution.points.empty()) {
                const auto& last = r.solution.points.back();

                oss << '\n';
                oss << std::setprecision(17);

                for (std::size_t i = 0; i < r.solution.functions.size(); ++i) {
                    oss << r.solution.functions[i]
                        << "(" << last.t << ") = "
                        << last.x[i] << '\n';
                }
            }

            return oss.str();
        }

        /// Извлечь значение из строки вида "<name>(<t>) = <value>".
        /// Возвращает NaN, если строка не найдена.
        double ExtractValue(const std::string& log, const std::string& name) {
            const std::string prefix = "\n" + name + "(";  // ← добавлен \n
            std::size_t pos = log.find(prefix);
            if (pos == std::string::npos) return std::nan("");
            pos += 1;  // пропустить \n, встать на начало имени
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

                // Системы, у которых Polynomize/Quadratize вводят скрытые
                // служебные переменные (v_*, q_*) — маркер обязан присутствовать
                // и число должно быть положительным.
                const bool expects_aux =
                    ref.name == "pendulum" ||
                    ref.name == "forced_resonance" ||
                    ref.name == "van_der_pol" ||
                    ref.name == "blowup_x3_partial" ||
                    ref.name == "cr3bp_earth_moon" ||
                    ref.name == "forced_exp";

                if (expects_aux) {
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

        TEST(SolverSystems, CR3BPMatchesScipy) {
            // Найти запись cr3bp_earth_moon в test_data::All()
            const test_data::SystemRef* ref = nullptr;
            for (const auto& r : test_data::All()) {
                if (r.name == "cr3bp_earth_moon") { ref = &r; break; }
            }
            ASSERT_NE(ref, nullptr) << "cr3bp_earth_moon not in reference list";

            const std::string log = RunAndFormat(*ref);

            EXPECT_NE(log.find("[Solved]"), std::string::npos);

            // Для CR3BP допуск шире: M=20, rtol=1e-10, система из 235 переменных
            constexpr double kTol = 1e-5;
            for (const auto& [name, expected] : ref->expected) {
                const double actual = ExtractValue(log, name);
                ASSERT_FALSE(std::isnan(actual))
                    << "value for '" << name << "' not found";
                EXPECT_NEAR(actual, expected, kTol)
                    << name << ": expected " << expected << ", got " << actual;
            }
        }

        TEST(SolverSystems, CR3BPJacobiIntegralConserved) {
            const char* kInput =
                "u1' = -u1*u1*u1*((x+0.0121505856)*vx + y*vy)\n"
                "u2' = -u2*u2*u2*((x-0.9878494144)*vx + y*vy)\n"
                "w1' = -2*u1*u1*u1*u1*((x+0.0121505856)*vx + y*vy)\n"
                "w2' = -2*u2*u2*u2*u2*((x-0.9878494144)*vx + y*vy)\n"
                "vx' = 2*vy + x - 0.9878494144*(x+0.0121505856)*u1*w1"
                "    - 0.0121505856*(x-0.9878494144)*u2*w2\n"
                "vy' = -2*vx + y - 0.9878494144*y*u1*w1"
                "    - 0.0121505856*y*u2*w2\n"
                "x' = vx\n"
                "y' = vy\n"
                "x(0)  = 0.5\ny(0)  = 0.0\nvx(0) = 0.0\nvy(0) = 0.5\n"
                "u1(0) = 1.9525508\nw1(0) = 3.8124546\n"
                "u2(0) = 2.0498130\nw2(0) = 4.2017330\n";

            SolveOptions opts;
            opts.t_end = 1.0;
            opts.M = 30;
            opts.rtol = 1e-12;
            opts.atol = 1e-14;
            opts.h_init = 1e-3;

            RunResult r = RunPipeline(kInput, opts);

            // Найти индексы x, y, vx, vy, u1, u2 в sol.functions
            auto idx = [&](const std::string& name) -> int {
                for (std::size_t i = 0; i < r.solution.functions.size(); ++i) {
                    if (r.solution.functions[i] == name) return (int)i;
                }
                return -1;
                };

            const int ix = idx("x"), iy = idx("y");
            const int ivx = idx("vx"), ivy = idx("vy");
            const int iu1 = idx("u1"), iu2 = idx("u2");
            ASSERT_GE(ix, 0); ASSERT_GE(iy, 0);
            ASSERT_GE(ivx, 0); ASSERT_GE(ivy, 0);
            ASSERT_GE(iu1, 0); ASSERT_GE(iu2, 0);

            auto C = [&](const SolvePoint& p) {
                const double x = p.x[ix], y = p.x[iy];
                const double vx = p.x[ivx], vy = p.x[ivy];
                const double u1 = p.x[iu1], u2 = p.x[iu2];
                constexpr double kMu = 0.0121505856;
                return x * x + y * y
                    + 2.0 * (1.0 - kMu) * u1
                    + 2.0 * kMu * u2
                    - vx * vx - vy * vy;
                };

            const double C0 = C(r.solution.points.front());
            const double C1 = C(r.solution.points.back());
            const double drift = std::abs(C1 - C0);

            // Для M=20, rtol=1e-10, t_end=1 — дрейф ~1e-7.
            // Порог с запасом: 1e-5.
            EXPECT_LT(drift, 1e-9)
                << "Jacobi integral drifted by " << drift
                << " (C0 = " << C0 << ", C1 = " << C1 << ")";
        }

    }  // namespace
}  // namespace diffuri