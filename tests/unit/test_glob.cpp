// ============================================================================
// tests/unit/test_glob.cpp
//
// Юнит-тесты модуля cli/glob: раскрытие масок файловых имён
// и формирование выходных путей с суффиксом.
//
// Структура файла:
//   1. Хелперы (TempDir, Touch, BuildPath).
//   2. HasGlobPattern.
//   3. ExpandGlob: без маски, с маской, граничные случаи.
//   4. MakeOutputPathWithSuffix.
//
// Тесты, работающие с файловой системой, создают изолированную
// временную папку на каждый тест и удаляют её в деструкторе TempDir.
// Никакого общего состояния между тестами нет — порядок запуска
// не важен.
// ============================================================================
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "cli/glob.h"

namespace diffuri {
    namespace {

        // ------------------------------------------------------------------------
        // 1. Хелперы
        // ------------------------------------------------------------------------

        // RAII-обёртка для временной директории. Создаёт уникальную папку
        // в системном temp и сносит её рекурсивно в деструкторе.
        // Использовать как:
        //     TempDir dir("glob_basic");
        //     Touch(dir.path() / "a.txt");
        //     auto files = ExpandGlob((dir.path() / "*.txt").string());
        class TempDir {
        public:
            explicit TempDir(const std::string& tag) {
                namespace fs = std::filesystem;
                // Уникальность: имя теста + счётчик. Счётчик — статический,
                // потому что имена тестов уникальны, но один тест может
                // создать несколько TempDir.
                static int counter = 0;
                path_ = fs::temp_directory_path()
                    / ("diffuri_glob_" + tag + "_" + std::to_string(++counter));
                fs::remove_all(path_);
                fs::create_directories(path_);
            }

            ~TempDir() {
                std::error_code ec;
                std::filesystem::remove_all(path_, ec);
                // Ошибку удаления молча игнорируем: если тест упал, файлы
                // могут быть залочены — это не повод падать в деструкторе.
            }

            TempDir(const TempDir&) = delete;
            TempDir& operator=(const TempDir&) = delete;

            const std::filesystem::path& path() const { return path_; }

        private:
            std::filesystem::path path_;
        };

        // Создать пустой файл по указанному пути. Родительские папки
        // создаются автоматически.
        void Touch(const std::filesystem::path& p,
            const std::string& content = "") {
            std::filesystem::create_directories(p.parent_path());
            std::ofstream f(p);
            ASSERT_TRUE(f.is_open()) << "cannot create " << p.string();
            f << content;
        }

        // Собрать путь из директории и имени файла. Возвращает строку —
        // ту, что принимает ExpandGlob.
        std::string InDir(const TempDir& dir, const std::string& name) {
            return (dir.path() / name).string();
        }

        // ------------------------------------------------------------------------
        // 2. HasGlobPattern
        // ------------------------------------------------------------------------

        TEST(Glob, HasGlobPatternEmpty) {
            EXPECT_FALSE(HasGlobPattern(""));
        }

        TEST(Glob, HasGlobPatternPlainPath) {
            EXPECT_FALSE(HasGlobPattern("data/example_input/lorenz.txt"));
            EXPECT_FALSE(HasGlobPattern("C:\\Users\\Ivan\\file.txt"));
            EXPECT_FALSE(HasGlobPattern("./relative"));
        }

        TEST(Glob, HasGlobPatternStar) {
            EXPECT_TRUE(HasGlobPattern("*.txt"));
            EXPECT_TRUE(HasGlobPattern("data/*.txt"));
            EXPECT_TRUE(HasGlobPattern("data/example_*/lorenz.txt"));
            EXPECT_TRUE(HasGlobPattern("a*b"));
        }

        TEST(Glob, HasGlobPatternQuestion) {
            EXPECT_TRUE(HasGlobPattern("?.txt"));
            EXPECT_TRUE(HasGlobPattern("file?.txt"));
            EXPECT_TRUE(HasGlobPattern("data/file?.csv"));
        }

        TEST(Glob, HasGlobPatternBoth) {
            EXPECT_TRUE(HasGlobPattern("a*b?c"));
        }

        TEST(Glob, HasGlobPatternStarInDirectoryName) {
            // '*' в имени каталога — тоже маска.
            EXPECT_TRUE(HasGlobPattern("data/*/file.txt"));
        }

        // ------------------------------------------------------------------------
        // 3. ExpandGlob
        // ------------------------------------------------------------------------

        // --- 3.1. Без маски ---

        TEST(Glob, ExpandPlainPathExistingFile) {
            TempDir dir("plain_existing");
            Touch(dir.path() / "a.txt", "hello");

            const auto result = ExpandGlob(InDir(dir, "a.txt"));
            ASSERT_EQ(result.size(), 1u);
            // Приводим к одному виду: на Windows пути могут отличаться
            // разделителями ('\\' vs '/'). Сравниваем через filesystem.
            namespace fs = std::filesystem;
            EXPECT_EQ(fs::path(result[0]).filename().string(), "a.txt");
        }

