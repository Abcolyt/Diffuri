// ============================================================================
// src/order_reducer/order_reducer.cpp
//
// Реализация модуля order_reducer: приведение нормализованной системы ОДУ
// y^(n) = RHS к системе первого порядка.
//
// Структура файла:
//   1. Анонимный namespace: локальные утилиты (SubstituteDerivatives).
//   2. Реализация исключений (OrderReducerError).
//   3. Реализация публичных функций (OrderReducer).
// ============================================================================
#include "order_reducer/order_reducer.h"

// --- Стандартная библиотека (по алфавиту) ---
#include <map>
#include <set>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace diffuri {

    // ============================================================================
    // 1. АНОНИМНЫЙ NAMESPACE: локальные утилиты
    // ============================================================================
    namespace {
        // Рекурсивная замена Derivative{f, k} → Function{f_k} для всех
        // k в [1, aux_names.size()]. Производные других функций остаются
        // как есть.
        ExprPtr SubstituteDerivatives(const Expr& e,
            const std::string& f,
            const std::vector<std::string>& aux_names) {
            return std::visit([&](const auto& n) -> ExprPtr {
                using T = std::decay_t<decltype(n)>;
                if constexpr (std::is_same_v<T, Number>) {
                    return MakeNumber(n.value);
                }
                else if constexpr (std::is_same_v<T, Function>) {
                    return MakeFunction(n.name);
                }
                else if constexpr (std::is_same_v<T, Constant>) {
                    return MakeConstant(n.name, n.value);
                }
                else if constexpr (std::is_same_v<T, Derivative>) {
                    if (n.function_name == f
                        && n.order >= 1
                        && n.order <= static_cast<int>(aux_names.size())) {
                        return MakeFunction(aux_names[n.order - 1]);
                    }
                    return MakeDerivative(n.function_name, n.order);
                }
                else if constexpr (std::is_same_v<T, Unary>) {
                    return MakeUnary(n.op,
                        SubstituteDerivatives(*n.operand, f, aux_names));
                }
                else if constexpr (std::is_same_v<T, Binary>) {
                    return MakeBinary(n.op,
                        SubstituteDerivatives(*n.lhs, f, aux_names),
                        SubstituteDerivatives(*n.rhs, f, aux_names));
                }
                else if constexpr (std::is_same_v<T, Call>) {
                    std::vector<ExprPtr> args;
                    args.reserve(n.args.size());
                    for (const auto& a : n.args) {
                        args.push_back(SubstituteDerivatives(*a, f, aux_names));
                    }
                    return MakeCall(n.name, std::move(args));
                }
                return nullptr;
                }, e.value);
        }
    } // namespace

    // ============================================================================
    // 2. РЕАЛИЗАЦИЯ ИСКЛЮЧЕНИЙ
    // ============================================================================
    OrderReducerError::OrderReducerError(const std::string& what)
        : std::runtime_error(what) {}

    // ============================================================================
    // 3. РЕАЛИЗАЦИЯ ПУБЛИЧНЫХ ФУНКЦИЙ
    // ============================================================================
    ReduceOrderAuxiliary ReduceOrder(RawSystem& sys) {
        // --- 1. Входные инварианты: lhs — Derivative, всё не null. ---
        for (const auto& eq : sys.equations) {
            if (!eq.lhs || !eq.rhs) {
                throw OrderReducerError(
                    "ReduceOrder: null lhs or rhs in equation");
            }
            if (!std::holds_alternative<Derivative>(eq.lhs->value)) {
                throw OrderReducerError(
                    "ReduceOrder: lhs is not a Derivative "
                    "(system must be normalized first)");
            }
        }

        // --- 2. Старшие порядки и выбор имён вспомогательных. ---
        const auto orders = DerivativeOrders(sys);
        std::set<std::string> occupied(sys.functions.begin(),
            sys.functions.end());
        const std::vector<std::string> original_functions = sys.functions;
        std::map<std::string, std::vector<std::string>> aux_names;
        std::map<std::string, int>                      max_order_for;

        for (const auto& f : original_functions) {
            int n = 0;
            auto it = orders.find(f);
            if (it != orders.end()) n = it->second;
            max_order_for[f] = n;

            if (n <= 1) continue;

            bool collision = false;
            for (int k = 1; k <= n - 1; ++k) {
                if (occupied.count(f + "_" + std::to_string(k))) {
                    collision = true;
                    break;
                }
            }

            std::vector<std::string> names;
            names.reserve(static_cast<std::size_t>(n - 1));

            if (!collision) {
                for (int k = 1; k <= n - 1; ++k) {
                    names.push_back(f + "_" + std::to_string(k));
                }
            }
            else {
                for (int k = 1; k <= n - 1; ++k) {
                    std::string cand = "_" + f + "_" + std::to_string(k);
                    if (occupied.count(cand)) {
                        throw OrderReducerError(
                            "ReduceOrder: cannot resolve name collision for "
                            "function '" + f + "'");
                    }
                    names.push_back(std::move(cand));
                }
            }

            for (const auto& nm : names) occupied.insert(nm);
            aux_names[f] = std::move(names);
        }

        // --- 3. Проверка согласованности IC. ---
        for (const auto& ic : sys.initial_conditions) {
            auto it = max_order_for.find(ic.function_name);
            if (it != max_order_for.end() && ic.order > it->second) {
                throw OrderReducerError(
                    "ReduceOrder: initial condition order " +
                    std::to_string(ic.order) +
                    " exceeds max derivative order " +
                    std::to_string(it->second) +
                    " for '" + ic.function_name + "'");
            }
        }

        // --- 4. Понижать нечего — выходим, не трогая систему. ---
        ReduceOrderAuxiliary aux;
        if (aux_names.empty()) return aux;

        // --- 5. Заполнение карты метаданных. ---
        for (const auto& kv : aux_names) {
            const std::string& f = kv.first;
            const auto& names = kv.second;
            for (std::size_t k = 1; k <= names.size(); ++k) {
                aux[names[k - 1]] =
                    MakeDerivative(f, static_cast<int>(k));
            }
        }

        // --- 6. Подстановка производных в rhs всех уравнений. ---
        for (auto& eq : sys.equations) {
            for (const auto& kv : aux_names) {
                eq.rhs = SubstituteDerivatives(*eq.rhs,
                    kv.first,
                    kv.second);
            }
        }

        // --- 7. Замена max-derivative уравнений на цепочки. ---
        std::vector<Equation> new_eqs;
        new_eqs.reserve(sys.equations.size() + aux_names.size());

        for (auto& eq : sys.equations) {
            const auto& d = std::get<Derivative>(eq.lhs->value);
            const std::string& f = d.function_name;
            const int n = d.order;
            const int max_n = max_order_for.at(f);

            if (n == max_n && max_n > 1) {
                const auto& names = aux_names.at(f);
                // f' = f_1
                new_eqs.push_back(Equation{
                    MakeDerivative(f, 1),
                    MakeFunction(names[0])
                    });
                // f_k' = f_{k+1}, k = 1..max_n-2
                for (int k = 1; k <= max_n - 2; ++k) {
                    new_eqs.push_back(Equation{
                        MakeDerivative(names[k - 1], 1),
                        MakeFunction(names[k])
                        });
                }
                // f_{max_n-1}' = rhs
                new_eqs.push_back(Equation{
                    MakeDerivative(names[max_n - 2], 1),
                    std::move(eq.rhs)
                    });
            }
            else {
                new_eqs.push_back(std::move(eq));
            }
        }
        sys.equations = std::move(new_eqs);

        // --- 8. Пополнение sys.functions (в порядке исходных функций). ---
        for (const auto& f : original_functions) {
            auto it = aux_names.find(f);
            if (it == aux_names.end()) continue;
            for (const auto& nm : it->second) sys.functions.push_back(nm);
        }

        // --- 9. Перенос IC старших порядков на новые переменные. ---
        for (auto& ic : sys.initial_conditions) {
            auto it = max_order_for.find(ic.function_name);
            if (it == max_order_for.end()) continue;
            if (ic.order >= 1 && ic.order <= it->second - 1) {
                ic.function_name =
                    aux_names.at(ic.function_name)[ic.order - 1];
                ic.order = 0;
            }
        }

        return aux;
    }

} // namespace diffuri