// ============================================================================
// src/cli/cli.cpp
//
// Реализация разбора аргументов командной строки и утилит вывода.
//
// Структура файла:
//   1. Анонимный namespace: локальные утилиты (хелперы разбора,
//      хелперы TUI: TrimCopy, ToLowerCopy, FormatDouble, ParseBool,
//      ParseNonNegativeInt, PrintSettings, ApplyPair, ApplySettingsLine).
//   2. Реализация исключений (отсутствуют).
//   3. Реализация публичных функций (ParseCliArgs, PrintUsage,
//      SaveTrajectory, IsStdinInteractive, ShouldUseInteractiveMode,
//      PromptSettings, PressEnterToExit).
//
// Все сообщения TUI — на английском: консоль Windows по умолчанию
// работает в CP866/CP1251, а исходник в UTF-8, поэтому русские строки
// отображаются как «╨╕╨╜╤В╨╡╤А...». Английский в ASCII-диапазоне
// отображается корректно независимо от кодовой страницы.
// ============================================================================
#include "cli/cli.h"

// --- Стандартная библиотека (по алфавиту) ---
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

// --- Платформенные заголовки для детекта TTY ---
#ifdef _WIN32
#  include <io.h>      // _isatty, _fileno
#else
#  include <unistd.h>  // isatty, fileno
#endif

namespace diffuri {

    // ============================================================================
    // 1. АНОНИМНЫЙ NAMESPACE: локальные утилиты
    // ============================================================================
    namespace {

        // Текущая позиция в последовательности позиционных аргументов.
        enum class Pos { TEnd, M, HInit, LogPath, Done };

        // Проверить, что значение допустимо как точность (конечное, > 0).
        // Возвращает true, если значение корректно.
        bool IsValidTolerance(double v) {
            return std::isfinite(v) && v > 0.0;
        }

        // Попытаться разобрать double из строки.
        // Возвращает true при успехе, записывает результат в out.
        bool ParseDouble(const std::string& s, double& out) {
            try {
                std::size_t pos = 0;
                out = std::stod(s, &pos);
                // Проверяем, что вся строка была потреблена.
                return pos == s.size();
            }
            catch (const std::exception&) {
                return false;
            }
        }

        // Попытаться разобрать целое из строки.
        // Возвращает true при успехе, записывает результат в out.
        bool ParseSizeT(const std::string& s, std::size_t& out) {
            try {
                std::size_t pos = 0;
                out = std::stoul(s, &pos);
                return pos == s.size();
            }
            catch (const std::exception&) {
                return false;
            }
        }

        // --- Хелперы TUI ------------------------------------------------

        // Обрезать пробелы с обоих концов.
        std::string TrimCopy(const std::string& s) {
            std::size_t b = 0;
            std::size_t e = s.size();
            while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
            while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
            return s.substr(b, e - b);
        }

        // Привести строку к нижнему регистру.
        std::string ToLowerCopy(const std::string& s) {
            std::string r = s;
            for (char& c : r) {
                c = static_cast<char>(
                    std::tolower(static_cast<unsigned char>(c)));
            }
            return r;
        }

        // Печать double для TUI: без хвостовых нулей, 15 значащих цифр.
        std::string FormatDouble(double v) {
            std::ostringstream os;
            os << std::setprecision(15) << v;
            return os.str();
        }

        // Разобрать bool: true/false/yes/no/on/off/1/0 (регистронезависимо).
        bool ParseBool(const std::string& s, bool& out) {
            const std::string t = ToLowerCopy(s);
            if (t == "true" || t == "1" || t == "yes" || t == "on") {
                out = true;
                return true;
            }
            if (t == "false" || t == "0" || t == "no" || t == "off") {
                out = false;
                return true;
            }
            return false;
        }

        // Разобрать неотрицательное целое (только цифры, без знака).
        bool ParseNonNegativeInt(const std::string& s, std::size_t& out) {
            if (s.empty()) return false;
            for (char c : s) {
                if (!std::isdigit(static_cast<unsigned char>(c))) return false;
            }
            try {
                std::size_t pos = 0;
                const unsigned long long v = std::stoull(s, &pos);
                if (pos != s.size()) return false;
                out = static_cast<std::size_t>(v);
                return true;
            }
            catch (const std::exception&) {
                return false;
            }
        }

