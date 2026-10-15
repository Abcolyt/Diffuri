// ============================================================================
// tests/unit/test_settings.cpp
//
// Юнит-тесты модуля cli/settings: чтение и запись пользовательских
// настроек (data/settings.txt), применяемых при интерактивном запуске.
//
// Структура файла:
//   1. Хелперы (TempFile, WriteFile, ReadFile, Tag).
//   2. DefaultSettingsPath.
//   3. Первый запуск: создание файла со значениями по умолчанию.
//   4. Чтение существующего файла: все поля SolveOptions.
//   5. Пути (input, log, trajectory).
//   6. Комментарии, пустые строки, пробелы, CRLF.
//   7. Диагностика: неизвестные ключи, мусорные строки, плохие значения.
//   8. Round-trip Save → Load.
//   9. Граничные случаи.
// ============================================================================
#include <gtest/gtest.h>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>

#include "cli/cli.h"
#include "cli/settings.h"

namespace diffuri {
    namespace {

        // ------------------------------------------------------------------------
        // 1. Хелперы
        // ------------------------------------------------------------------------

        // RAII-обёртка: удаляет файл и при создании, и при разрушении,
        // чтобы тесты не накапливали мусор в build-директории и не зависели
        // от результатов предыдущих прогонов.
        class TempFile {
        public:
            explicit TempFile(std::string path) : path_(std::move(path)) {
                std::remove(path_.c_str());
            }
            ~TempFile() { std::remove(path_.c_str()); }
            TempFile(const TempFile&) = delete;
            TempFile& operator=(const TempFile&) = delete;

            const std::string& path() const { return path_; }
        private:
            std::string path_;
        };

        void WriteFile(const std::string& path, const std::string& content) {
            std::ofstream f(path);
            ASSERT_TRUE(f.is_open()) << "cannot open " << path << " for writing";
            f << content;
        }

        std::string ReadFile(const std::string& path) {
            std::ifstream f(path);
            std::ostringstream oss;
            oss << f.rdbuf();
            return oss.str();
        }

        // Уникальное имя файла для каждого теста.
        std::string Tag(const std::string& name) {
            return "test_settings_" + name + ".txt";
        }

        // ------------------------------------------------------------------------
        // 2. DefaultSettingsPath
        // ------------------------------------------------------------------------

        TEST(Settings, DefaultPathIsDataSettingsTxt) {
            EXPECT_EQ(DefaultSettingsPath(), "data/settings.txt");
        }

        // ------------------------------------------------------------------------
        // 3. Первый запуск: создание файла
        // ------------------------------------------------------------------------

        TEST(Settings, FirstRunCreatesFile) {
            TempFile tmp(Tag("first_run"));
            CliOptions opts;
            opts.solve.t_end = 42.0;
            opts.solve.M = 17;

            const auto r = LoadSettings(tmp.path(), opts);

            EXPECT_FALSE(r.file_existed);
            EXPECT_TRUE(r.created_default);
            EXPECT_EQ(r.path, tmp.path());
            EXPECT_TRUE(r.warning.empty()) << r.warning;

            // Файл действительно создан на диске.
            std::ifstream f(tmp.path());
            ASSERT_TRUE(f.is_open());
        }

        TEST(Settings, FirstRunFileContainsCurrentValues) {
            TempFile tmp(Tag("first_run_content"));
            CliOptions opts;
            opts.solve.t_end = 3.5;
            opts.solve.M = 25;
            opts.solve.enable_order_adaptation = true;

            LoadSettings(tmp.path(), opts);

            const std::string content = ReadFile(tmp.path());
            EXPECT_NE(content.find("t_end = 3.5"), std::string::npos);
            EXPECT_NE(content.find("M = 25"), std::string::npos);
            EXPECT_NE(content.find("enable_order_adaptation = true"),
                std::string::npos);
        }

