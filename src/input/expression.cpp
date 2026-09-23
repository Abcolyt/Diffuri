// ============================================================================
// src/input/expression.cpp
//
// Реализация дерева выражений: фабрики, запросы, печать.
//
// Что здесь есть:
//   - фабрики узлов (MakeNumber, MakeFunction, ...);
//   - запросы по дереву (IsLeaf, HasDerivative, MaxDerivativeOrder,
//     CollectFunctionNames, CollectDifferentiatedNames, CollectConstantNames);
//   - печать дерева (ToString).
//
// Чего здесь нет:
//   - парсинга текста (это parser.cpp);
//   - семантики системы (это input.cpp);
//   - полиномизации и свёрток (это polynomization.cpp).
// ============================================================================
#include "input/expression.h"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

namespace diffuri {

    namespace {

        // ----------------------------------------------------------------------------
        // Форматирование числа для ToString.
        //
        // Требования тестов:
        //   MakeNumber(3.5)      -> "3.5"
        //   MakeNumber(2.0)      -> "2"        (не "2.0" и не "2.000000")
        //   MakeNumber(1e-3)     -> "0.001"    (не "1e-3")
        //   MakeNumber(2.5e+2)   -> "250"
        //
        // std::to_chars со std::chars_format::general даёт кратчайшее
        // представление без экспоненты для чисел, которые помещаются
        // в «нормальный» диапазон.
        // ----------------------------------------------------------------------------
        std::string FormatNumber(double v) {
            char buf[64];
            auto result = std::to_chars(buf, buf + sizeof(buf), v,
                std::chars_format::general);
            return std::string(buf, result.ptr);
        }

        // ----------------------------------------------------------------------------
        // Проверка: содержит ли вектор строку.
        // ----------------------------------------------------------------------------
        bool Contains(const std::vector<std::string>& v, const std::string& s) {
            return std::find(v.begin(), v.end(), s) != v.end();
        }

        // ----------------------------------------------------------------------------
        // Общий обход дерева с накоплением имён через «экстрактор».
        //
        // Extractor — лямбда, которая по узлу либо возвращает имя (и тогда оно
        // добавляется в результат, если его там ещё нет), либо возвращает
        // std::nullopt (и тогда узел пропускается).
        //
        // Порядок обхода — pre-order: сначала узел, потом левое поддерево,
        // потом правое. Для Call — по аргументам слева направо.
        // ----------------------------------------------------------------------------
        template <typename Extractor>
        std::vector<std::string> CollectNames(const Expr& e, Extractor extract) {
            std::vector<std::string> result;

            std::function<void(const Expr&)> visit = [&](const Expr& node) {
                std::visit([&](const auto& n) {
                    using T = std::decay_t<decltype(n)>;

                    if constexpr (std::is_same_v<T, Function>
                        || std::is_same_v<T, Constant>
                        || std::is_same_v<T, Derivative>) {
                        if (auto name = extract(n); name && !Contains(result, *name)) {
                            result.push_back(*name);
                        }
                    }
                    else if constexpr (std::is_same_v<T, Binary>) {
                        visit(*n.lhs);
                        visit(*n.rhs);
                    }
                    else if constexpr (std::is_same_v<T, Call>) {
                        for (const auto& arg : n.args) visit(*arg);
                    }
                    // Number — ничего
                    }, node.value);
                };

            visit(e);
            return result;
        }

    } // namespace

    // ============================================================================
    // ФАБРИКИ
    // ============================================================================

    ExprPtr MakeNumber(double value) {
        return std::make_unique<Expr>(Expr{ Number{value} });
    }

    ExprPtr MakeFunction(std::string name) {
        return std::make_unique<Expr>(Expr{ Function{std::move(name)} });
    }

    ExprPtr MakeConstant(std::string name, double value) {
        return std::make_unique<Expr>(Expr{ Constant{std::move(name), value} });
    }

    ExprPtr MakeDerivative(std::string function_name, int order) {
        // Инвариант: order >= 1. Если вызывающий код передал 0 или отрицательное —
        // это баг в вызывающем коде, а не в пользовательском вводе. Пока зажимаем
        // до 1; позже можно заменить на assert в debug-сборке.
        if (order < 1) order = 1;
        return std::make_unique<Expr>(
            Expr{ Derivative{std::move(function_name), order} });
    }

