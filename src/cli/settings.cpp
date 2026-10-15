// ============================================================================
// src/cli/settings.cpp
//
// Реализация модуля cli/settings: чтение и запись пользовательских настроек.
//
// Покрывает все поля CliOptions:
//   - SolveOptions (13 полей: t_end, rtol, atol, h_init, h_min, h_max,
//     M, K, max_steps, M_min, M_max, m_factor, enable_order_adaptation);
//   - пути (input, log, trajectory);
//     для input: "-" и пустая строка оба означают stdin;
//   - sweep-режим (sweep_spec, sweep_metric, sweep_out_path).
//
// Структура файла:
//   1. Анонимный namespace: локальные утилиты (Trim, ParseDouble, ParseSize,
//      ParseBool).
//   2. Реализация исключений (отсутствуют — ошибки возвращаются в warning).
//   3. Реализация публичных функций (DefaultSettingsPath, SaveSettings,
//      LoadSettings).
// ============================================================================
#include "cli/settings.h"

// --- Стандартная библиотека (по алфавиту) ---
#include <cstddef>
#include <fstream>
#include <sstream>
#include <string>

namespace diffuri {

    // ============================================================================
    // 1. АНОНИМНЫЙ NAMESPACE: локальные утилиты
    // ============================================================================
    namespace {

        constexpr const char* kDefaultSettingsPath = "data/settings.txt";

        // Обрезает пробельные символы с обоих концов. Возвращает пустую строку,
        // если строка состоит только из пробелов.
        std::string Trim(const std::string& s) {
            const auto first = s.find_first_not_of(" \t\r\n");
            if (first == std::string::npos) return {};
            const auto last = s.find_last_not_of(" \t\r\n");
            return s.substr(first, last - first + 1);
        }

        // Разбор double: значение должно занимать всю строку, иначе false.
        // Используется stod, а не from_chars, ради совместимости с MSVC 2019.
        // Проверка pos == text.size() отсекает "3.14abc": stod разобрал бы
        // префикс, но такое значение — почти наверняка опечатка, и молча
        // принять его хуже, чем вернуть warning.
        bool ParseDouble(const std::string& text, double& out) {
            try {
                std::size_t pos = 0;
                const double value = std::stod(text, &pos);
                if (pos != text.size()) return false;
                out = value;
                return true;
            }
            catch (...) {
                return false;
            }
        }

        // Разбор std::size_t. Отрицательные значения отсекаются самим stoull:
        // "-5" он разберёт как большое положительное, поэтому явно проверяем
        // ведущий минус — так честнее диагностика.
        bool ParseSize(const std::string& text, std::size_t& out) {
            if (!text.empty() && text.front() == '-') return false;
            try {
                std::size_t pos = 0;
                const auto value = std::stoull(text, &pos);
                if (pos != text.size()) return false;
                out = static_cast<std::size_t>(value);
                return true;
            }
            catch (...) {
                return false;
            }
        }

        // Разбор bool: принимаем true/1/on/yes и false/0/off/no в нижнем
        // регистре. Регистр не трогаем — файл настроек пишется машиной
        // через SaveSettings, разнообразие вариантов нужно только для
        // правок руками.
        bool ParseBool(const std::string& text, bool& out) {
            if (text == "true" || text == "1" || text == "on" || text == "yes") {
                out = true;
                return true;
            }
            if (text == "false" || text == "0" || text == "off" || text == "no") {
                out = false;
                return true;
            }
            return false;
        }

        // Нормализация значения ключа input: "-" и пустое значение оба означают
        // «читать систему из stdin». Каноническое представление в CliOptions —
        // пустая строка (она же — значение по умолчанию).
        std::string NormalizeInputPath(const std::string& value) {
            return (value == "-") ? std::string{} : value;
        }

    } // namespace

    // ============================================================================
    // 2. РЕАЛИЗАЦИЯ ИСКЛЮЧЕНИЙ
    // ============================================================================
    // (В этом модуле исключения не бросаются: ошибки ввода-вывода и опечатки
    //  в ключах возвращаются через SettingsLoadResult::warning, а SaveSettings
    //  работает best-effort и молча выходит, если файл не открылся)

    // ============================================================================
    // 3. РЕАЛИЗАЦИЯ ПУБЛИЧНЫХ ФУНКЦИЙ
    // ============================================================================

    std::string DefaultSettingsPath() {
        return kDefaultSettingsPath;
    }