        TEST(Glob, ExpandPlainPathMissingFile) {
            TempDir dir("plain_missing");
            // Файл не создаём.
            const auto result = ExpandGlob(InDir(dir, "nope.txt"));
            EXPECT_TRUE(result.empty());
        }

        TEST(Glob, ExpandPlainPathIsDirectory) {
            TempDir dir("plain_dir");
            std::filesystem::create_directories(dir.path() / "subdir");

            // Каталог — не regular file, ExpandGlob должен его пропустить.
            const auto result = ExpandGlob(InDir(dir, "subdir"));
            EXPECT_TRUE(result.empty());
        }

        // --- 3.2. С маской, есть совпадения ---

        TEST(Glob, ExpandStarMatchesAllTxt) {
            TempDir dir("star_all");
            Touch(dir.path() / "a.txt");
            Touch(dir.path() / "b.txt");
            Touch(dir.path() / "c.txt");
            Touch(dir.path() / "readme.md");  // не подходит

            const auto result = ExpandGlob(InDir(dir, "*.txt"));
            ASSERT_EQ(result.size(), 3u);
            for (const auto& p : result) {
                EXPECT_EQ(std::filesystem::path(p).extension().string(),
                    ".txt");
            }
        }

        TEST(Glob, ExpandStarReturnsSortedOrder) {
            TempDir dir("star_sorted");
            // Создаём в «неправильном» порядке — результат всё равно
            // должен быть лексикографически отсортирован.
            Touch(dir.path() / "z.txt");
            Touch(dir.path() / "a.txt");
            Touch(dir.path() / "m.txt");

            const auto result = ExpandGlob(InDir(dir, "*.txt"));
            ASSERT_EQ(result.size(), 3u);
            EXPECT_EQ(std::filesystem::path(result[0]).filename().string(),
                "a.txt");
            EXPECT_EQ(std::filesystem::path(result[1]).filename().string(),
                "m.txt");
            EXPECT_EQ(std::filesystem::path(result[2]).filename().string(),
                "z.txt");
        }

        TEST(Glob, ExpandQuestionMatchesSingleChar) {
            TempDir dir("question");
            Touch(dir.path() / "a.txt");
            Touch(dir.path() / "b.txt");
            Touch(dir.path() / "ab.txt");  // не подходит

            const auto result = ExpandGlob(InDir(dir, "?.txt"));
            ASSERT_EQ(result.size(), 2u);
            for (const auto& p : result) {
                EXPECT_EQ(std::filesystem::path(p).stem().string().size(), 1u);
            }
        }

        TEST(Glob, ExpandStarMatchesEmptySubstring) {
            TempDir dir("star_empty");
            Touch(dir.path() / "a.txt");
            Touch(dir.path() / "file.txt");

            // "*.txt" должна матчить "a.txt" (звёздочка = "a")
            // и "file.txt" (звёздочка = "file").
            const auto result = ExpandGlob(InDir(dir, "*.txt"));
            EXPECT_EQ(result.size(), 2u);
        }

        TEST(Glob, ExpandStarInMiddle) {
            TempDir dir("star_middle");
            Touch(dir.path() / "test_lorenz_1.txt");
            Touch(dir.path() / "test_lorenz_2.txt");
            Touch(dir.path() / "test_other.txt");

            const auto result = ExpandGlob(InDir(dir, "test_lorenz_*.txt"));
            ASSERT_EQ(result.size(), 2u);
        }

        TEST(Glob, ExpandMultipleStars) {
            TempDir dir("multi_star");
            Touch(dir.path() / "a_b_c.txt");
            Touch(dir.path() / "a_x_c.txt");
            Touch(dir.path() / "a_b_d.txt");

            const auto result = ExpandGlob(InDir(dir, "a_*_*.txt"));
            ASSERT_EQ(result.size(), 3u);
        }

        // --- 3.3. Маска без совпадений ---

        TEST(Glob, ExpandStarNoMatch) {
            TempDir dir("star_no_match");
            Touch(dir.path() / "a.txt");
            Touch(dir.path() / "b.txt");

            const auto result = ExpandGlob(InDir(dir, "*.csv"));
            EXPECT_TRUE(result.empty());
        }

        TEST(Glob, ExpandMaskInMissingDirectory) {
            TempDir dir("missing_dir");
            // Подкаталога no_such нет.
            const auto result = ExpandGlob(
                InDir(dir, "no_such/*.txt"));
            EXPECT_TRUE(result.empty());
        }

        // --- 3.4. Маска не цепляет каталоги и прочее ---

        TEST(Glob, ExpandStarSkipsDirectories) {
            TempDir dir("skip_dirs");
            Touch(dir.path() / "a.txt");
            std::filesystem::create_directories(dir.path() / "fake.txt");

            const auto result = ExpandGlob(InDir(dir, "*.txt"));
            ASSERT_EQ(result.size(), 1u);
            EXPECT_EQ(std::filesystem::path(result[0]).filename().string(),
                "a.txt");
        }