        TEST(Settings, SecondRunDoesNotRecreateFile) {
            TempFile tmp(Tag("second_run"));
            CliOptions opts;

            const auto r1 = LoadSettings(tmp.path(), opts);
            EXPECT_TRUE(r1.created_default);
            EXPECT_FALSE(r1.file_existed);

            const auto r2 = LoadSettings(tmp.path(), opts);
            EXPECT_FALSE(r2.created_default);
            EXPECT_TRUE(r2.file_existed);
        }

        // ------------------------------------------------------------------------
        // 4. Чтение существующего файла: поля SolveOptions
        // ------------------------------------------------------------------------

        TEST(Settings, ReadsAllSolveOptionFields) {
            TempFile tmp(Tag("all_fields"));
            WriteFile(tmp.path(),
                "t_end = 2.5\n"
                "rtol = 1e-8\n"
                "atol = 1e-11\n"
                "h_init = 5e-4\n"
                "h_min = 1e-15\n"
                "h_max = 0.5\n"
                "M = 30\n"
                "K = 2\n"
                "max_steps = 500\n"
                "M_min = 7\n"
                "M_max = 40\n"
                "m_factor = 3.5\n"
                "enable_order_adaptation = true\n"
            );

            CliOptions opts;
            const auto r = LoadSettings(tmp.path(), opts);

            EXPECT_TRUE(r.file_existed);
            EXPECT_FALSE(r.created_default);
            EXPECT_TRUE(r.warning.empty()) << r.warning;

            EXPECT_DOUBLE_EQ(opts.solve.t_end, 2.5);
            EXPECT_DOUBLE_EQ(opts.solve.rtol, 1e-8);
            EXPECT_DOUBLE_EQ(opts.solve.atol, 1e-11);
            EXPECT_DOUBLE_EQ(opts.solve.h_init, 5e-4);
            EXPECT_DOUBLE_EQ(opts.solve.h_min, 1e-15);
            EXPECT_DOUBLE_EQ(opts.solve.h_max, 0.5);
            EXPECT_EQ(opts.solve.M, 30u);
            EXPECT_EQ(opts.solve.K, 2u);
            EXPECT_EQ(opts.solve.max_steps, 500u);
            EXPECT_EQ(opts.solve.M_min, 7u);
            EXPECT_EQ(opts.solve.M_max, 40u);
            EXPECT_DOUBLE_EQ(opts.solve.m_factor, 3.5);
            EXPECT_TRUE(opts.solve.enable_order_adaptation);
        }

        TEST(Settings, OrderAdaptationFalseOverridesTrue) {
            TempFile tmp(Tag("adapt_false"));
            WriteFile(tmp.path(), "enable_order_adaptation = false\n");

            CliOptions opts;
            opts.solve.enable_order_adaptation = true;   // стартовое значение
            LoadSettings(tmp.path(), opts);

            EXPECT_FALSE(opts.solve.enable_order_adaptation);
        }

        TEST(Settings, PartialFileDoesNotResetOtherFields) {
            TempFile tmp(Tag("partial"));
            WriteFile(tmp.path(), "t_end = 9.0\n");

            CliOptions opts;
            opts.solve.M = 33;
            opts.solve.h_init = 0.123;

            LoadSettings(tmp.path(), opts);

            EXPECT_DOUBLE_EQ(opts.solve.t_end, 9.0);
            EXPECT_EQ(opts.solve.M, 33u);                 // не тронуто
            EXPECT_DOUBLE_EQ(opts.solve.h_init, 0.123);   // не тронуто
        }

        // ------------------------------------------------------------------------
        // 5. Пути (input, log, trajectory)
        // ------------------------------------------------------------------------

        TEST(Settings, ReadsPathFields) {
            TempFile tmp(Tag("paths"));
            WriteFile(tmp.path(),
                "input = data/in.txt\n"
                "log = out.log\n"
                "trajectory = traj.csv\n"
            );

            CliOptions opts;
            LoadSettings(tmp.path(), opts);

            EXPECT_EQ(opts.input_path, "data/in.txt");
            EXPECT_EQ(opts.log_path, "out.log");
            EXPECT_EQ(opts.trajectory_path, "traj.csv");
        }