        // Печать таблицы настроек. Один и тот же формат используется и
        // до правки, и после — чтобы изменения были очевидны.
        void PrintSettings(const CliOptions& opts, std::ostream& os) {
            const SolveOptions& s = opts.solve;
            os << "  t_end                   = " << FormatDouble(s.t_end) << "\n";
            os << "  M                       = " << s.M << "\n";
            os << "  K                       = " << s.K << "\n";
            os << "  h_init                  = " << FormatDouble(s.h_init) << "\n";
            os << "  h_min                   = " << FormatDouble(s.h_min) << "\n";
            os << "  h_max                   = " << FormatDouble(s.h_max) << "\n";
            os << "  rtol                    = " << FormatDouble(s.rtol) << "\n";
            os << "  atol                    = " << FormatDouble(s.atol) << "\n";
            os << "  max_steps               = " << s.max_steps << "\n";
            os << "  M_min                   = " << s.M_min << "\n";
            os << "  M_max                   = " << s.M_max << "\n";
            os << "  m_factor                = " << FormatDouble(s.m_factor) << "\n";
            os << "  enable_order_adaptation = "
                << (s.enable_order_adaptation ? "true" : "false") << "\n";
            os << "  trajectory              = "
                << (opts.trajectory_path.empty()
                    ? std::string("(not set)")
                    : opts.trajectory_path) << "\n";
            os << "  log                     = "
                << (opts.log_path.empty()
                    ? std::string("(not set)")
                    : opts.log_path) << "\n";
        }

        // Применить одну пару key=value к opts. false + текст в err при ошибке.
        bool ApplyPair(const std::string& key, const std::string& value,
            CliOptions& opts, std::string& err) {
            // --- double-поля ---
            if (key == "t_end" || key == "h_init" || key == "h_min"
                || key == "h_max" || key == "m_factor") {
                double v = 0.0;
                if (!ParseDouble(value, v)) {
                    err = key + ": expected a number, got '" + value + "'";
                    return false;
                }
                if (key == "t_end")    opts.solve.t_end = v;
                if (key == "h_init")   opts.solve.h_init = v;
                if (key == "h_min")    opts.solve.h_min = v;
                if (key == "h_max")    opts.solve.h_max = v;
                if (key == "m_factor") opts.solve.m_factor = v;
                return true;
            }
            // --- точности: конечные и > 0 ---
            if (key == "rtol" || key == "atol") {
                double v = 0.0;
                if (!ParseDouble(value, v) || !IsValidTolerance(v)) {
                    err = key + ": expected a positive finite number";
                    return false;
                }
                if (key == "rtol") opts.solve.rtol = v;
                else               opts.solve.atol = v;
                return true;
            }
            // --- size_t-поля ---
            if (key == "M" || key == "K" || key == "max_steps"
                || key == "M_min" || key == "M_max") {
                std::size_t v = 0;
                if (!ParseNonNegativeInt(value, v)) {
                    err = key + ": expected a non-negative integer";
                    return false;
                }
                if (key == "M")         opts.solve.M = v;
                if (key == "K")         opts.solve.K = v;
                if (key == "max_steps") opts.solve.max_steps = v;
                if (key == "M_min")     opts.solve.M_min = v;
                if (key == "M_max")     opts.solve.M_max = v;
                return true;
            }
            // --- bool ---
            if (key == "enable_order_adaptation") {
                bool v = false;
                if (!ParseBool(value, v)) {
                    err = "enable_order_adaptation: expected true/false "
                        "(or yes/no, on/off, 1/0)";
                    return false;
                }
                opts.solve.enable_order_adaptation = v;
                return true;
            }
            // --- строки (пути). Пустое значение key= допустимо (очищает путь). ---
            if (key == "trajectory") {
                opts.trajectory_path = value;
                return true;
            }
            if (key == "log") {
                opts.log_path = value;
                return true;
            }
            err = "unknown key '" + key + "'";
            return false;
        }

        // Разобрать строку «key=value key2=value2 ...».
        bool ApplySettingsLine(const std::string& line, CliOptions& opts,
            std::string& err) {
            std::istringstream is(line);
            std::string token;
            while (is >> token) {
                const auto eq = token.find('=');
                if (eq == std::string::npos) {
                    err = "missing '=' in token '" + token + "'";
                    return false;
                }
                const std::string key = token.substr(0, eq);
                const std::string value = token.substr(eq + 1);
                if (!ApplyPair(key, value, opts, err)) return false;
            }
            return true;
        }

    } // namespace

