// ============================================================================
// src/polynomization/library.h
//
// Модуль polynomization/library: реестр известных расширений.
//
// Зависимости:
//   library -> function_class  (Expansion)
//   library -> expression      (Expr, ExprPtr, фабрики)
//
// Отвечает за:
//   - хранение пяти расширений этапа (inv, ln, exp, sin/cos, sh/ch);
//   - выдачу расширения по имени через Lookup;
//   - проверку регистрации имени через Contains.
//
// Что НЕ делает:
//   - не знает про Polynomize как таковой;
//   - не занимается подстановкой аргументов (это Polynomize);
//   - не содержит многоаргументных функций (EK, Weber, atan2) —
//     это следующий этап.
//
// Покрытие этапа:
//
//   функция  | расширение | уравнения расширения
//   ---------|------------|---------------------
//   inv(p)   | {inv}      | inv' = -inv^2
//   ln(p)    | {ln, inv}  | ln'  = inv,  inv' = -inv^2
//   exp(p)   | {exp}      | exp' = exp
//   sin(p)   | {sin, cos} | sin' = cos,  cos' = -sin
//   cos(p)   | {sin, cos} | то же
//   sh(p)    | {sh, ch}   | sh'  = ch,   ch'  = sh
//   ch(p)    | {sh, ch}   | то же
//
// (Штрих здесь — производная по аргументу p, не по t.)
// ============================================================================
#pragma once

#include <optional>
#include <string>

#include "polynomization/function_class.h"

namespace diffuri {

    /**
     * @class FunctionLibrary
     * @brief Реестр расширений библиотечных функций.
     *
     * Класс без состояния: Lookup каждый раз строит свежий Expansion,
     * потому что Expansion владеет ExprPtr и не копируется. Вызывающий
     * получает полноценное владение результатом.
     */
    class FunctionLibrary {
    public:
        FunctionLibrary() = default;

        /**
         * @brief Расширение функции по её имени.
         *
         * Принимает и «основное» имя (sin, ln, sh), и имя, входящее
         * в чужое расширение (cos для sin, inv для ln, ch для sh —
         * и наоборот). Для sin и cos возвращает одно и то же
         * расширение {sin, cos}.
         *
         * @return Expansion или пустой optional, если имя не из библиотеки.
         */
        [[nodiscard]] std::optional<Expansion> Lookup(const std::string& name) const;

        /// Зарегистрировано ли имя в библиотеке (как основное или
        /// как часть чужого расширения).
        [[nodiscard]] bool Contains(const std::string& name) const;
    };

} // namespace diffuri