        TEST(Settings, EmptyPathValueClearsField) {
            TempFile tmp(Tag("empty_path"));
            WriteFile(tmp.path(),
                "input =\n"
                "log =\n"
                "trajectory =\n"
            );

            CliOptions opts;
            opts.input_path = "should_be_cleared";
            opts.log_path = "should_be_cleared";
            opts.trajectory_path = "should_be_cleared";

            LoadSettings(tmp.path(), opts);

            EXPECT_TRUE(opts.input_path.empty());
            EXPECT_TRUE(opts.log_path.empty());
            EXPECT_TRUE(opts.trajectory_path.empty());
        }

        // ------------------------------------------------------------------------
        // 6. Комментарии, пустые строки, пробелы, CRLF
        // ------------------------------------------------------------------------

        TEST(Settings, CommentsAndBlankLinesIgnored) {
            TempFile tmp(Tag("comments"));
            WriteFile(tmp.path(),
                "# diffuri settings\n"
                "\n"
                "   # indented comment\n"
                "t_end = 7.5\n"
                "\n"
                "# trailing comment\n"
            );

            CliOptions opts;
            const auto r = LoadSettings(tmp.path(), opts);

            EXPECT_DOUBLE_EQ(opts.solve.t_end, 7.5);
            EXPECT_TRUE(r.warning.empty()) << r.warning;
        }

        TEST(Settings, WhitespaceAroundKeyAndValueTrimmed) {
            TempFile tmp(Tag("whitespace"));
            WriteFile(tmp.path(), "  \t t_end \t = \t 4.25 \t \n");

            CliOptions opts;
            const auto r = LoadSettings(tmp.path(), opts);

            EXPECT_DOUBLE_EQ(opts.solve.t_end, 4.25);
            EXPECT_TRUE(r.warning.empty()) << r.warning;
        }

        TEST(Settings, WindowsLineEndingsHandled) {
            TempFile tmp(Tag("crlf"));
            WriteFile(tmp.path(), "t_end = 1.5\r\nM = 12\r\n");

            CliOptions opts;
            LoadSettings(tmp.path(), opts);

            EXPECT_DOUBLE_EQ(opts.solve.t_end, 1.5);
            EXPECT_EQ(opts.solve.M, 12u);
        }

        // ------------------------------------------------------------------------
        // 7. Диагностика: неизвестные ключи, мусор, плохие значения
        // ------------------------------------------------------------------------

        TEST(Settings, UnknownKeyProducesWarningButDoesNotBreakParsing) {
            TempFile tmp(Tag("unknown_key"));
            WriteFile(tmp.path(),
                "t_end = 1.0\n"
                "foo_bar = 5\n"
                "M = 10\n"
            );

            CliOptions opts;
            const auto r = LoadSettings(tmp.path(), opts);

            // Соседние корректные строки всё равно применены.
            EXPECT_DOUBLE_EQ(opts.solve.t_end, 1.0);
            EXPECT_EQ(opts.solve.M, 10u);
            EXPECT_NE(r.warning.find("foo_bar"), std::string::npos);
        }

        TEST(Settings, MalformedLineProducesWarning) {
            TempFile tmp(Tag("malformed"));
            WriteFile(tmp.path(),
                "this is not a key=value line\n"
                "t_end = 2.0\n"
            );

            CliOptions opts;
            const auto r = LoadSettings(tmp.path(), opts);

            EXPECT_DOUBLE_EQ(opts.solve.t_end, 2.0);
            EXPECT_FALSE(r.warning.empty());
        }

        TEST(Settings, BadNumericValueWarnsAndDoesNotChangeField) {
            TempFile tmp(Tag("bad_numeric"));
            WriteFile(tmp.path(),
                "t_end = not_a_number\n"
                "M = 42\n"
            );

            CliOptions opts;
            opts.solve.t_end = 99.0;   // должно остаться 99.0
            const auto r = LoadSettings(tmp.path(), opts);

            EXPECT_DOUBLE_EQ(opts.solve.t_end, 99.0);
            EXPECT_EQ(opts.solve.M, 42u);   // следующая строка применена
            EXPECT_NE(r.warning.find("t_end"), std::string::npos);
        }

