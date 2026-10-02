// ============================================================================
// src/polynomization/function_class.h
//
// Модуль polynomization/function_class: описание одного расширения
// библиотечной функции.
//
// Зависимости:
//   function_class -> expression  (Expr, ExprPtr)
//
// Отвечает за:
//   - структуру Expansion — набор функций расширения и их производные
//     по аргументу (df/dp), выраженные через функции расширения.
//
// Не знает:
//   - про конкретный набор расширений (это library.h);
//   - про систему целиком (это polynomization.h);
//   - про синтаксис ввода (это parser);
//   - про приведение к первому порядку (это order_reducer).
//
// Соглашение: в equations[f] производная df/dp выражается через
// «функции-братья» по расширению, записанные как Function{имя}
// без аргумента. Конкретный аргумент подставит Polynomize через
// Substitute: Function{"sin"} → Function{"v_1"} и т.д.
//
// Пример для sin:
//   functions = {"sin", "cos"}
//   equations["sin"] = Function{"cos"}                 // d(sin)/dp
//   equations["cos"] = Mul(-1, Function{"sin"})        // d(cos)/dp
//
// Пример для ln:
//   functions = {"ln", "inv"}
//   equations["ln"]  = Function{"inv"}                 // d(ln)/dp
//   equations["inv"] = Mul(-1, Pow(Function{"inv"},2)) // d(inv)/dp
// ============================================================================
#pragma once

#include <map>
#include <string>
#include <vector>

#include "input/expression.h"

namespace diffuri {

    /**
     * @struct Expansion
     * @brief Расширение одной библиотечной функции.
     *
     * Набор функций, в терминах которых динамика функции и её «соседей»
     * по расширению выражается полиномиально, плюс уравнения, задающие
     * производные этих функций по их общему аргументу p.
     *
     * Инварианты (проверяются тестами библиотеки, не самим типом):
     *   - functions непуст и не содержит повторов;
     *   - ключи equations ровно совпадают с functions;
     *   - equations[f] — полином по Function{f_i}, f_i ∈ functions
     *     (без Call, без Div, без отрицательных/дробных показателей
     *     в степенях).
     *
     * Move-only: содержит ExprPtr (unique_ptr) внутри.
     */
    struct Expansion {
        std::vector<std::string>       functions;
        std::map<std::string, ExprPtr> equations;
    };

} // namespace diffuri