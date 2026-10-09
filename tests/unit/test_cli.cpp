#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

#include "autonomize/autonomize.h"
#include "cli/cli.h"
#include "input/input.h"
#include "input/parser.h"
#include "normalize/normalize.h"
#include "order_reducer/order_reducer.h"
#include "polynomization/polynomization.h"
#include "quadratize/quadratize.h"
#include "solver/solver.h"

namespace diffuri {
    namespace {

        // Хелпер: собрать argv из вектора строк.
        // argv[0] = "Diffuri", argv[i] = strings[i-1].
        class Argv {
        public:
            explicit Argv(std::vector<std::string> args) : storage_(std::move(args)) {
                ptrs_.push_back(const_cast<char*>("Diffuri"));
                for (auto& s : storage_)
                    ptrs_.push_back(const_cast<char*>(s.c_str()));
            }
            int argc() const { return static_cast<int>(ptrs_.size()); }
            char** argv() { return ptrs_.data(); }
        private:
            std::vector<std::string> storage_;
            std::vector<char*>       ptrs_;
        };

        // ------------------------------------------------------------------------
        // Разбор аргументов
        // ------------------------------------------------------------------------

        TEST(CliParse, NoArgsGivesDefaults) {
            Argv a({});
            auto r = ParseCliArgs(a.argc(), a.argv());
            EXPECT_EQ(r.status, CliParseStatus::Ok);
            EXPECT_DOUBLE_EQ(r.options.solve.t_end, 1.0);
            EXPECT_EQ(r.options.solve.M, 20u);
            EXPECT_DOUBLE_EQ(r.options.solve.h_init, 1e-3);
            EXPECT_TRUE(r.options.log_path.empty());
            EXPECT_TRUE(r.options.trajectory_path.empty());
        }

        TEST(CliParse, PositionalAllFour) {
            Argv a({ "2.5", "15", "5e-4", "out.log" });
            auto r = ParseCliArgs(a.argc(), a.argv());
            EXPECT_EQ(r.status, CliParseStatus::Ok);
            EXPECT_DOUBLE_EQ(r.options.solve.t_end, 2.5);
            EXPECT_EQ(r.options.solve.M, 15u);
            EXPECT_DOUBLE_EQ(r.options.solve.h_init, 5e-4);
            EXPECT_EQ(r.options.log_path, "out.log");
        }

        TEST(CliParse, PositionalPartial) {
            Argv a({ "2.5" });
            auto r = ParseCliArgs(a.argc(), a.argv());
            EXPECT_EQ(r.status, CliParseStatus::Ok);
            EXPECT_DOUBLE_EQ(r.options.solve.t_end, 2.5);
            EXPECT_EQ(r.options.solve.M, 20u);  // дефолт
        }

        TEST(CliParse, TrajectoryBeforePositional) {
            Argv a({ "--trajectory", "traj.csv", "1", "20", "1e-3" });
            auto r = ParseCliArgs(a.argc(), a.argv());
            EXPECT_EQ(r.status, CliParseStatus::Ok);
            EXPECT_EQ(r.options.trajectory_path, "traj.csv");
            EXPECT_DOUBLE_EQ(r.options.solve.t_end, 1.0);
            EXPECT_EQ(r.options.solve.M, 20u);
        }

        TEST(CliParse, TrajectoryAfterPositional) {
            Argv a({ "1", "20", "1e-3", "--trajectory", "traj.csv" });
            auto r = ParseCliArgs(a.argc(), a.argv());
            EXPECT_EQ(r.status, CliParseStatus::Ok);
            EXPECT_EQ(r.options.trajectory_path, "traj.csv");
            EXPECT_EQ(r.options.solve.M, 20u);
        }

        TEST(CliParse, TrajectoryWithoutPath) {
            Argv a({ "--trajectory" });
            auto r = ParseCliArgs(a.argc(), a.argv());
            EXPECT_EQ(r.status, CliParseStatus::Error);
            EXPECT_NE(r.error.find("requires a path"), std::string::npos);
        }

        TEST(CliParse, HelpLong) {
            Argv a({ "--help" });
            auto r = ParseCliArgs(a.argc(), a.argv());
            EXPECT_EQ(r.status, CliParseStatus::Help);
        }