        TEST(Settings, NegativeValueForUnsignedFieldWarns) {
            TempFile tmp(Tag("negative_size"));
            WriteFile(tmp.path(),
                "M = -5\n"
                "K = 3\n"
            );

            CliOptions opts;
            opts.solve.M = 20;
            const auto r = LoadSettings(tmp.path(), opts);

            EXPECT_EQ(opts.solve.M, 20u);   // отрицательное → не применилось
            EXPECT_EQ(opts.solve.K, 3u);
            EXPECT_FALSE(r.warning.empty());
        }

        TEST(Settings, PartialNumericValueRejected) {
            // "3.14abc" не должно молча превратиться в 3.14.
            TempFile tmp(Tag("partial_numeric"));
            WriteFile(tmp.path(), "t_end = 3.14abc\n");

            CliOptions opts;
            opts.solve.t_end = 1.0;
            const auto r = LoadSettings(tmp.path(), opts);

            EXPECT_DOUBLE_EQ(opts.solve.t_end, 1.0);
            EXPECT_FALSE(r.warning.empty());
        }

        TEST(Settings, BadBoolValueWarns) {
            TempFile tmp(Tag("bad_bool"));
            WriteFile(tmp.path(), "enable_order_adaptation = maybe\n");

            CliOptions opts;
            opts.solve.enable_order_adaptation = false;
            const auto r = LoadSettings(tmp.path(), opts);

            EXPECT_FALSE(opts.solve.enable_order_adaptation);
            EXPECT_FALSE(r.warning.empty());
        }

        TEST(Settings, BoolVariantsAccepted) {
            const std::pair<const char*, bool> cases[] = {
                {"true",  true},  {"1", true},  {"on", true},  {"yes", true},
                {"false", false}, {"0", false}, {"off", false}, {"no", false},
            };

            for (const auto& [text, expected] : cases) {
                TempFile tmp(Tag(std::string("bool_") + text));
                WriteFile(tmp.path(),
                    std::string("enable_order_adaptation = ") + text + "\n");

                CliOptions opts;
                opts.solve.enable_order_adaptation = !expected;   // заведомо другое

                const auto r = LoadSettings(tmp.path(), opts);

                EXPECT_EQ(opts.solve.enable_order_adaptation, expected)
                    << "text = " << text;
                EXPECT_TRUE(r.warning.empty())
                    << "text = " << text << " warning = " << r.warning;
            }
        }

        // ------------------------------------------------------------------------
        // 8. Round-trip Save → Load
        // ------------------------------------------------------------------------

        TEST(Settings, RoundTripPreservesAllFields) {
            TempFile tmp(Tag("round_trip"));

            CliOptions original;
            original.solve.t_end = 12.5;
            original.solve.rtol = 1e-9;
            original.solve.atol = 1e-13;
            original.solve.h_init = 2e-3;
            original.solve.h_min = 1e-14;
            original.solve.h_max = 0.25;
            original.solve.M = 42;
            original.solve.K = 3;
            original.solve.max_steps = 12345;
            original.solve.M_min = 4;
            original.solve.M_max = 55;
            original.solve.m_factor = 7.5;
            original.solve.enable_order_adaptation = true;
            original.input_path = "in.txt";
            original.log_path = "out.log";
            original.trajectory_path = "traj.csv";

            SaveSettings(tmp.path(), original);

            CliOptions restored;
            const auto r = LoadSettings(tmp.path(), restored);
            EXPECT_TRUE(r.warning.empty()) << r.warning;

            EXPECT_DOUBLE_EQ(restored.solve.t_end, original.solve.t_end);
            EXPECT_DOUBLE_EQ(restored.solve.rtol, original.solve.rtol);
            EXPECT_DOUBLE_EQ(restored.solve.atol, original.solve.atol);
            EXPECT_DOUBLE_EQ(restored.solve.h_init, original.solve.h_init);
            EXPECT_DOUBLE_EQ(restored.solve.h_min, original.solve.h_min);
            EXPECT_DOUBLE_EQ(restored.solve.h_max, original.solve.h_max);
            EXPECT_EQ(restored.solve.M, original.solve.M);
            EXPECT_EQ(restored.solve.K, original.solve.K);
            EXPECT_EQ(restored.solve.max_steps, original.solve.max_steps);
            EXPECT_EQ(restored.solve.M_min, original.solve.M_min);
            EXPECT_EQ(restored.solve.M_max, original.solve.M_max);
            EXPECT_DOUBLE_EQ(restored.solve.m_factor, original.solve.m_factor);
            EXPECT_EQ(restored.solve.enable_order_adaptation,
                original.solve.enable_order_adaptation);
            EXPECT_EQ(restored.input_path, original.input_path);
            EXPECT_EQ(restored.log_path, original.log_path);
            EXPECT_EQ(restored.trajectory_path, original.trajectory_path);
        }

