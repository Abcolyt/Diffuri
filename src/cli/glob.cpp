// ============================================================================
// src/cli/glob.cpp
//
// Реализация модуля cli/glob: раскрытие масок файловых имён.
//
// Структура файла:
//   1. Анонимный namespace: локальные утилиты (MatchMask, BaselineName).
//   2. Реализация исключений (отсутствуют).
//   3. Реализация публичных функций (HasGlobPattern, ExpandGlob,
//      MakeOutputPathWithSuffix).
//
// Все возвращаемые пути — в универсальном формате с '/' в качестве
// разделителя (std::filesystem::path::generic_string). Это даёт
// одинаковый результат на Windows и POSIX, а также одинаковый вывод
// в логах и CSV. std::ofstream и std::ifstream принимают '/' на
// Windows так же, как '\\', поэтому работоспособность не страдает.
// ============================================================================
#include "cli/glob.h"

// --- Стандартная библиотека (по алфавиту) ---
#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

namespace diffuri {

    // ============================================================================
    // 1. АНОНИМНЫЙ NAMESPACE: локальные утилиты
    // ============================================================================
    namespace {

        // Сопоставление имени с маской. Поддерживаются '*' (любая
        // последовательность) и '?' (ровно один символ). Классический
        // итеративный алгоритм с одной точкой возврата — без рекурсии,
        // O(n*m) в худшем случае, O(n) в типичном.
        bool MatchMask(const std::string& name, const std::string& mask) {
            std::size_t ni = 0;               // позиция в name
            std::size_t mi = 0;               // позиция в mask
            std::size_t star_ni = std::string::npos;  // последний '*' в name
            std::size_t star_mi = 0;          // ... и его позиция в mask

            while (ni < name.size()) {
                const bool match_one =
                    mi < mask.size() &&
                    (mask[mi] == '?' || mask[mi] == name[ni]);

                if (match_one) {
                    ++ni;
                    ++mi;
                }
                else if (mi < mask.size() && mask[mi] == '*') {
                    star_mi = mi++;
                    star_ni = ni;
                }
                else if (star_ni != std::string::npos) {
                    // Откат: '*' поглощает ещё один символ name.
                    mi = star_mi + 1;
                    ni = ++star_ni;
                }
                else {
                    return false;
                }
            }

            // Съедаем хвостовые '*' в маске: "*" должна матчить "".
            while (mi < mask.size() && mask[mi] == '*') ++mi;
            return mi == mask.size();
        }

        // Имя файла без пути и без расширения: "a/b/lorenz.txt" → "lorenz".
        // Нужно для MakeOutputPathWithSuffix. Возвращает компоненту имени —
        // разделителей в ней нет, платформенная зависимость невозможна.
        std::string BaselineName(const std::string& path) {
            std::filesystem::path p(path);
            return p.stem().string();
        }

    } // namespace

    // ============================================================================
    // 2. РЕАЛИЗАЦИЯ ИСКЛЮЧЕНИЙ
    // ============================================================================
    // (В этом модуле исключения не бросаются)

    // ============================================================================
    // 3. РЕАЛИЗАЦИЯ ПУБЛИЧНЫХ ФУНКЦИЙ
    // ============================================================================

    bool HasGlobPattern(const std::string& path) {
        return path.find('*') != std::string::npos
            || path.find('?') != std::string::npos;
    }

    std::vector<std::string> ExpandGlob(const std::string& pattern) {
        namespace fs = std::filesystem;

        // Если маски нет — ведём себя как обычный путь: возвращаем его
        // в единственном экземпляре, но только если файл существует.
        // Это упрощает вызывающий код: ему не нужно ветвиться на «маска/не маска».
        if (!HasGlobPattern(pattern)) {
            std::error_code ec;
            if (fs::is_regular_file(pattern, ec)) {
                // Нормализуем разделители даже для одиночного пути:
                // пользователь мог передать "data\\file.txt" — вернём
                // "data/file.txt", чтобы вывод был единообразным.
                return { fs::path(pattern).generic_string() };
            }
            return {};
        }

        fs::path p(pattern);
        fs::path dir = p.parent_path();
        const std::string mask = p.filename().string();

        if (dir.empty()) dir = ".";

        std::error_code ec;
        if (!fs::exists(dir, ec) || !fs::is_directory(dir, ec)) {
            return {};
        }

        std::vector<std::string> result;
        for (const auto& entry : fs::directory_iterator(dir, ec)) {
            if (ec) break;
            if (!entry.is_regular_file()) continue;

            const std::string name = entry.path().filename().string();
            if (MatchMask(name, mask)) {
                // generic_string() — всегда '/', независимо от платформы.
                // Дальше этот путь уходит в лог и в grid-имена: одинаковый
                // вид на Windows и Linux удобнее читать и сравнивать.
                result.push_back(entry.path().generic_string());
            }
        }

        // Порядок прогонов должен быть детерминированным: одинаковый
        // sweep.csv на разных машинах. fs::directory_iterator порядка
        // не гарантирует — сортируем сами.
        std::sort(result.begin(), result.end());
        return result;
    }

    std::string MakeOutputPathWithSuffix(const std::string& base_path,
        const std::string& input_path) {
        if (base_path.empty()) return {};
        if (input_path.empty()) return base_path;

        const std::string suffix = BaselineName(input_path);
        if (suffix.empty()) return base_path;

        std::filesystem::path p(base_path);
        const std::string stem = p.stem().string();
        const std::string ext = p.extension().string();

        std::filesystem::path parent = p.parent_path();
        std::string new_name = stem + "_" + suffix + ext;

        if (parent.empty()) return new_name;
        // generic_string() — всегда '/'. Тест Glob.SuffixKeepsParentDirectory
        // ожидает "results/out_lorenz.csv" независимо от платформы;
        // std::ofstream принимает '/' и на Windows.
        return (parent / new_name).generic_string();
    }

} // namespace diffuri