    // ============================================================================
    // 2. РЕАЛИЗАЦИЯ ИСКЛЮЧЕНИЙ
    // ============================================================================
    // (В этом модуле исключения пробрасываются из нижележащих модулей)

    // ============================================================================
    // 3. РЕАЛИЗАЦИЯ ПУБЛИЧНЫХ ФУНКЦИЙ
    // ============================================================================

    CliParseResult ParseCliArgs(int argc, char** argv) {
        CliParseResult out;
        Pos pos = Pos::TEnd;

        for (int i = 1; i < argc; ++i) {
            const std::string a = argv[i];

            // --- Именованные флаги (могут стоять в любом месте) ---

            if (a == "--help" || a == "-h") {
                out.status = CliParseStatus::Help;
                return out;
            }

            if (a == "--trajectory") {
                if (i + 1 >= argc) {
                    out.status = CliParseStatus::Error;
                    out.error = "--trajectory requires a path";
                    return out;
                }
                out.options.trajectory_path = argv[++i];
                continue;
            }

            if (a == "--rtol") {
                if (i + 1 >= argc) {
                    out.status = CliParseStatus::Error;
                    out.error = "--rtol requires a value";
                    return out;
                }
                double v = 0.0;
                if (!ParseDouble(argv[++i], v) || !IsValidTolerance(v)) {
                    out.status = CliParseStatus::Error;
                    out.error = "--rtol: expected a positive finite number";
                    return out;
                }
                out.options.solve.rtol = v;
                continue;
            }

            if (a == "--atol") {
                if (i + 1 >= argc) {
                    out.status = CliParseStatus::Error;
                    out.error = "--atol requires a value";
                    return out;
                }
                double v = 0.0;
                if (!ParseDouble(argv[++i], v) || !IsValidTolerance(v)) {
                    out.status = CliParseStatus::Error;
                    out.error = "--atol: expected a positive finite number";
                    return out;
                }
                out.options.solve.atol = v;
                continue;
            }

            // Неизвестный флаг.
            if (!a.empty() && a[0] == '-') {
                out.status = CliParseStatus::Error;
                out.error = "unknown option: " + a;
                return out;
            }

            // --- Позиционные аргументы (в порядке появления) ---

            switch (pos) {
            case Pos::TEnd: {
                double v = 0.0;
                if (!ParseDouble(a, v)) {
                    out.status = CliParseStatus::Error;
                    out.error = "t_end: bad numeric value '" + a + "'";
                    return out;
                }
                out.options.solve.t_end = v;
                pos = Pos::M;
                break;
            }
            case Pos::M: {
                std::size_t v = 0;
                if (!ParseSizeT(a, v)) {
                    out.status = CliParseStatus::Error;
                    out.error = "M: bad integer value '" + a + "'";
                    return out;
                }
                out.options.solve.M = v;
                pos = Pos::HInit;
                break;
            }
            case Pos::HInit: {
                double v = 0.0;
                if (!ParseDouble(a, v)) {
                    out.status = CliParseStatus::Error;
                    out.error = "h_init: bad numeric value '" + a + "'";
                    return out;
                }
                out.options.solve.h_init = v;
                pos = Pos::LogPath;
                break;
            }
            case Pos::LogPath:
                out.options.log_path = a;
                pos = Pos::Done;
                break;
            case Pos::Done:
                out.status = CliParseStatus::Error;
                out.error = "too many positional arguments";
                return out;
            }
        }
        return out;  // status == Ok
    }

    void PrintUsage(std::ostream& os, const std::string& prog) {
        os <<
            "Usage: " << prog << " [options] [t_end] [M] [h_init] [log_path]\n"
            "\n"
            "Reads an ODE system from stdin until EOF and runs the pipeline\n"
            "(Parse -> Validate -> Normalize -> ReduceOrder -> Autonomize\n"
            " -> Polynomize -> Quadratize -> Solve).\n"
            "\n"
            "Options:\n"
            "  --trajectory PATH   save full trajectory to CSV\n"
            "  --rtol VALUE        relative tolerance (default 1e-10)\n"
            "  --atol VALUE        absolute tolerance (default 1e-12)\n"
            "  --help, -h          this message\n"
            "\n"
            "Positional:\n"
            "  t_end     final time (default 1)\n"
            "  M         Taylor order (default 20)\n"
            "  h_init    initial step (default 1e-3)\n"
            "  log_path  optional path for the full pipeline report\n"
            "\n"
            "Interactive TUI:\n"
            "  Run with no arguments in an interactive console (double-click\n"
            "  the executable) to open the settings screen before the run.\n";
    }

