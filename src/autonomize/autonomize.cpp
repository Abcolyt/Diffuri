// ============================================================================
// src/autonomize/autonomize.cpp
//
// Реализация модуля autonomize: приведение неавтономной системы ОДУ
// к автономной путём добавления независимой переменной t как обычной функции.
//
// Структура файла:
//   1. Анонимный namespace: локальные утилиты (ContainsFunction,
//      ValidateFirstOrder, AnyRhsContainsT, HasTrivialTEquation,
//      IsRegisteredFunction).
//   2. Реализация исключений (AutonomizeError).
//   3. Реализация публичных функций (Autonomize).
// ============================================================================
#include "autonomize/autonomize.h"

// --- Стандартная библиотека (по алфавиту) ---
#include <set>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

namespace diffuri {

    // ============================================================================
    // 1. АНОНИМНЫЙ NAMESPACE: локальные утилиты
    // ============================================================================
    namespace {
        // Обход дерева: содержит ли оно узел Function{name}.
        //
        // Для Function — сравнение имени.
        // Для Unary/Binary/Call — рекурсия по детям.
        // Для Number/Constant/Derivative — false (у Derivative нет
        // детей-Expr; см. §4.2 ТЗ).
        bool ContainsFunction(const Expr& e, const std::string& name) {
            return std::visit([&](const auto& node) -> bool {
                using T = std::decay_t<decltype(node)>;
                if constexpr (std::is_same_v<T, Function>) {
                    return node.name == name;
                }
                else if constexpr (std::is_same_v<T, Unary>) {
                    return ContainsFunction(*node.operand, name);
                }
                else if constexpr (std::is_same_v<T, Binary>) {
                    return ContainsFunction(*node.lhs, name)
                        || ContainsFunction(*node.rhs, name);
                }
                else if constexpr (std::is_same_v<T, Call>) {
                    for (const auto& arg : node.args) {
                        if (ContainsFunction(*arg, name)) return true;
                    }
                    return false;
                }
                else {
                    // Number, Constant, Derivative.
                    return false;
                }
                }, e.value);
        }

        // Проверка, что все eq.lhs — Derivative{f, 1}.
        // Бросает AutonomizeError если lhs не Derivative или order != 1.
        void ValidateFirstOrder(const RawSystem& sys) {
            for (const auto& eq : sys.equations) {
                auto* d = std::get_if<Derivative>(&eq.lhs->value);
                if (d == nullptr) {
                    throw AutonomizeError(
                        "Autonomize: lhs уравнения не является Derivative");
                }
                if (d->order != 1) {
                    throw AutonomizeError(
                        "Autonomize: система не первого порядка "
                        "(встречена производная порядка > 1)");
                }
            }
        }

        // Встречается ли независимая переменная t в RHS хотя бы
        // одного уравнения.
        bool AnyRhsContainsT(const RawSystem& sys) {
            const std::string& t = sys.independent_variable;
            for (const auto& eq : sys.equations) {
                if (ContainsFunction(*eq.rhs, t)) return true;
            }
            return false;
        }

        // Есть ли в системе уравнение Derivative{t, 1} = Number(1.0).
        // Признак «нашей» автономизации — используется для идемпотентности.
        bool HasTrivialTEquation(const RawSystem& sys) {
            const std::string& t = sys.independent_variable;
            for (const auto& eq : sys.equations) {
                auto* d = std::get_if<Derivative>(&eq.lhs->value);
                if (d == nullptr) continue;
                if (d->function_name != t || d->order != 1) continue;
                auto* num = std::get_if<Number>(&eq.rhs->value);
                if (num != nullptr && num->value == 1.0) return true;
            }
            return false;
        }

        /// Зарегистрировано ли имя в sys.functions.
        bool IsRegisteredFunction(const RawSystem& sys,
            const std::string& name) {
            for (const auto& f : sys.functions) {
                if (f == name) return true;
            }
            return false;
        }
    } // namespace

    // ============================================================================
    // 2. РЕАЛИЗАЦИЯ ИСКЛЮЧЕНИЙ
    // ============================================================================
    AutonomizeError::AutonomizeError(const std::string& what)
        : std::runtime_error(what) {
    }

    // ============================================================================
    // 3. РЕАЛИЗАЦИЯ ПУБЛИЧНЫХ ФУНКЦИЙ
    // ============================================================================
    AutonomizeAuxiliary Autonomize(RawSystem& sys) {
        // --- 4.1. Предусловия -----------------------------------------------
        ValidateFirstOrder(sys);

        // --- 4.2. Есть ли t в RHS -------------------------------------------
        if (!AnyRhsContainsT(sys)) {
            // Система автономна — no-op.
            return {};
        }
        const std::string& t = sys.independent_variable;

        // --- Идемпотентность -----------------------------------------------
        // Второй вызов Autonomize на уже автономизированной системе
        // должен вернуть пустую карту и не менять систему. Признак
        // «нашей» работы: t ∈ sys.functions и есть тривиальное
        // уравнение t' = 1.
        if (IsRegisteredFunction(sys, t) && HasTrivialTEquation(sys)) {
            return {};
        }

        // --- Страховка (ТЗ §7) ---------------------------------------------
        // t уже в функциях, но это не результат нашей автономизации.
        // Validate обычно такого не пропускает, но перестрахуемся.
        if (IsRegisteredFunction(sys, t)) {
            throw AutonomizeError(
                "Autonomize: независимая переменная '" + t +
                "' уже присутствует в sys.functions");
        }

        // --- 4.3. Определение t0 -------------------------------------------
        const std::set<double> t0s = CollectT0s(sys);
        if (t0s.empty()) {
            throw AutonomizeError(
                "Autonomize: нельзя определить t0: нет начальных условий");
        }
        if (t0s.size() > 1) {
            throw AutonomizeError(
                "Autonomize: разные t0 в IC: нельзя однозначно задать t(t0)");
        }
        const double t0 = *t0s.begin();

        // --- 4.4. Добавление уравнения t' = 1 ------------------------------
        Equation eq_t;
        eq_t.lhs = MakeDerivative(t, 1);
        eq_t.rhs = MakeNumber(1.0);
        sys.equations.push_back(std::move(eq_t));

        // --- 4.5. Добавление IC для t --------------------------------------
        InitialCondition ic_t;
        ic_t.function_name = t;
        ic_t.order = 0;
        ic_t.t0 = t0;
        ic_t.value = t0;
        sys.initial_conditions.push_back(ic_t);

        // --- 4.6. Регистрация t как функции --------------------------------
        sys.functions.push_back(t);

        // --- 4.7. Карта ----------------------------------------------------
        AutonomizeAuxiliary aux;
        aux[t] = MakeNumber(t0);
        return aux;
    }

} // namespace diffuri