        TEST(CliParse, HelpShort) {
            Argv a({ "-h" });
            auto r = ParseCliArgs(a.argc(), a.argv());
            EXPECT_EQ(r.status, CliParseStatus::Help);
        }

        TEST(CliParse, UnknownFlag) {
            Argv a({ "--foo" });
            auto r = ParseCliArgs(a.argc(), a.argv());
            EXPECT_EQ(r.status, CliParseStatus::Error);
            EXPECT_NE(r.error.find("unknown option"), std::string::npos);
        }

        TEST(CliParse, TooManyPositional) {
            Argv a({ "1", "20", "1e-3", "log.txt", "extra" });
            auto r = ParseCliArgs(a.argc(), a.argv());
            EXPECT_EQ(r.status, CliParseStatus::Error);
            EXPECT_NE(r.error.find("too many"), std::string::npos);
        }

        TEST(CliParse, BadNumeric) {
            Argv a({ "abc" });
            auto r = ParseCliArgs(a.argc(), a.argv());
            EXPECT_EQ(r.status, CliParseStatus::Error);
            EXPECT_NE(r.error.find("bad argument"), std::string::npos);
        }

        TEST(CliParse, HelpWinsOverEverything) {
            // --help стоит первым — остальное не важно.
            Argv a({ "--help", "--foo", "1" });
            auto r = ParseCliArgs(a.argc(), a.argv());
            EXPECT_EQ(r.status, CliParseStatus::Help);
        }

        // ------------------------------------------------------------------------
        // Печать справки
        // ------------------------------------------------------------------------

        TEST(CliUsage, ContainsKeyStrings) {
            std::ostringstream oss;
            PrintUsage(oss, "Diffuri");
            const std::string s = oss.str();
            EXPECT_NE(s.find("Usage:"), std::string::npos);
            EXPECT_NE(s.find("--trajectory"), std::string::npos);
            EXPECT_NE(s.find("--help"), std::string::npos);
            EXPECT_NE(s.find("t_end"), std::string::npos);
        }

        // ------------------------------------------------------------------------
        // SaveTrajectory
        // ------------------------------------------------------------------------

        // Сформировать фиктивный Solution для тестов.
        Solution MakeFakeSolution() {
            Solution s;
            s.functions = { "x" };
            s.independent_variable = "t";
            s.points = {
                {0.0, {1.0}},
                {0.5, {0.5}},
                {1.0, {0.25}},
            };
            s.steps = 2;
            s.t_final = 1.0;
            s.order_used = 20;
            return s;
        }

        TEST(SaveTrajectory, WritesHeaderAndRows) {
            const Solution sol = MakeFakeSolution();
            const std::string path = "test_traj_csv_ok.csv";
            std::remove(path.c_str());

            // visible_functions = sol.functions → поведение как раньше.
            SaveTrajectory(sol, sol.functions, path);

            std::ifstream f(path);
            ASSERT_TRUE(f.is_open());

            std::string line;
            std::getline(f, line);
            EXPECT_EQ(line, "t,x");   // заголовок

            std::size_t rows = 0;
            while (std::getline(f, line)) ++rows;
            f.close();
            std::remove(path.c_str());

            EXPECT_EQ(rows, sol.points.size());
        }

        TEST(SaveTrajectory, RoundTripPrecision) {
            Solution sol = MakeFakeSolution();
            sol.points[1].x[0] = 0.12345678901234567;   // 17 значащих цифр

            const std::string path = "test_traj_csv_rt.csv";
            std::remove(path.c_str());
            SaveTrajectory(sol, sol.functions, path);

            std::ifstream f(path);
            ASSERT_TRUE(f.is_open());
            std::string line;
            std::getline(f, line);   // заголовок
            std::getline(f, line);   // t=0
            std::getline(f, line);   // t=0.5

            // "0.5,0.12345678901234567"
            const auto comma = line.find(',');
            ASSERT_NE(comma, std::string::npos);
            const double v = std::stod(line.substr(comma + 1));
            f.close();
            std::remove(path.c_str());

            // Round-trip должен совпасть бит-в-бит.
            EXPECT_EQ(v, 0.12345678901234567);
        }