        TEST(Settings, SaveOverwritesExistingFile) {
            TempFile tmp(Tag("overwrite"));

            CliOptions first;
            first.solve.t_end = 1.0;
            first.solve.M = 10;
            SaveSettings(tmp.path(), first);

            CliOptions second;
            second.solve.t_end = 2.0;
            second.solve.M = 20;
            SaveSettings(tmp.path(), second);

            CliOptions loaded;
            LoadSettings(tmp.path(), loaded);

            EXPECT_DOUBLE_EQ(loaded.solve.t_end, 2.0);
            EXPECT_EQ(loaded.solve.M, 20u);
        }

        // ------------------------------------------------------------------------
        // 9. Граничные случаи
        // ------------------------------------------------------------------------

        TEST(Settings, EmptyFileLeavesFieldsUntouched) {
            TempFile tmp(Tag("empty_file"));
            WriteFile(tmp.path(), "");

            CliOptions opts;
            opts.solve.t_end = 7.0;
            const auto r = LoadSettings(tmp.path(), opts);

            EXPECT_TRUE(r.file_existed);
            EXPECT_FALSE(r.created_default);
            EXPECT_DOUBLE_EQ(opts.solve.t_end, 7.0);
            EXPECT_TRUE(r.warning.empty()) << r.warning;
        }

        TEST(Settings, OnlyCommentsLeavesFieldsUntouched) {
            TempFile tmp(Tag("only_comments"));
            WriteFile(tmp.path(), "# just a comment\n# and another\n");

            CliOptions opts;
            opts.solve.M = 33;
            const auto r = LoadSettings(tmp.path(), opts);

            EXPECT_EQ(opts.solve.M, 33u);
            EXPECT_TRUE(r.warning.empty()) << r.warning;
        }

        TEST(Settings, IntegerLiteralAcceptedForDoubleField) {
            TempFile tmp(Tag("int_for_double"));
            WriteFile(tmp.path(),
                "t_end = 5\n"
                "h_init = 2\n"
            );

            CliOptions opts;
            LoadSettings(tmp.path(), opts);

            EXPECT_DOUBLE_EQ(opts.solve.t_end, 5.0);
            EXPECT_DOUBLE_EQ(opts.solve.h_init, 2.0);
        }

        TEST(Settings, ExtremeButFiniteValues) {
            TempFile tmp(Tag("extremes"));
            WriteFile(tmp.path(),
                "rtol = 1e-300\n"
                "h_max = 1e300\n"
            );

            CliOptions opts;
            const auto r = LoadSettings(tmp.path(), opts);

            EXPECT_DOUBLE_EQ(opts.solve.rtol, 1e-300);
            EXPECT_DOUBLE_EQ(opts.solve.h_max, 1e300);
            EXPECT_TRUE(r.warning.empty()) << r.warning;
        }

    } // namespace
} // namespace diffuri