    void SaveSettings(const std::string& path, const CliOptions& opts) {
        // Best-effort: если файл не открылся — тихо выходим. Файл настроек
        // не критичен для работы пайплайна, падать из-за него нельзя.
        std::ofstream f(path);
        if (!f) return;

        const auto& s = opts.solve;
        f << "# Diffuri settings (key = value).\n";
        f << "# Читается при запуске в интерактивном режиме (двойной клик).\n";
        f << "# Строки, начинающиеся с '#', игнорируются.\n";
        f << "\n";
        f << "# --- Параметры интегрирования ---\n";
        f << "t_end = " << s.t_end << "\n";
        f << "rtol = " << s.rtol << "\n";
        f << "atol = " << s.atol << "\n";
        f << "h_init = " << s.h_init << "\n";
        f << "h_min = " << s.h_min << "\n";
        f << "h_max = " << s.h_max << "\n";
        f << "M = " << s.M << "\n";
        f << "K = " << s.K << "\n";
        f << "max_steps = " << s.max_steps << "\n";
        f << "M_min = " << s.M_min << "\n";
        f << "M_max = " << s.M_max << "\n";
        f << "m_factor = " << s.m_factor << "\n";
        f << "enable_order_adaptation = "
            << (s.enable_order_adaptation ? "true" : "false") << "\n";
        f << "\n";
        f << "# --- Пути ---\n";
        f << "# '-' или пустое значение = читать систему из stdin.\n";
        f << "input = "
            << (opts.input_path.empty() ? "-" : opts.input_path) << "\n";
        f << "log = " << opts.log_path << "\n";
        f << "trajectory = " << opts.trajectory_path << "\n";
        f << "\n";
        f << "# --- Sweep-режим ---\n";
        f << "# sweep_spec пуст — sweep выключен.\n";
        f << "sweep_spec = " << opts.sweep_spec << "\n";
        f << "sweep_metric = " << opts.sweep_metric << "\n";
        f << "sweep_out_path = " << opts.sweep_out_path << "\n";
    }

    SettingsLoadResult LoadSettings(const std::string& path, CliOptions& opts) {
        SettingsLoadResult result;
        result.path = path;

        std::ifstream f(path);
        if (!f) {
            // Первый запуск: файла нет — создаём с текущими значениями.
            // Это и есть ответ на «двойной клик»: пользователь получает
            // готовый файл с комментариями, который можно править.
            SaveSettings(path, opts);
            result.file_existed = false;
            result.created_default = true;
            return result;
        }

        result.file_existed = true;
        std::ostringstream warnings;
        std::string line;
        int line_no = 0;
        auto& s = opts.solve;

        while (std::getline(f, line)) {
            ++line_no;

            // getline оставляет '\r' при чтении CRLF-файла — Trim его съест.
            const auto trimmed = Trim(line);
            if (trimmed.empty() || trimmed[0] == '#') continue;

            const auto eq = trimmed.find('=');
            if (eq == std::string::npos) {
                warnings << "line " << line_no
                    << ": нет '=' — строка пропущена\n";
                continue;
            }
            const auto key = Trim(trimmed.substr(0, eq));
            const auto value = Trim(trimmed.substr(eq + 1));

            // Разбор идёт во временную переменную, а не сразу в поле opts:
            // неудачная строка не должна портить уже применённые значения.
            // Для строковых полей это неактуально (значение присваивается
            // как есть), но для числовых — принципиально.
            bool ok = true;
            if (key == "t_end")       ok = ParseDouble(value, s.t_end);
            else if (key == "rtol")        ok = ParseDouble(value, s.rtol);
            else if (key == "atol")        ok = ParseDouble(value, s.atol);
            else if (key == "h_init")      ok = ParseDouble(value, s.h_init);
            else if (key == "h_min")       ok = ParseDouble(value, s.h_min);
            else if (key == "h_max")       ok = ParseDouble(value, s.h_max);
            else if (key == "m_factor")    ok = ParseDouble(value, s.m_factor);
            else if (key == "M")           ok = ParseSize(value, s.M);
            else if (key == "K")           ok = ParseSize(value, s.K);
            else if (key == "max_steps")   ok = ParseSize(value, s.max_steps);
            else if (key == "M_min")       ok = ParseSize(value, s.M_min);
            else if (key == "M_max")       ok = ParseSize(value, s.M_max);
            else if (key == "enable_order_adaptation")
                ok = ParseBool(value, s.enable_order_adaptation);
            else if (key == "input")
                opts.input_path = NormalizeInputPath(value);
            else if (key == "log")             opts.log_path = value;
            else if (key == "trajectory")      opts.trajectory_path = value;
            else if (key == "sweep_spec")      opts.sweep_spec = value;
            else if (key == "sweep_metric")    opts.sweep_metric = value;
            else if (key == "sweep_out_path")  opts.sweep_out_path = value;
            else {
                warnings << "line " << line_no
                    << ": неизвестный ключ '" << key << "'\n";
                continue;
            }

            if (!ok) {
                warnings << "line " << line_no
                    << ": не удалось разобрать значение для '" << key
                    << "': '" << value << "'\n";
            }
        }

        result.warning = warnings.str();
        return result;
    }

} // namespace diffuri