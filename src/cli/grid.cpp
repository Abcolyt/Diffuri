// ============================================================================
// src/cli/grid.cpp
//
// Реализация модуля cli/grid: перебор комбинаций параметров, генерация
// имён траекторных файлов и сводного summary-файла.
//
// Структура файла:
//   1. Анонимный namespace: локальные утилиты (Trim, SplitWhitespace,
//      SplitCommas, ParseDouble, ParseSizeT, ParseBool, FormatDouble).
//   2. Реализация исключений (GridError).
//   3. Реализация публичных функций.
// ============================================================================
#include "cli/grid.h"

// --- Стандартная библиотека (по алфавиту) ---
#include <cctype>
#include <charconv>
#include <cstddef>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace diffuri {

    // ============================================================================
    // 1. АНОНИМНЫЙ NAMESPACE: локальные утилиты
    // ============================================================================
    namespace {

        // Обрезка пробельных символов с обоих концов.
        std::string Trim(const std::string& s) {
            std::size_t first = 0;
            while (first < s.size() &&
                std::isspace(static_cast<unsigned char>(s[first]))) {
                ++first;
            }
            if (first == s.size()) return {};
            std::size_t last = s.size() - 1;
            while (last > first &&
                std::isspace(static_cast<unsigned char>(s[last]))) {
                --last;
            }
            return s.substr(first, last - first + 1);
        }

        // Разбиение по пробельным символам. Пустые токены не появляются:
        // "a  b\tc" → ["a", "b", "c"].
        std::vector<std::string> SplitWhitespace(const std::string& s) {
            std::vector<std::string> result;
            std::size_t i = 0;
            while (i < s.size()) {
                while (i < s.size() &&
                    std::isspace(static_cast<unsigned char>(s[i]))) {
                    ++i;
                }
                if (i >= s.size()) break;
                const std::size_t start = i;
                while (i < s.size() &&
                    !std::isspace(static_cast<unsigned char>(s[i]))) {
                    ++i;
                }
                result.push_back(s.substr(start, i - start));
            }
            return result;
        }

        // Разбиение по запятым. Пустые токены СОХРАНЯЮТСЯ — их наличие
        // должен обнаружить вызывающий код (для "t_end=1,,2").
        std::vector<std::string> SplitCommas(const std::string& s) {
            std::vector<std::string> result;
            std::size_t start = 0;
            while (true) {
                const std::size_t comma = s.find(',', start);
                if (comma == std::string::npos) {
                    result.push_back(s.substr(start));
                    break;
                }
                result.push_back(s.substr(start, comma - start));
                start = comma + 1;
            }
            return result;
        }

        // Разбор double: значение должно занимать всю строку.
        bool ParseDouble(const std::string& text, double& out) {
            if (text.empty()) return false;
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

        // Разбор std::size_t. Отрицательные значения отсекаются явной
        // проверкой ведущего минуса: std::stoull("-5") вернул бы большое
        // положительное число.
        bool ParseSizeT(const std::string& text, std::size_t& out) {
            if (text.empty() || text.front() == '-') return false;
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

        // Разбор bool: true/1/on/yes и false/0/off/no.
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

        // Короткое представление double (round-trip, без потери точности).
        // Для 1.0 даёт "1", для 1e-15 — "1e-15", для -6.51211369333325 —
        // "-6.51211369333325". Если to_chars недоступен на платформе —
        // откат на ostringstream с максимальной точностью.
        std::string FormatDouble(double value) {
            char buf[64];
            const auto res = std::to_chars(buf, buf + sizeof(buf), value);
            if (res.ec == std::errc{}) {
                return std::string(buf, res.ptr);
            }
            std::ostringstream oss;
            oss << std::setprecision(std::numeric_limits<double>::max_digits10)
                << value;
            return oss.str();
        }

    } // namespace

    // ============================================================================
    // 2. РЕАЛИЗАЦИЯ ИСКЛЮЧЕНИЙ
    // ============================================================================

    GridError::GridError(const std::string& what)
        : std::runtime_error(what) {}

    // ============================================================================
    // 3. РЕАЛИЗАЦИЯ ПУБЛИЧНЫХ ФУНКЦИЙ
    // ============================================================================

    std::string SanitizeFilenameComponent(const std::string& value) {
        // Снимаем пробелы по краям, внутренние пробелы и запрещённые
        // символы заменяем на '_'. Для числовых значений это no-op.
        const std::string trimmed = Trim(value);

        std::string result;
        result.reserve(trimmed.size());
        for (const char c : trimmed) {
            const auto uc = static_cast<unsigned char>(c);
            const bool forbidden =
                c == '<' || c == '>' || c == ':' || c == '"' ||
                c == '/' || c == '\\' || c == '|' || c == '?' ||
                c == '*' || c == ' ' || c == '\t' || uc < 32;
            result += forbidden ? '_' : c;
        }
        return result;
    }

    GridSpec ParseGridSpec(const std::string& text) {
        GridSpec spec;

        const auto tokens = SplitWhitespace(text);
        for (const auto& token : tokens) {
            const std::size_t eq = token.find('=');
            if (eq == std::string::npos) {
                throw GridError(
                    "ParseGridSpec: missing '=' in token '" + token + "'");
            }
            const std::string name = Trim(token.substr(0, eq));
            const std::string raw_values = token.substr(eq + 1);

            if (name.empty()) {
                throw GridError("ParseGridSpec: empty parameter name");
            }
            for (const auto& existing : spec.params) {
                if (existing.name == name) {
                    throw GridError(
                        "ParseGridSpec: duplicate parameter '" + name + "'");
                }
            }

            std::vector<std::string> values;
            for (const auto& raw : SplitCommas(raw_values)) {
                const std::string v = Trim(raw);
                if (v.empty()) {
                    throw GridError(
                        "ParseGridSpec: empty value in list for '" + name + "'");
                }
                values.push_back(v);
            }
            if (values.empty()) {
                throw GridError(
                    "ParseGridSpec: no values for '" + name + "'");
            }

            spec.params.push_back(GridParam{ name, std::move(values) });
        }

        return spec;
    }

    std::vector<GridCombination> EnumerateGrid(const GridSpec& spec) {
        if (spec.params.empty()) return {};

        std::size_t total = 1;
        for (const auto& p : spec.params) {
            total *= p.values.size();
        }

        std::vector<GridCombination> result;
        result.reserve(total);

        std::vector<std::size_t> indices(spec.params.size());

        for (std::size_t k = 0; k < total; ++k) {
            // Первый параметр — внешний счётчик: он меняется медленнее
            // всех. Поэтому индексы вычисляем, разворачивая k в смешанную
            // систему счисления от последнего параметра к первому.
            std::size_t remaining = k;
            for (std::size_t i = spec.params.size(); i-- > 0; ) {
                const std::size_t radix = spec.params[i].values.size();
                indices[i] = remaining % radix;
                remaining /= radix;
            }

            GridCombination combo;
            combo.cells.reserve(spec.params.size());
            for (std::size_t i = 0; i < spec.params.size(); ++i) {
                combo.cells.push_back(GridCell{
                    spec.params[i].name,
                    spec.params[i].values[indices[i]]
                    });
            }
            result.push_back(std::move(combo));
        }

        return result;
    }

    void ApplyGridCombination(CliOptions& opts, const GridCombination& combo) {
        auto& s = opts.solve;
        for (const auto& cell : combo.cells) {
            const std::string& key = cell.name;
            const std::string& value = cell.value;

            bool ok = true;
            if (key == "t_end") {
                ok = ParseDouble(value, s.t_end);
            }
            else if (key == "rtol") {
                ok = ParseDouble(value, s.rtol);
            }
            else if (key == "atol") {
                ok = ParseDouble(value, s.atol);
            }
            else if (key == "h_init") {
                ok = ParseDouble(value, s.h_init);
            }
            else if (key == "h_min") {
                ok = ParseDouble(value, s.h_min);
            }
            else if (key == "h_max") {
                ok = ParseDouble(value, s.h_max);
            }
            else if (key == "m_factor") {
                ok = ParseDouble(value, s.m_factor);
            }
            else if (key == "M") {
                ok = ParseSizeT(value, s.M);
            }
            else if (key == "K") {
                ok = ParseSizeT(value, s.K);
            }
            else if (key == "max_steps") {
                ok = ParseSizeT(value, s.max_steps);
            }
            else if (key == "M_min") {
                ok = ParseSizeT(value, s.M_min);
            }
            else if (key == "M_max") {
                ok = ParseSizeT(value, s.M_max);
            }
            else if (key == "enable_order_adaptation") {
                ok = ParseBool(value, s.enable_order_adaptation);
            }
            else if (key == "input") {
                opts.input_path = (value == "-") ? std::string{} : value;
            }
            else if (key == "log") {
                opts.log_path = (value == "-") ? std::string{} : value;
            }
            else if (key == "trajectory") {
                opts.trajectory_path = (value == "-") ? std::string{} : value;
            }
            else if (key == "sweep_spec") {
                opts.sweep_spec = value;
            }
            else if (key == "sweep_metric") {
                opts.sweep_metric = value;
            }
            else if (key == "sweep_out_path") {
                opts.sweep_out_path = value;
            }
            else {
                throw GridError(
                    "ApplyGridCombination: unknown key '" + key + "'");
            }

            if (!ok) {
                throw GridError(
                    "ApplyGridCombination: bad value for '" + key +
                    "': '" + value + "'");
            }
        }
    }

    std::string MakeGridSuffix(const GridCombination& combo) {
        std::string result;
        for (const auto& cell : combo.cells) {
            result += '_';
            result += SanitizeFilenameComponent(cell.value);
        }
        return result;
    }

    std::string MakeGridOutputPath(const std::string& base_path,
        const std::string& input_path,
        const GridCombination& combo) {
        if (base_path.empty()) return {};

        namespace fs = std::filesystem;

        const fs::path base(base_path);
        const fs::path parent = base.parent_path();
        const std::string stem = base.stem().string();
        const std::string ext = base.extension().string();

        std::string input_stem;
        if (!input_path.empty()) {
            input_stem = fs::path(input_path).stem().string();
        }

        const std::string suffix = MakeGridSuffix(combo);

        std::string new_name = stem;
        if (!input_stem.empty()) {
            new_name += '_';
            new_name += input_stem;
        }
        new_name += suffix;
        new_name += ext;

        if (parent.empty()) return new_name;
        return (parent / new_name).string();
    }

    std::string FormatGridManifestHeader(const GridSpec& spec) {
        std::string result = "index";
        for (const auto& p : spec.params) {
            result += ',';
            result += p.name;
        }
        return result;
    }

    std::string FormatGridCombinationRow(const GridCombination& combo,
        std::size_t index) {
        std::string result = std::to_string(index);
        for (const auto& cell : combo.cells) {
            result += ',';
            result += cell.value;
        }
        return result;
    }

    std::string DefaultGridSummaryPath() {
        return "grid_summary.csv";
    }

    std::string FormatGridSummaryHeader(
        const GridSpec& spec,
        const std::vector<std::string>& function_names) {
        std::string result = "source,index";
        for (const auto& p : spec.params) {
            result += ',';
            result += p.name;
        }
        result += ",steps,order,t_final";
        for (const auto& f : function_names) {
            result += ',';
            result += f;
        }
        return result;
    }

    std::string FormatGridSummaryRow(const GridCombination& combo,
        const GridSummaryMeta& meta,
        const std::vector<double>& values) {
        std::string result = meta.source;
        result += ',';
        result += std::to_string(meta.index);
        for (const auto& cell : combo.cells) {
            result += ',';
            result += cell.value;
        }
        result += ',';
        result += std::to_string(meta.steps);
        result += ',';
        result += std::to_string(meta.order_used);
        result += ',';
        result += FormatDouble(meta.t_final);
        for (const double v : values) {
            result += ',';
            result += FormatDouble(v);
        }
        return result;
    }

} // namespace diffuri  