    ExprPtr MakeBinary(Binary::Op op, ExprPtr lhs, ExprPtr rhs) {
        return std::make_unique<Expr>(
            Expr{ Binary{op, std::move(lhs), std::move(rhs)} });
    }

    ExprPtr MakeCall(std::string name, std::vector<ExprPtr> args) {
        return std::make_unique<Expr>(
            Expr{ Call{std::move(name), std::move(args)} });
    }

    // ============================================================================
    // ЗАПРОСЫ
    // ============================================================================

    bool IsLeaf(const Expr& e) {
        return std::visit([](const auto& node) {
            using T = std::decay_t<decltype(node)>;
            return std::is_same_v<T, Number>
                || std::is_same_v<T, Function>
                || std::is_same_v<T, Constant>
                || std::is_same_v<T, Derivative>;
            }, e.value);
    }

    bool HasDerivative(const Expr& e) {
        return std::visit([&](const auto& node) -> bool {
            using T = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<T, Derivative>) {
                return true;
            }
            else if constexpr (std::is_same_v<T, Binary>) {
                return HasDerivative(*node.lhs) || HasDerivative(*node.rhs);
            }
            else if constexpr (std::is_same_v<T, Call>) {
                for (const auto& arg : node.args) {
                    if (HasDerivative(*arg)) return true;
                }
                return false;
            }
            else {
                // Number, Function, Constant
                return false;
            }
            }, e.value);
    }

    int MaxDerivativeOrder(const Expr& e) {
        return std::visit([&](const auto& node) -> int {
            using T = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<T, Derivative>) {
                return node.order;
            }
            else if constexpr (std::is_same_v<T, Binary>) {
                return std::max(MaxDerivativeOrder(*node.lhs),
                    MaxDerivativeOrder(*node.rhs));
            }
            else if constexpr (std::is_same_v<T, Call>) {
                int m = 0;
                for (const auto& arg : node.args) {
                    m = std::max(m, MaxDerivativeOrder(*arg));
                }
                return m;
            }
            else {
                return 0;
            }
            }, e.value);
    }

    std::vector<std::string> CollectFunctionNames(const Expr& e) {
        return CollectNames(e, [](const auto& n) -> std::optional<std::string> {
            using T = std::decay_t<decltype(n)>;
            if constexpr (std::is_same_v<T, Function>) return n.name;
            else return std::nullopt;
            });
    }

    std::vector<std::string> CollectDifferentiatedNames(const Expr& e) {
        return CollectNames(e, [](const auto& n) -> std::optional<std::string> {
            using T = std::decay_t<decltype(n)>;
            if constexpr (std::is_same_v<T, Derivative>) return n.function_name;
            else return std::nullopt;
            });
    }

    std::vector<std::string> CollectConstantNames(const Expr& e) {
        return CollectNames(e, [](const auto& n) -> std::optional<std::string> {
            using T = std::decay_t<decltype(n)>;
            if constexpr (std::is_same_v<T, Constant>) return n.name;
            else return std::nullopt;
            });
    }

    // ============================================================================
    // ПЕЧАТЬ
    // ============================================================================

    std::string ToString(const Expr& e) {
        return std::visit([&](const auto& node) -> std::string {
            using T = std::decay_t<decltype(node)>;

            if constexpr (std::is_same_v<T, Number>) {
                return FormatNumber(node.value);
            }
            else if constexpr (std::is_same_v<T, Function>) {
                return node.name;
            }
            else if constexpr (std::is_same_v<T, Constant>) {
                return node.name;
            }
            else if constexpr (std::is_same_v<T, Derivative>) {
                return node.function_name + std::string(node.order, '\'');
            }
            else if constexpr (std::is_same_v<T, Binary>) {
                const char* op = "";
                switch (node.op) {
                case Binary::Op::Add: op = "+"; break;
                case Binary::Op::Sub: op = "-"; break;
                case Binary::Op::Mul: op = "*"; break;
                case Binary::Op::Div: op = "/"; break;
                case Binary::Op::Pow: op = "^"; break;
                }
                return "(" + ToString(*node.lhs) + " " + op + " "
                    + ToString(*node.rhs) + ")";
            }
            else if constexpr (std::is_same_v<T, Call>) {
                std::string s = node.name + "(";
                for (std::size_t i = 0; i < node.args.size(); ++i) {
                    if (i > 0) s += ", ";
                    s += ToString(*node.args[i]);
                }
                s += ")";
                return s;
            }
            }, e.value);
    }

} // namespace diffuri