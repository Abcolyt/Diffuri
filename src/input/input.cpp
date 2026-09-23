// ============================================================================
// src/input/input.cpp
//
// Реализация модуля input: сборка RawSystem из текста, валидация, запросы.
//
// Структура файла:
//   1. Анонимный namespace: локальные утилиты.
//   2. InputError.
//   3. ParseSystem, ParseSystemFromFile, ParseSystemFromStdin.
//   4. Validate.
//   5. Запросы: DerivativeOrders, InitialConditionOrders, CollectT0s,
//      ToString.
// ============================================================================
#include "input/input.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace diffuri {

    namespace {

        // ========================================================================
        // 1. ЛОКАЛЬНЫЕ УТИЛИТЫ
        // ========================================================================

        bool IsSpaceChar(char c) {
            return c == ' ' || c == '\t' || c == '\r' || c == '\n';
        }

        bool IsAlphaChar(char c) {
            return std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_';
        }

        bool IsAlphaNumChar(char c) {
            return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
        }

        bool IsDigitChar(char c) {
            return c >= '0' && c <= '9';
        }

        std::string Trim(const std::string& s) {
            std::size_t a = 0;
            std::size_t b = s.size();
            while (a < b && IsSpaceChar(s[a])) ++a;
            while (b > a && IsSpaceChar(s[b - 1])) --b;
            return s.substr(a, b - a);
        }

        std::string CutComment(const std::string& s) {
            auto pos = s.find('#');
            if (pos == std::string::npos) return s;
            return s.substr(0, pos);
        }

        /**
         * @brief Эвристика: похожа ли строка на попытку записать начальное условие.
         *
         * Формат левой части (до '='):
         *   имя_функции [штрихи] "(" число ")"
         * с опциональными пробелами. Больше ничего в левой части быть не должно.
         *
         * Нужна в ParseSystem: если строка похожа на нач. условие, парсим её
         * ТОЛЬКО через ParseInitialCondition. Иначе — ТОЛЬКО через ParseEquation.
         * Это даёт осмысленные сообщения об ошибках и не пытается разобрать
         * "x(0 = 1" как уравнение.
         */
        bool LooksLikeInitialCondition(const std::string& raw_line) {
            std::string s = Trim(CutComment(raw_line));
            if (s.empty()) return false;

            auto eq_pos = s.find('=');
            if (eq_pos == std::string::npos) return false;

            std::string lhs = Trim(s.substr(0, eq_pos));
            std::size_t i = 0;
            std::size_t n = lhs.size();

            // Имя функции: буква/подчёркивание, затем буквы/цифры/подчёркивания.
            if (i >= n || !IsAlphaChar(lhs[i])) return false;
            while (i < n && IsAlphaNumChar(lhs[i])) ++i;

            // Штрихи (0 или больше).
            while (i < n && lhs[i] == '\'') ++i;

            // Открывающая скобка.
            if (i >= n || lhs[i] != '(') return false;
            ++i;

            // Пробелы.
            while (i < n && IsSpaceChar(lhs[i])) ++i;

            // Число: знак, цифры/точка, опционально экспонента.
            bool has_digit = false;
            if (i < n && (lhs[i] == '+' || lhs[i] == '-')) ++i;
            while (i < n && (IsDigitChar(lhs[i]) || lhs[i] == '.')) {
                if (IsDigitChar(lhs[i])) has_digit = true;
                ++i;
            }
            if (!has_digit) return false;
            if (i < n && (lhs[i] == 'e' || lhs[i] == 'E')) {
                ++i;
                if (i < n && (lhs[i] == '+' || lhs[i] == '-')) ++i;
                while (i < n && IsDigitChar(lhs[i])) ++i;
            }

            // Пробелы.
            while (i < n && IsSpaceChar(lhs[i])) ++i;

            // Закрывающая скобка.
            if (i >= n || lhs[i] != ')') return false;
            ++i;

            // Пробелы.
            while (i < n && IsSpaceChar(lhs[i])) ++i;

            // Ничего лишнего в левой части.
            return i == n;
        }

        /**
         * @brief Обойти дерево и для каждого узла Derivative обновить
         *        max_orders[name] = max(prev, order). Порядок первого появления
         *        имён копится в order.
         */
        void CollectDerivativesInto(const Expr& e,
            std::vector<std::string>& order,
            std::map<std::string, int>& max_orders) {
            std::visit([&](const auto& node) {
                using T = std::decay_t<decltype(node)>;
                if constexpr (std::is_same_v<T, Derivative>) {
                    if (max_orders.find(node.function_name) == max_orders.end()) {
                        order.push_back(node.function_name);
                    }
                    auto& cur = max_orders[node.function_name];
                    if (node.order > cur) cur = node.order;
                }
                else if constexpr (std::is_same_v<T, Binary>) {
                    CollectDerivativesInto(*node.lhs, order, max_orders);
                    CollectDerivativesInto(*node.rhs, order, max_orders);
                }
                else if constexpr (std::is_same_v<T, Call>) {
                    for (const auto& arg : node.args) {
                        CollectDerivativesInto(*arg, order, max_orders);
                    }
                }
                // Number, Function, Constant — ничего.
                }, e.value);
        }

        /**
         * @brief Собрать имена функций, под которыми есть производная в системе.
         *
         * Порядок — первого появления при обходе уравнений сверху вниз:
         * сначала lhs, потом rhs, внутри — pre-order.
         */
        std::vector<std::string> CollectFunctionsFromEquations(const RawSystem& sys) {
            std::vector<std::string> order;
            std::map<std::string, int> dummy;
            for (const auto& eq : sys.equations) {
                CollectDerivativesInto(*eq.lhs, order, dummy);
                CollectDerivativesInto(*eq.rhs, order, dummy);
            }
            return order;
        }

        /**
         * @brief Имена функций, встречающихся в начальных условиях (без повторов).
         */
        std::vector<std::string> CollectFunctionsFromInitialConditions(const RawSystem& sys) {
            std::vector<std::string> result;
            for (const auto& ic : sys.initial_conditions) {
                if (std::find(result.begin(), result.end(), ic.function_name) == result.end()) {
                    result.push_back(ic.function_name);
                }
            }
            return result;
        }

        /**
         * @brief Форматирование числа для ToString(RawSystem).
         *
         * Совпадает с тем, что делает expression.cpp, но локально — чтобы не
         * тянуть FormatNumber в публичный API модуля expression.
         */
        std::string FormatNumber(double v) {
            char buf[64];
            auto result = std::to_chars(buf, buf + sizeof(buf), v,
                std::chars_format::general);
            return std::string(buf, result.ptr);
        }

        /**
         * @brief Разобрать одну содержательную строку в Equation или InitialCondition.
         *
         * Классификация — по эвристике LooksLikeInitialCondition.
         */
        void ParseContentLine(const std::string& line,
            const ParseOptions& opts,
            int line_number,
            std::vector<Equation>& equations,
            std::vector<InitialCondition>& ics) {
            // Копируем опции и выставляем сдвиг строки. Парсер прибавит его
            // к номерам строк, которые он сообщает в ParseError и в токенах.
            ParseOptions local = opts;
            local.line_offset = line_number - 1;

            if (LooksLikeInitialCondition(line)) {
                ics.push_back(ParseInitialCondition(line, local));
            }
            else {
                equations.push_back(ParseEquation(line, local));
            }
        }

    } // namespace

    // ============================================================================
    // 2. InputError
    // ============================================================================

    InputError::InputError(const std::string& what)
        : std::runtime_error(what) {}

    // ============================================================================
    // 3. ЧТЕНИЕ СИСТЕМЫ
    // ============================================================================

    RawSystem ParseSystem(const std::string& text, const ParseOptions& opts) {
        RawSystem sys;
        sys.independent_variable = opts.independent_variable;

        std::istringstream stream(text);
        std::string line;
        int line_number = 0;
        while (std::getline(stream, line)) {
            ++line_number;
            if (IsBlankOrComment(line)) continue;
            ParseContentLine(line, opts, line_number,
                sys.equations, sys.initial_conditions);
        }

        // Список функций собирается из уравнений: только те имена, под которыми
        // встречается производная. Имена, которые упомянуты без производной,
        // в functions не попадают — это задача следующих этапов.
        sys.functions = CollectFunctionsFromEquations(sys);

        Validate(sys);
        return sys;
    }

    RawSystem ParseSystemFromFile(const std::string& path, const ParseOptions& opts) {
        std::ifstream f(path);
        if (!f) {
            throw InputError("не удалось открыть файл: " + path);
        }
        std::ostringstream buf;
        buf << f.rdbuf();
        return ParseSystem(buf.str(), opts);
    }

    RawSystem ParseSystemFromStdin(const ParseOptions& opts) {
        std::ostringstream buf;
        std::string line;
        while (std::getline(std::cin, line)) {
            if (line.empty()) break;   // пустая строка — признак конца ввода
            buf << line << '\n';
        }
        return ParseSystem(buf.str(), opts);
    }

    // ============================================================================
    // 4. ВАЛИДАЦИЯ
    // ============================================================================

    void Validate(const RawSystem& sys) {
        if (sys.independent_variable.empty()) {
            throw InputError("имя независимой переменной пусто");
        }
        if (sys.equations.empty()) {
            throw InputError("система не содержит ни одного уравнения");
        }

        // Независимая переменная не должна совпадать с именем функции.
        for (const auto& f : sys.functions) {
            if (f == sys.independent_variable) {
                throw InputError(
                    "имя независимой переменной совпадает с именем функции: " +
                    sys.independent_variable);
            }
        }

        // Каждое имя в начальных условиях должно быть среди functions.
        for (const auto& name : CollectFunctionsFromInitialConditions(sys)) {
            if (std::find(sys.functions.begin(), sys.functions.end(), name)
                == sys.functions.end()) {
                throw InputError(
                    "начальное условие задано для функции без уравнения: " + name);
            }
        }

        // Для каждой функции число начальных условий >= её старшего порядка
        // производной в системе.
        auto deriv_orders = DerivativeOrders(sys);
        std::map<std::string, int> ic_counts;
        for (const auto& ic : sys.initial_conditions) {
            ++ic_counts[ic.function_name];
        }

        for (const auto& [name, order] : deriv_orders) {
            int have = ic_counts.count(name) ? ic_counts[name] : 0;
            if (have < order) {
                throw InputError(
                    "для функции " + name + " не хватает начальных условий: "
                    "нужно " + std::to_string(order) +
                    ", задано " + std::to_string(have));
            }
        }
    }

    // ============================================================================
    // 5. ЗАПРОСЫ
    // ============================================================================

    std::map<std::string, int> DerivativeOrders(const RawSystem& sys) {
        std::map<std::string, int> result;
        std::vector<std::string> order;
        for (const auto& eq : sys.equations) {
            CollectDerivativesInto(*eq.lhs, order, result);
            CollectDerivativesInto(*eq.rhs, order, result);
        }
        return result;
    }

    std::map<std::string, int> InitialConditionOrders(const RawSystem& sys) {
        std::map<std::string, int> result;
        for (const auto& ic : sys.initial_conditions) {
            auto it = result.find(ic.function_name);
            if (it == result.end() || ic.order > it->second) {
                result[ic.function_name] = ic.order;
            }
        }
        return result;
    }

    std::set<double> CollectT0s(const RawSystem& sys) {
        std::set<double> result;
        for (const auto& ic : sys.initial_conditions) {
            result.insert(ic.t0);
        }
        return result;
    }

    std::string ToString(const RawSystem& sys) {
        std::ostringstream oss;

        for (const auto& eq : sys.equations) {
            oss << ToString(*eq.lhs) << " = " << ToString(*eq.rhs) << '\n';
        }
        for (const auto& ic : sys.initial_conditions) {
            oss << ic.function_name;
            for (int i = 0; i < ic.order; ++i) oss << '\'';
            oss << '(' << FormatNumber(ic.t0) << ") = "
                << FormatNumber(ic.value) << '\n';
        }

        return oss.str();
    }

} // namespace diffuri