        TEST(SaveTrajectory, BadPathThrows) {
            const Solution sol = MakeFakeSolution();
            EXPECT_THROW(
                SaveTrajectory(sol, sol.functions,
                    "/no/such/directory/x.csv"),
                SolverError);
        }

        TEST(SaveTrajectory, TwoFunctions) {
            Solution sol;
            sol.functions = { "x", "y" };
            sol.points = {
                {0.0, {1.0, 0.0}},
                {1.0, {0.5, 0.5}},
            };
            const std::string path = "test_traj_csv_2f.csv";
            std::remove(path.c_str());
            SaveTrajectory(sol, sol.functions, path);

            std::ifstream f(path);
            ASSERT_TRUE(f.is_open());
            std::string header;
            std::getline(f, header);
            f.close();
            std::remove(path.c_str());

            EXPECT_EQ(header, "t,x,y");
        }

    } // namespace

    // ========================================================================
    // Дополнительное покрытие SaveTrajectory
    // ========================================================================

    namespace {

        // Полный пайплайн — тот же хелпер, что в tests/systems.
        RawSystem FullPipeline(const std::string& text) {
            RawSystem sys = ParseSystem(text);
            NormalizeSystem(sys);
            ReduceOrder(sys);
            Autonomize(sys);
            Polynomize(sys);
            Quadratize(sys);
            return sys;
        }

        // Разобрать одну строку CSV в вектор double.
        std::vector<double> ParseCsvRow(const std::string& line) {
            std::vector<double> out;
            std::size_t start = 0;
            while (start <= line.size()) {
                const std::size_t comma = line.find(',', start);
                const std::string tok =
                    (comma == std::string::npos)
                    ? line.substr(start)
                    : line.substr(start, comma - start);
                out.push_back(std::stod(tok));
                if (comma == std::string::npos) break;
                start = comma + 1;
            }
            return out;
        }

        // Прочитать CSV: первая строка — заголовок, остальные — числа.
        struct CsvData {
            std::string                        header;
            std::vector<std::vector<double>>   rows;
        };

        CsvData ReadCsv(const std::string& path) {
            CsvData out;
            std::ifstream f(path);
            if (!f.is_open()) return out;
            std::getline(f, out.header);
            std::string line;
            while (std::getline(f, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (line.empty()) continue;
                out.rows.push_back(ParseCsvRow(line));
            }
            return out;
        }

        // Уникальное имя файла для каждого теста.
        std::string TempPath(const std::string& tag) {
            return "test_cli_" + tag + ".csv";
        }

    }  // namespace

    // ------------------------------------------------------------------------
    // Интеграция: Solve → CSV.
    // ------------------------------------------------------------------------

    TEST(SaveTrajectory, MatchesSolutionPoints) {
        RawSystem sys = FullPipeline("x' = -x\nx(0) = 1\n");
        SolveOptions opts;
        opts.t_end = 0.5;
        opts.h_init = 1e-2;
        const Solution sol = Solve(sys, opts);
        ASSERT_FALSE(sol.points.empty());

        const std::string path = TempPath("matches");
        std::remove(path.c_str());
        SaveTrajectory(sol, sol.functions, path);

        const CsvData csv = ReadCsv(path);
        std::remove(path.c_str());

        ASSERT_EQ(csv.rows.size(), sol.points.size());
        for (std::size_t i = 0; i < sol.points.size(); ++i) {
            ASSERT_EQ(csv.rows[i].size(), sol.points[i].x.size() + 1);
            EXPECT_DOUBLE_EQ(csv.rows[i][0], sol.points[i].t) << "i=" << i;
            for (std::size_t j = 0; j < sol.points[i].x.size(); ++j) {
                EXPECT_DOUBLE_EQ(csv.rows[i][j + 1], sol.points[i].x[j])
                    << "i=" << i << " j=" << j;
            }
        }
    }

    TEST(SaveTrajectory, FirstPointIsInitial) {
        RawSystem sys = FullPipeline("x' = -x\nx(0) = 0.5\n");
        SolveOptions opts;
        opts.t_end = 0.1;
        opts.h_init = 1e-2;
        const Solution sol = Solve(sys, opts);

        const std::string path = TempPath("first");
        std::remove(path.c_str());
        SaveTrajectory(sol, sol.functions, path);
        const CsvData csv = ReadCsv(path);
        std::remove(path.c_str());

        ASSERT_GE(csv.rows.size(), 1u);
        EXPECT_DOUBLE_EQ(csv.rows.front()[0], 0.0);
        EXPECT_DOUBLE_EQ(csv.rows.front()[1], 0.5);
    }