    void SaveTrajectory(const Solution& sol,
        const std::vector<std::string>& visible_functions,
        const std::string& path) {
        if (sol.points.empty()) {
            throw SolverError("SaveTrajectory: solution has no points");
        }

        // --- 1. Поиск индексов функций в sol.functions ---
        std::vector<std::size_t> indices;
        indices.reserve(visible_functions.size());
        for (const auto& name : visible_functions) {
            auto it = std::find(sol.functions.begin(),
                sol.functions.end(),
                name);
            if (it == sol.functions.end()) {
                throw SolverError("SaveTrajectory: function '" + name +
                    "' not found in solution");
            }
            indices.push_back(static_cast<std::size_t>(
                std::distance(sol.functions.begin(), it)));
        }

        // --- 2. Открытие файла ---
        std::ofstream f(path);
        if (!f) {
            throw SolverError("SaveTrajectory: cannot open '" + path + "'");
        }

        // --- 3. Запись содержимого ---
        f << std::setprecision(17);

        // Заголовок: колонки функций.
        f << 't';
        for (const auto& name : visible_functions) f << ',' << name;
        f << '\n';

        // Строки данных: по одной на точку траектории.
        for (const auto& pt : sol.points) {
            f << pt.t;
            for (std::size_t i : indices) {
                if (i >= pt.x.size()) {
                    throw SolverError(
                        "SaveTrajectory: solution point has too few "
                        "components");
                }
                f << ',' << pt.x[i];
            }
            f << '\n';
        }

        // --- 4. Проверка успешности записи ---
        if (!f) {
            throw SolverError("SaveTrajectory: write failed for '" +
                path + "'");
        }
    }

    bool IsStdinInteractive() {
#ifdef _WIN32
        return _isatty(_fileno(stdin)) != 0;
#else
        return isatty(fileno(stdin)) != 0;
#endif
    }

    bool ShouldUseInteractiveMode(int argc) {
        return argc == 1 && IsStdinInteractive();
    }

    void PromptSettings(CliOptions& opts, std::istream& in, std::ostream& out) {
        out << "\n========================================================\n";
        out << "Diffuri - interactive mode\n";
        out << "========================================================\n\n";
        out << "Current settings (key = value):\n";
        PrintSettings(opts, out);

        out << "\nChange settings? [y/N]: ";
        out.flush();

        std::string line;
        if (!std::getline(in, line)) return;
        const std::string ans = ToLowerCopy(TrimCopy(line));
        const bool want_change = (ans == "y" || ans == "yes");
        if (!want_change) return;

        out << "\nEnter key=value pairs separated by spaces, for example:\n";
        out << "  t_end=10 M=30 rtol=1e-8 trajectory=out.csv log=report.log\n";
        out << "Empty line - keep everything as is.\n";
        out << "Available keys: t_end, M, K, h_init, h_min, h_max, rtol,\n";
        out << "                atol, max_steps, M_min, M_max, m_factor,\n";
        out << "                enable_order_adaptation, trajectory, log.\n";

        for (;;) {
            out << "\n> ";
            out.flush();
            if (!std::getline(in, line)) return;
            if (TrimCopy(line).empty()) {
                out << "No changes.\n";
                return;
            }

            // Парсим во временную копию: неудачная строка не портит
            // уже валидное состояние opts.
            CliOptions tmp = opts;
            std::string err;
            if (ApplySettingsLine(line, tmp, err)) {
                opts = tmp;
                out << "\nOK, settings updated.\n";
                PrintSettings(opts, out);
                return;
            }
            out << "Error: " << err << "\n";
            out << "Try again (empty line to cancel):";
        }
    }

    void PressEnterToExit(std::istream& in, std::ostream& out) {
        out << "\nPress Enter to close the window...";
        out.flush();
        std::string dummy;
        std::getline(in, dummy);  // EOF тоже нормально
    }

} // namespace diffuri