        // --- 3.5. Относительные пути и путь без каталога ---

        TEST(Glob, ExpandBareStarUsesCurrentDirectory) {
            // "*.txt" без каталога — dir по умолчанию ".".
            // Здесь проверяем только то, что функция не падает и не
            // возвращает мусор. Конкретные совпадения зависят от CWD,
            // поэтому проверяем аккуратно.
            const auto result = ExpandGlob("*.definitely_no_such_ext");
            EXPECT_TRUE(result.empty());
        }

        TEST(Glob, ExpandExactFilenameWithStarOnlyPattern) {
            TempDir dir("single_star");
            Touch(dir.path() / "only.txt");

            const auto result = ExpandGlob(InDir(dir, "*"));
            // "*" матчит любое имя, в папке один файл.
            EXPECT_EQ(result.size(), 1u);
        }

        // --- 3.6. Много файлов ---

        TEST(Glob, ExpandManyFiles) {
            TempDir dir("many");
            for (int i = 0; i < 20; ++i) {
                Touch(dir.path() / ("f" + std::to_string(i) + ".txt"));
            }
            Touch(dir.path() / "skip.md");

            const auto result = ExpandGlob(InDir(dir, "*.txt"));
            EXPECT_EQ(result.size(), 20u);
        }

        // ------------------------------------------------------------------------
        // 4. MakeOutputPathWithSuffix
        // ------------------------------------------------------------------------

        TEST(Glob, SuffixBasicCsv) {
            EXPECT_EQ(
                MakeOutputPathWithSuffix("out.csv", "lorenz.txt"),
                "out_lorenz.csv");
        }

        TEST(Glob, SuffixWithFullPathInInput) {
            EXPECT_EQ(
                MakeOutputPathWithSuffix(
                    "out.csv",
                    "data/example_input/lorenz.txt"),
                "out_lorenz.csv");
        }

        TEST(Glob, SuffixWithWindowsBackslashes) {
            // На Windows std::filesystem::path корректно разберёт
            // обратные слэши. На POSIX — нет (там '\' — обычный символ
            // в имени). Поэтому тест условный.
#ifdef _WIN32
            EXPECT_EQ(
                MakeOutputPathWithSuffix(
                    "out.csv",
                    "data\\example_input\\lorenz.txt"),
                "out_lorenz.csv");
#endif
        }

        TEST(Glob, SuffixPreservesExtension) {
            EXPECT_EQ(
                MakeOutputPathWithSuffix("report.log", "vdp.txt"),
                "report_vdp.log");
            EXPECT_EQ(
                MakeOutputPathWithSuffix("data.csv", "sys.txt"),
                "data_sys.csv");
        }

        TEST(Glob, SuffixNoExtensionInBase) {
            EXPECT_EQ(
                MakeOutputPathWithSuffix("out", "lorenz.txt"),
                "out_lorenz");
        }

        TEST(Glob, SuffixMultiDotBasename) {
            // "out.v2.csv" — stem = "out.v2", ext = ".csv".
            EXPECT_EQ(
                MakeOutputPathWithSuffix("out.v2.csv", "lorenz.txt"),
                "out.v2_lorenz.csv");
        }

        TEST(Glob, SuffixKeepsParentDirectory) {
            EXPECT_EQ(
                MakeOutputPathWithSuffix(
                    "results/out.csv", "lorenz.txt"),
                "results/out_lorenz.csv");
        }

        TEST(Glob, SuffixEmptyBaseReturnsEmpty) {
            EXPECT_EQ(MakeOutputPathWithSuffix("", "lorenz.txt"), "");
        }

        TEST(Glob, SuffixEmptyInputReturnsBase) {
            EXPECT_EQ(MakeOutputPathWithSuffix("out.csv", ""), "out.csv");
        }

        TEST(Glob, SuffixBothEmpty) {
            EXPECT_EQ(MakeOutputPathWithSuffix("", ""), "");
        }

        TEST(Glob, SuffixInputWithoutExtension) {
            EXPECT_EQ(
                MakeOutputPathWithSuffix("out.csv", "lorenz"),
                "out_lorenz.csv");
        }

        TEST(Glob, SuffixInputWithMultipleDots) {
            // "a.b.c.txt" — stem = "a.b.c", суффикс = "a.b.c".
            EXPECT_EQ(
                MakeOutputPathWithSuffix("out.csv", "a.b.c.txt"),
                "out_a.b.c.csv");
        }

        TEST(Glob, SuffixFullAbsolutePaths) {
            namespace fs = std::filesystem;
            const fs::path base = fs::temp_directory_path() / "out.csv";
            const fs::path input = fs::temp_directory_path() / "lorenz.txt";
            const auto result = MakeOutputPathWithSuffix(
                base.string(), input.string());
            EXPECT_EQ(fs::path(result).filename().string(),
                "out_lorenz.csv");
            EXPECT_EQ(fs::path(result).parent_path(), base.parent_path());
        }

    } // namespace
} // namespace diffuri