    TEST(SaveTrajectory, LastPointMatchesTFinal) {
        RawSystem sys = FullPipeline("x' = -x\nx(0) = 1\n");
        SolveOptions opts;
        opts.t_end = 0.3;
        opts.h_init = 1e-2;
        const Solution sol = Solve(sys, opts);

        const std::string path = TempPath("last");
        std::remove(path.c_str());
        SaveTrajectory(sol, sol.functions, path);
        const CsvData csv = ReadCsv(path);
        std::remove(path.c_str());

        ASSERT_FALSE(csv.rows.empty());
        EXPECT_DOUBLE_EQ(csv.rows.back()[0], sol.t_final);
    }

    // ------------------------------------------------------------------------
    // Если передать полный список sol.functions — все колонки сохраняются
    // (обратная совместимость со старым поведением).
    // ------------------------------------------------------------------------

    TEST(SaveTrajectory, AuxiliaryColumnsPreservedWhenAllPassed) {
        RawSystem sys = FullPipeline(
            "x'' = -sin(x)\n"
            "x(0) = 0\n"
            "x'(0) = 1\n");
        SolveOptions opts;
        opts.t_end = 0.1;
        opts.h_init = 1e-3;
        const Solution sol = Solve(sys, opts);
        ASSERT_GT(sol.functions.size(), 2u)
            << "expected auxiliary vars after Polynomize/Quadratize";

        const std::string path = TempPath("aux");
        std::remove(path.c_str());
        SaveTrajectory(sol, sol.functions, path);
        const CsvData csv = ReadCsv(path);
        std::remove(path.c_str());

        // Заголовок: число колонок = functions.size() + 1 (для t).
        const std::size_t commas =
            static_cast<std::size_t>(std::count(csv.header.begin(),
                csv.header.end(), ','));
        EXPECT_EQ(commas + 1, sol.functions.size() + 1);
        for (const auto& row : csv.rows) {
            EXPECT_EQ(row.size(), sol.functions.size() + 1);
        }
    }

    TEST(SaveTrajectory, TimeIsMonotonic) {
        RawSystem sys = FullPipeline("x' = x^2\nx(0) = 1\n");
        SolveOptions opts;
        opts.t_end = 0.5;
        opts.h_init = 1e-3;
        const Solution sol = Solve(sys, opts);

        const std::string path = TempPath("monotonic");
        std::remove(path.c_str());
        SaveTrajectory(sol, sol.functions, path);
        const CsvData csv = ReadCsv(path);
        std::remove(path.c_str());

        ASSERT_GE(csv.rows.size(), 2u);
        for (std::size_t i = 1; i < csv.rows.size(); ++i) {
            EXPECT_LE(csv.rows[i - 1][0], csv.rows[i][0]) << "i=" << i;
        }
    }

    // ------------------------------------------------------------------------
    // Вырожденные случаи
    // ------------------------------------------------------------------------

    TEST(SaveTrajectory, SinglePoint) {
        Solution sol;
        sol.functions = { "x" };
        sol.points = { {0.0, {1.0}} };

        const std::string path = TempPath("single");
        std::remove(path.c_str());
        SaveTrajectory(sol, sol.functions, path);

        std::ifstream f(path);
        ASSERT_TRUE(f.is_open());
        std::string line;
        std::getline(f, line);
        EXPECT_EQ(line, "t,x");
        ASSERT_TRUE(std::getline(f, line));
        EXPECT_EQ(line, "0,1");
        EXPECT_FALSE(std::getline(f, line));   // больше строк нет
        f.close();
        std::remove(path.c_str());
    }

    // Пустой points — SolverError (новое поведение после ТЗ №4).
    TEST(SaveTrajectory, EmptyPointsThrows) {
        Solution sol;
        sol.functions = { "x" };
        // points пуст
        EXPECT_THROW(
            SaveTrajectory(sol, sol.functions, TempPath("empty")),
            SolverError);
    }

