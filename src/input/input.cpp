// ============================================================================
// src/input/input.cpp
//
// Реализация модуля input: сборка RawSystem из текста, валидация, запросы.
//
// Структура файла:
//   1. Анонимный namespace: локальные утилиты.
//   2. Реализация InputError.
//   3. Реализация чтения системы (ParseSystem, ParseSystemFromFile, ParseSystemFromStdin).
//   4. Реализация валидации и запросов (Validate, DerivativeOrders, и т.д.).
// ============================================================================
#include "input/input.h"

// --- Стандартная библиотека (по алфавиту) ---
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

    // ============================================================================
    // 1. АНОНИМНЫЙ NAMESPACE: локальные утилиты
    // ============================================================================
    namespace {
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

        bool LooksLikeInitialCondition(const std::string& raw_line) {
            std::string s = Trim(CutComment(raw_line));
            if (s.empty()) return false;
            auto eq_pos = s.find('=');
            if (eq_pos == std::string::npos) return false;
            std::string lhs = Trim(s.substr(0, eq_pos));
            std::size_t i = 0;
            std::size_t n = lhs.size();
            if (i >= n || !IsAlphaChar(lhs[i])) return false;
            while (i < n && IsAlphaNumChar(lhs[i])) ++i;
            while (i < n && lhs[i] == '\'') ++i;
            if (i >= n || lhs[i] != '(') return false;
            ++i;
            while (i < n && IsSpaceChar(lhs[i])) ++i;
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
            while (i < n && IsSpaceChar(lhs[i])) ++i;
            if (i >= n || lhs[i] != ')') return false;
            ++i;
            while (i < n && IsSpaceChar(lhs[i])) ++i;
            return i == n;
        }

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
                else if constexpr (std::is_same_v<T, Unary>) {
                    CollectDerivativesInto(*node.operand, order, max_orders);
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
                }, e.value);
        }

        std::vector<std::string> CollectFunctionsFromEquations(const RawSystem& sys) {
            std::vector<std::string> order;
            std::map<std::string, int> dummy;
            for (const auto& eq : sys.equations) {
                CollectDerivativesInto(*eq.lhs, order, dummy);
                CollectDerivativesInto(*eq.rhs, order, dummy);
            }
            return order;
        }

        std::vector<std::string> CollectFunctionsFromInitialConditions(const RawSystem& sys) {
            std::vector<std::string> result;
            for (const auto& ic : sys.initial_conditions) {
                if (std::find(result.begin(), result.end(), ic.function_name) == result.end()) {
                    result.push_back(ic.function_name);
                }
            }
            return result;
        }

        std::string FormatNumber(double v) {
            char buf[64];
            auto result = std::to_chars(buf, buf + sizeof(buf), v, std::chars_format::general);
            return std::string(buf, result.ptr);
        }

        void ParseContentLine(const std::string& line,
            const ParseOptions& opts,
            int line_number,
            std::vector<Equation>& equations,
            std::vector<InitialCondition>& ics) {
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
    // 2. РЕАЛИЗАЦИЯ ИСКЛЮЧЕНИЙ
    // ============================================================================
    InputError::InputError(const std::string& what)
        : std::runtime_error(what) {}

    // ============================================================================
    // 3. РЕАЛИЗАЦИЯ ПУБЛИЧНЫХ ФУНКЦИЙ
    // ============================================================================
    RawSystem ParseSystem(std::istream& is, const ParseOptions& opts) {
        RawSystem sys;
        sys.independent_variable = opts.independent_variable;

        std::string line;
        int line_number = 0;
        while (std::getline(is, line)) {
            ++line_number;
            if (IsBlankOrComment(line)) continue;
            ParseContentLine(line, opts, line_number,
                sys.equations, sys.initial_conditions);
        }

        sys.functions = CollectFunctionsFromEquations(sys);
        Validate(sys);
        return sys;
    }

    RawSystem ParseSystem(const std::string& text, const ParseOptions& opts) {
        std::istringstream is(text);
        return ParseSystem(is, opts);
    }

    RawSystem ParseSystemFromFile(const std::string& path, const ParseOptions& opts) {
        std::ifstream f(path);
        if (!f) {
            throw InputError("failed to open file: " + path);
        }
        return ParseSystem(f, opts);
    }

    RawSystem ParseSystemFromStdin(StdinMode mode, const ParseOptions& opts) {
        if (mode == StdinMode::UntilEof) {
            return ParseSystem(std::cin, opts);
        }

        // UntilBlankLine: накапливаем строки до пустой или EOF
        std::ostringstream buf;
        std::string line;
        while (std::getline(std::cin, line)) {
            if (line.empty()) break;
            buf << line << '\n';
        }
        return ParseSystem(buf.str(), opts);
    }

    void Validate(const RawSystem& sys) {
        if (sys.independent_variable.empty()) {
            throw InputError("independent variable name is empty");
        }
        if (sys.equations.empty()) {
            throw InputError("system contains no equations");
        }
        for (const auto& f : sys.functions) {
            if (f == sys.independent_variable) {
                throw InputError(
                    "independent variable name conflicts with function name: " +
                    sys.independent_variable);
            }
        }
        for (const auto& name : CollectFunctionsFromInitialConditions(sys)) {
            if (std::find(sys.functions.begin(), sys.functions.end(), name)
                == sys.functions.end()) {
                throw InputError(
                    "initial condition given for function without equation: " + name);
            }
        }
        auto deriv_orders = DerivativeOrders(sys);
        std::map<std::string, int> ic_counts;
        for (const auto& ic : sys.initial_conditions) {
            ++ic_counts[ic.function_name];
        }
        for (const auto& [name, order] : deriv_orders) {
            int have = ic_counts.count(name) ? ic_counts[name] : 0;
            if (have < order) {
                throw InputError(
                    "not enough initial conditions for function " + name + ": "
                    "need " + std::to_string(order) +
                    ", got " + std::to_string(have));
            }
        }
    }

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