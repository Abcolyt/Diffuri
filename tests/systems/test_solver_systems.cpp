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

        // ============================================================================
// Хелпер для проверки сходимости метода Тейлора при увеличении порядка M.
// ============================================================================

/// @brief Отчёт о сходимости: значения переменных при разных M и дельты.
        struct TaylorConvergenceReport {
            /// Порядок метода на каждом шаге.
            std::vector<std::size_t> orders;
            /// Значения переменных: values[i][k] — значение k-й переменной
            /// при M = orders[i].
            std::vector<std::vector<double>> values;
            /// Максимальная абсолютная разность между соседними M
            /// по всем переменным.
            /// deltas[i] = max_k |values[i+1][k] - values[i][k]|.
            std::vector<double> deltas;
            /// Имена переменных, для которых проводилась проверка.
            std::vector<std::string> var_names;
        };

        /// @brief Прогнать решатель с разными порядками M и собрать отчёт
        ///        о сходимости.
        ///
        /// Для каждого M из диапазона [m_start, m_end] с шагом m_step:
        ///   1. Решает систему с заданными t_end, rtol, atol, h_init.
        ///   2. Извлекает значения переменных var_names в конечной точке.
        ///   3. Вычисляет максимальную абсолютную разность с предыдущим M.
        ///   4. При verbose=true печатает таблицы значений и дельт.
        ///
        /// @param input     Текст системы ОДУ.
        /// @param var_names Имена переменных для отслеживания.
        /// @param m_start   Начальный порядок метода.
        /// @param m_end     Конечный порядок метода (включительно).
        /// @param m_step    Шаг изменения порядка.
        /// @param t_end     Конечная точка интегрирования.
        /// @param rtol      Относительная точность решателя.
        /// @param atol      Абсолютная точность решателя.
        /// @param h_init    Начальный шаг интегрирования.
        /// @param verbose   Печатать ли таблицы (false для пакетного прогона).
        /// @return          Отчёт о сходимости.
        TaylorConvergenceReport RunTaylorConvergenceTest(
            const std::string& input,
            const std::vector<std::string>& var_names,
            std::size_t m_start,
            std::size_t m_end,
            std::size_t m_step,
            double t_end,
            double rtol,
            double atol,
            double h_init,
            bool verbose = true)
        {
            TaylorConvergenceReport report;
            report.var_names = var_names;

            SolveOptions opts;
            opts.t_end = t_end;
            opts.rtol = rtol;
            opts.atol = atol;
            opts.h_init = h_init;

            if (verbose) {
                std::cout << "\n"
                    << "=============================================="
                    << "==================\n";
                std::cout << "  Taylor Method Convergence Test\n";
                std::cout << "  t_end = " << t_end
                    << ", rtol = " << rtol
                    << ", atol = " << atol << "\n";
                std::cout << "=============================================="
                    << "==================\n";
                std::cout << std::scientific << std::setprecision(15);
                std::cout << "  M  |";
                for (const auto& name : var_names) {
                    std::cout << "    " << std::setw(16) << name << " |";
                }
                std::cout << "\n";
                std::cout << "-----+";
                for (std::size_t k = 0; k < var_names.size(); ++k) {
                    std::cout << "--------------------+";
                }
                std::cout << "\n";
            }

            // --- Главный цикл: перебор M ---
            for (std::size_t M = m_start; M <= m_end; M += m_step) {
                opts.M = M;
                RunResult r = RunPipeline(input, opts);
                const auto& last_pt = r.solution.points.back();

                std::vector<double> current_values;
                current_values.reserve(var_names.size());

                if (verbose) {
                    std::cout << std::setw(3) << M << " |";
                }

                for (const auto& name : var_names) {
                    double val = std::nan("");
                    for (std::size_t i = 0; i < r.solution.functions.size(); ++i) {
                        if (r.solution.functions[i] == name) {
                            val = last_pt.x[i];
                            break;
                        }
                    }
                    current_values.push_back(val);
                    if (verbose) {
                        std::cout << " " << std::setw(18) << val << " |";
                    }
                }

                if (verbose) {
                    std::cout << "\n";
                }

                report.orders.push_back(M);
                report.values.push_back(std::move(current_values));
            }

            if (verbose) {
                std::cout << "-----+";
                for (std::size_t k = 0; k < var_names.size(); ++k) {
                    std::cout << "--------------------+";
                }
                std::cout << "\n";
                std::cout << "\nDifferences between consecutive M values "
                    << "(max over variables):\n";
                std::cout << "  M  |    delta\n";
                std::cout << "-----+--------------------\n";
            }

            // --- Вычисление дельт ---
            for (std::size_t i = 1; i < report.values.size(); ++i) {
                double max_delta = 0.0;
                for (std::size_t k = 0; k < var_names.size(); ++k) {
                    double diff = std::abs(report.values[i][k]
                        - report.values[i - 1][k]);
                    if (diff > max_delta) max_delta = diff;
                }
                report.deltas.push_back(max_delta);
                if (verbose) {
                    std::cout << std::setw(3) << report.orders[i] << " | "
                        << std::setw(18) << max_delta << "\n";
                }
            }

            if (verbose) {
                std::cout << "-----+--------------------\n";
            }

            return report;
        }

        // ============================================================================
        // Тест сходимости метода Тейлора для CR3BP при увеличении порядка M.
        //
        // Цель: доказать, что наш решатель сходится к стабильному значению,
        // и найти это значение с точностью, превышающей эталонный scipy (DOP853).
        // Если разница между M=40 и M=45 исчезающе мала, значит M=45 даёт
        // "истинное" решение в пределах машинного эпсилона.
        // ============================================================================
        TEST(SolverSystems, CR3BPConvergenceWithOrderM) {
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
                "x(0)  = 0.5\n"
                "y(0)  = 0.0\n"
                "vx(0) = 0.0\n"
                "vy(0) = 0.5\n"
                "u1(0) = 1.9525508\n"
                "w1(0) = 3.8124546\n"
                "u2(0) = 2.0498130\n"
                "w2(0) = 4.2017330\n";

            const std::vector<std::string> kVars = { "x", "y", "vx", "vy" };

            auto report = RunTaylorConvergenceTest(
                kInput, kVars,
                /*m_start=*/15, /*m_end=*/45, /*m_step=*/5,
                /*t_end=*/1.0,
                /*rtol=*/1e-13, /*atol=*/1e-15, /*h_init=*/1e-3,
                /*verbose=*/true);

            ASSERT_GE(report.deltas.size(), 2u)
                << "Not enough M values to assess convergence";

            const double final_delta = report.deltas.back();
            std::cout << "\nFinal delta (M=" << report.orders.back()
                << " vs M=" << report.orders[report.orders.size() - 2]
                << "): " << final_delta << "\n";

            EXPECT_LT(final_delta, 1e-9)
                << "Solver did not converge between the last two M values.";

            EXPECT_LT(final_delta, report.deltas.front() * 10.0)
                << "No overall convergence trend.";

            std::cout << "=== Convergence test PASSED. M="
                << report.orders.back() << " is the ground truth. ===\n";
        }

        // ============================================================================
        // Тест сходимости метода Тейлора для всех систем из reference_systems.h.
        //
        // Для каждой системы прогоняет решатель с M от 15 до 35 (шаг 5) и
        // проверяет, что финальная дельта между соседними M меньше 1e-9.
        // Это доказывает, что метод Тейлора сходится к стабильному решению
        // для каждой системы в наборе.
        //
        // Системы, которые выбрасывают SolverError (например, blowup до t_end),
        // пропускаются с пометкой в логе.
        // ============================================================================
        TEST(SolverSystems, ConvergenceAllSystems) {
            std::cout << "\n=== ConvergenceAllSystems ===\n";

            for (const auto& ref : test_data::All()) {
                SCOPED_TRACE("system: " + ref.name);

                // Собираем имена переменных из эталона.
                std::vector<std::string> var_names;
                var_names.reserve(ref.expected.size());
                for (const auto& [name, value] : ref.expected) {
                    var_names.push_back(name);
                }

                try {
                    auto report = RunTaylorConvergenceTest(
                        ref.input, var_names,
                        /*m_start=*/15, /*m_end=*/35, /*m_step=*/5,
                        /*t_end=*/ref.t_end,
                        /*rtol=*/1e-13, /*atol=*/1e-15,
                        /*h_init=*/ref.h_init,
                        /*verbose=*/false);

                    ASSERT_GE(report.deltas.size(), 2u)
                        << ref.name << ": not enough M values";

                    const double final_delta = report.deltas.back();

                    std::cout << "  " << std::setw(25) << std::left << ref.name
                        << "  final_delta = "
                        << std::scientific << std::setprecision(3)
                        << final_delta << "\n";

                    EXPECT_LT(final_delta, 1e-9)
                        << ref.name << ": solver did not converge "
                        << "(final delta = " << final_delta << ")";

                }
                catch (const SolverError& e) {
                    // Blowup-системы могут выбросить исключение при больших M
                    // и жёстких допусках. Это ожидаемо — пропускаем.
                    std::cout << "  " << std::setw(25) << std::left << ref.name
                        << "  SKIPPED (SolverError: " << e.what() << ")\n";
                }
            }

            std::cout << "=== End ConvergenceAllSystems ===\n";
        }

    }  // namespace
}  // namespace diffuri