    // ------------------------------------------------------------------------
    // Round-trip нескольких значений double с разными экспонентами.
    // ------------------------------------------------------------------------

    TEST(SaveTrajectory, RoundTripMultipleMagnitudes) {
        const std::vector<double> values = {
            0.0,
            -0.0,
            1.0,
            -1.0,
            3.14159265358979323846,
            1e-300,
            1e300,
            2.2250738585072014e-308,   // DBL_MIN
            1.7976931348623157e308,    // DBL_MAX
        };

        Solution sol;
        sol.functions = { "x" };
        for (std::size_t i = 0; i < values.size(); ++i) {
            sol.points.push_back({ static_cast<double>(i), {values[i]} });
        }

        const std::string path = TempPath("roundtrip");
        std::remove(path.c_str());
        SaveTrajectory(sol, sol.functions, path);
        const CsvData csv = ReadCsv(path);
        std::remove(path.c_str());

        ASSERT_EQ(csv.rows.size(), values.size());
        for (std::size_t i = 0; i < values.size(); ++i) {
            ASSERT_EQ(csv.rows[i].size(), 2u);
            EXPECT_EQ(csv.rows[i][1], values[i]) << "i=" << i;
        }
    }

    TEST(SaveTrajectory, NonFiniteValuesWrittenAsText) {
        Solution sol;
        sol.functions = { "x" };
        sol.points = {
            {0.0, {std::numeric_limits<double>::infinity()}},
            {1.0, {-std::numeric_limits<double>::infinity()}},
            {2.0, {std::numeric_limits<double>::quiet_NaN()}},
        };

        const std::string path = TempPath("nonfinite");
        std::remove(path.c_str());
        SaveTrajectory(sol, sol.functions, path);

        std::ifstream f(path);
        ASSERT_TRUE(f.is_open());
        std::string line;
        std::getline(f, line);   // header
        ASSERT_TRUE(std::getline(f, line));
        EXPECT_NE(line.find("inf"), std::string::npos) << line;
        ASSERT_TRUE(std::getline(f, line));
        EXPECT_NE(line.find("-inf"), std::string::npos) << line;
        ASSERT_TRUE(std::getline(f, line));
        EXPECT_NE(line.find("nan"), std::string::npos) << line;
        f.close();
        std::remove(path.c_str());
    }

    TEST(SaveTrajectory, OverwritesExistingFile) {
        const std::string path = TempPath("overwrite");
        std::remove(path.c_str());

        {
            Solution sol;
            sol.functions = { "x" };
            sol.points = { {0.0, {1.0}}, {1.0, {2.0}} };
            SaveTrajectory(sol, sol.functions, path);
        }

        {
            Solution sol;
            sol.functions = { "x" };
            sol.points = { {0.0, {9.0}} };
            SaveTrajectory(sol, sol.functions, path);
        }

        const CsvData csv = ReadCsv(path);
        std::remove(path.c_str());
        ASSERT_EQ(csv.rows.size(), 1u);
        EXPECT_DOUBLE_EQ(csv.rows[0][1], 9.0);
    }

    // ------------------------------------------------------------------------
    // End-to-end через CLI-слой.
    // ------------------------------------------------------------------------

    TEST(SaveTrajectory, CliOptionsDriveTrajectory) {
        Argv a({ "--trajectory", "test_cli_via_opts.csv", "0.2" });
        auto parsed = ParseCliArgs(a.argc(), a.argv());
        ASSERT_EQ(parsed.status, CliParseStatus::Ok);
        ASSERT_EQ(parsed.options.trajectory_path, "test_cli_via_opts.csv");
        EXPECT_DOUBLE_EQ(parsed.options.solve.t_end, 0.2);

        RawSystem sys = FullPipeline("x' = -x\nx(0) = 1\n");
        const Solution sol = Solve(sys, parsed.options.solve);

        std::remove(parsed.options.trajectory_path.c_str());
        SaveTrajectory(sol, sol.functions,
            parsed.options.trajectory_path);

        std::ifstream f(parsed.options.trajectory_path);
        ASSERT_TRUE(f.is_open());
        std::string line;
        std::getline(f, line);
        EXPECT_EQ(line, "t,x");
        f.close();
        std::remove(parsed.options.trajectory_path.c_str());
    }

    // ========================================================================
    // НОВЫЕ ТЕСТЫ (ТЗ №4, §3.2)
    // ========================================================================

    // 1. OnlyVisibleColumnsAreWritten.
    //    Система x' = x^3 — после Quadratize в sol.functions есть q_1 = x².
    //    Передаём visible = {"x"}; заголовок должен быть "t,x", а "q_"
    //    не должно встречаться нигде.
    TEST(SaveTrajectory, OnlyVisibleColumnsAreWritten) {
        RawSystem sys = FullPipeline("x' = x^3\nx(0) = 1\n");
        SolveOptions opts;
        opts.t_end = 0.3;
        opts.h_init = 1e-3;
        const Solution sol = Solve(sys, opts);
        ASSERT_GE(sol.functions.size(), 2u)
            << "expected auxiliary var q_1 after Quadratize";

        const std::string path = TempPath("only_visible");
        std::remove(path.c_str());
        SaveTrajectory(sol, { "x" }, path);

        std::ifstream f(path);
        ASSERT_TRUE(f.is_open());
        std::string content((std::istreambuf_iterator<char>(f)),
            std::istreambuf_iterator<char>());
        f.close();
        std::remove(path.c_str());

        ASSERT_FALSE(content.empty());
        const std::size_t nl = content.find('\n');
        ASSERT_NE(nl, std::string::npos);
        EXPECT_EQ(content.substr(0, nl), "t,x");

        EXPECT_EQ(content.find("q_"), std::string::npos) << content;
    }

    // 2. OrderOfVisibleIsPreserved.
    //    visible = {"y", "x"} → заголовок "t,y,x" (не "t,x,y").
    TEST(SaveTrajectory, OrderOfVisibleIsPreserved) {
        Solution sol;
        sol.functions = { "x", "y", "q_1" };
        sol.points = {
            {0.0, {1.0, 2.0, 3.0}},
            {1.0, {1.5, 2.5, 3.5}},
        };

        const std::string path = TempPath("order");
        std::remove(path.c_str());
        SaveTrajectory(sol, { "y", "x" }, path);

        std::ifstream f(path);
        ASSERT_TRUE(f.is_open());
        std::string header;
        std::getline(f, header);
        std::string first_row;
        std::getline(f, first_row);
        f.close();
        std::remove(path.c_str());

        EXPECT_EQ(header, "t,y,x");
        // Первая строка: t=0, y=2, x=1 → "0,2,1".
        EXPECT_EQ(first_row, "0,2,1");
    }

    // 3. UnknownVisibleThrows.
    TEST(SaveTrajectory, UnknownVisibleThrows) {
        Solution sol;
        sol.functions = { "x", "y" };
        sol.points = { {0.0, {1.0, 2.0}} };

        try {
            SaveTrajectory(sol, { "x", "nope" }, TempPath("unknown"));
            FAIL() << "expected SolverError";
        }
        catch (const SolverError& e) {
            const std::string what = e.what();
            EXPECT_NE(what.find("nope"), std::string::npos) << what;
            EXPECT_NE(what.find("SaveTrajectory"), std::string::npos)
                << what;
        }
    }

    // 4. AllVisibleIsBackwardCompatible.
    //    visible = sol.functions → заголовок и данные как в старом коде.
    TEST(SaveTrajectory, AllVisibleIsBackwardCompatible) {
        Solution sol;
        sol.functions = { "x", "y" };
        sol.points = {
            {0.0, {1.0, 2.0}},
            {0.5, {1.5, 2.5}},
        };

        const std::string path = TempPath("all_visible");
        std::remove(path.c_str());
        SaveTrajectory(sol, sol.functions, path);

        std::ifstream f(path);
        ASSERT_TRUE(f.is_open());
        std::string header;
        std::getline(f, header);
        std::string r1;
        std::getline(f, r1);
        std::string r2;
        std::getline(f, r2);
        f.close();
        std::remove(path.c_str());

        EXPECT_EQ(header, "t,x,y");
        EXPECT_EQ(r1, "0,1,2");
        EXPECT_EQ(r2, "0.5,1.5,2.5");
    }

} // namespace diffuri