// ============================================================================
// src/core/expression.h
//
// Дерево выражений (AST) для левых и правых частей уравнений.
//
// Зависимости:
//   expression -> (только std)
//   expression <- parser, input, polynomization, quadratize
//
// Отвечает за:
//   - структуру дерева: какие бывают узлы, как они связаны;
//   - фабрики создания узлов (MakeNumber, MakeFunction, MakeBinary, ...);
//   - запросы по дереву (IsLeaf, HasDerivative, CollectFunctionNames, ...);
//   - печать дерева (ToString).
//
// Что модуль НЕ делает:
//   - не разбирает синтаксис ввода (штрихи, d²y/dx², sin, cos) — это parser;
//   - не знает про уравнения и системы — это input.h;
//   - не занимается полиномами и свёртками — это polynomization.h;
//   - не упрощает выражения — это simplify.
//
// Соответствие между записью пользователя и узлами:
//   "2"          -> Number{2.0}
//   "x"          -> Function{"x"}            (если x — не константа)
//   "pi"         -> Constant{"pi", 3.14...}  (если pi есть в таблице констант)
//   "x'"         -> Derivative{"x", 1}
//   "x''"        -> Derivative{"x", 2}
//   "dy/dt"      -> Derivative{"y", 1}
//   "d^2y/dt^2"  -> Derivative{"y", 2}
//   "x + y"      -> MakeBinary(Add, MakeFunction("x"), MakeFunction("y"))
//   "sin(x)"     -> MakeCallArgs("sin", MakeFunction("x"))
//   "pow(x, 2)"  -> MakeCallArgs("pow", MakeFunction("x"), MakeNumber(2.0))
//
// ВАЖНО: строить узлы нужно через фабрики, а не через braced-init-list.
// Запись Call{"sin", {Function{"x"}}} не скомпилируется: initializer_list
// требует копирования элементов, а ExprPtr (unique_ptr) не копируется.
// Используйте MakeCallArgs (для фиксированного числа аргументов) либо
// MakeCall с явно собранным std::vector<ExprPtr> (для динамического списка).
// ============================================================================
#pragma once

// --- Стандартная библиотека ---
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace diffuri {

    // ============================================================================
    // 1. ОПЦИИ И КОНФИГУРАЦИЯ
    // ============================================================================
    // (В этом модуле нет структур опций)

    // ----------------------------------------------------------------------------
    // Expr рекурсивна: у узлов-операций дети — того же типа Expr.
    // Поэтому дети хранятся как unique_ptr. Ровно один родитель на узел,
    // дерево не «шарится» между частями выражения — отсюда unique, не shared.
    // ----------------------------------------------------------------------------
    struct Expr;

    /// Умный указатель на узел дерева выражений.
    using ExprPtr = std::unique_ptr<Expr>;

    // ============================================================================
    // 2. СТРУКТУРЫ ДАННЫХ
    // ============================================================================

    /**
     * @struct Number
     * @brief Числовой литерал: 2, 3.14, -1.0.
     */
    struct Number {
        double value = 0.0; ///< Значение числа.
    };

    /**
     * @struct Function
     * @brief Символьное имя, не являющееся константой.
     *
     * На уровне дерева не различаются:
     *   - неизвестные функции (x, y, u, ...);
     *   - независимая переменная (t, s, τ, ...);
     *   - произвольные параметры.
     *
     * Отличие «функция vs независимая переменная» — свойство системы,
     * а не узла (см. RawSystem::independent_variable). Отличие от константы
     * определяется тем, попало ли имя в таблицу констант.
     */
    struct Function {
        std::string name; ///< Имя функции.
    };

    /**
     * @struct Constant
     * @brief Именованная константа из таблицы констант.
     *
     * Парсер распознаёт имя как Constant, если оно есть в таблице
     * (файл из ParseOptions::constants_file или ParseOptions::extra_constants).
     * Хранится и имя (для печати/сравнения), и значение (для вычислений).
     */
    struct Constant {
        std::string name;  ///< Имя константы.
        double      value = 0.0; ///< Числовое значение.
    };

    /**
     * @struct Derivative
     * @brief Производная функции по независимой переменной.
     *
     * Одна структура для всех форм записи:
     *   y'         -> Derivative{"y", 1}
     *   y''        -> Derivative{"y", 2}
     *   dy/dt      -> Derivative{"y", 1}
     *   d^2y/dt^2  -> Derivative{"y", 2}
     *
     * Независимая переменная здесь не хранится: в проекте она одна
     * (см. RawSystem::independent_variable), и её имя проверяет валидатор.
     */
    struct Derivative {
        std::string function_name; ///< Имя функции, по которой берётся производная.
        int         order = 1;     ///< Порядок производной (>= 1).
    };

    /**
     * @struct Unary
     * @brief Унарная операция над одним операндом.
     *
     * Появилась, чтобы не плодить артефакты вида "0 - x" для унарного минуса.
     * Парсер при встрече "-expr" генерирует Unary::Neg, а Simplifier уже
     * сам решает, оставить его или развернуть в (-1) * expr.
     *
     * Инвариант: operand не должен быть nullptr.
     */
    struct Unary {
        enum class Op { Neg }; ///< В будущем можно добавить Pos, Not и т.п.
        Op      op = Op::Neg;  ///< Тип унарной операции.
        ExprPtr operand;       ///< Операнд (не nullptr).
    };

    /**
     * @struct Binary
     * @brief Бинарная арифметическая операция: lhs OP rhs.
     *
     * Показатель в Pow — любое выражение дерева; ограничение «целая
     * неотрицательная степень» проверяется на этапе полиномизации.
     *
     * Инвариант: lhs и rhs не должны быть nullptr. Проверка — на совести
     * вызывающего кода; фабрики MakeBinary этого не проверяют.
     */
    struct Binary {
        enum class Op { Add, Sub, Mul, Div, Pow }; ///< Тип бинарной операции.
        Op      op = Op::Add; ///< Конкретная операция.
        ExprPtr lhs;          ///< Левый операнд (не nullptr).
        ExprPtr rhs;          ///< Правый операнд (не nullptr).
    };

    /**
     * @struct Call
     * @brief Вызов именованной функции с произвольным числом аргументов.
     *
     * Примеры:
     *   sin(x)        -> MakeCallArgs("sin", MakeFunction("x"))
     *   pow(x, 2)     -> MakeCallArgs("pow", MakeFunction("x"), MakeNumber(2.0))
     *   atan2(y, x)   -> MakeCallArgs("atan2", MakeFunction("y"), MakeFunction("x"))
     *
     * Арность конкретных функций проверяется на этапе полиномизации.
     * Аргументы не должны быть nullptr.
     */
    struct Call {
        std::string          name; ///< Имя вызываемой функции.
        std::vector<ExprPtr> args; ///< Аргументы вызова (не nullptr).
    };

    /**
     * @struct Expr
     * @brief Один узел дерева выражений.
     *
     * Вариант из семи альтернатив. Обход выполняется через std::visit:
     *
     *     std::visit([](const auto& node) { ... }, expr.value);
     *
     * Рекурсия по детям — обязанность вызывающего кода; Expr её не прячет.
     */
    struct Expr {
        std::variant<Number, Function, Constant, Derivative,
            Unary, Binary, Call> value; ///< Содержимое узла.
    };

    // ============================================================================
    // 3. ИСКЛЮЧЕНИЯ
    // ============================================================================
    // (В этом модуле нет специфичных исключений)

    // ============================================================================
    // 4. ПУБЛИЧНЫЙ API (фабрики и запросы)
    // ============================================================================

    /**
     * @brief Создать узел-число.
     *
     * @param value Числовое значение.
     * @return      Новый узел Number.
     */
    [[nodiscard]] ExprPtr MakeNumber(double value);

    /**
     * @brief Создать узел-имя (Function).
     *
     * Пустое имя допускается технически, но осмысленным не является —
     * семантику проверяет валидатор системы.
     *
     * @param name Имя функции.
     * @return     Новый узел Function.
     */
    [[nodiscard]] ExprPtr MakeFunction(std::string name);

    /**
     * @brief Создать узел-константу.
     *
     * @param name  Имя константы.
     * @param value Числовое значение.
     * @return      Новый узел Constant.
     */
    [[nodiscard]] ExprPtr MakeConstant(std::string name, double value);

    /**
     * @brief Создать узел-производную.
     *
     * @param function_name Имя функции, по которой берётся производная.
     * @param order         Порядок производной. Если order < 1, значение
     *                      зажимается до 1 — это гарантирует инвариант
     *                      Derivative::order >= 1. Если такое поведение
     *                      нежелательно, проверяйте order на стороне
     *                      вызывающего кода до вызова фабрики.
     * @return              Новый узел Derivative.
     */
    [[nodiscard]] ExprPtr MakeDerivative(std::string function_name, int order);

    /**
     * @brief Создать узел унарной операции.
     *
     * @param op      Операция (сейчас только Neg).
     * @param operand Операнд. Не должен быть nullptr.
     * @return        Новый узел Unary.
     */
    [[nodiscard]] ExprPtr MakeUnary(Unary::Op op, ExprPtr operand);

    /**
     * @brief Создать узел бинарной операции.
     *
     * @param op  Операция (Add, Sub, Mul, Div, Pow).
     * @param lhs Левый операнд. Не должен быть nullptr.
     * @param rhs Правый операнд. Не должен быть nullptr.
     * @return    Новый узел Binary.
     */
    [[nodiscard]] ExprPtr MakeBinary(Binary::Op op, ExprPtr lhs, ExprPtr rhs);

    /**
     * @brief Создать узел вызова функции из готового вектора аргументов.
     *
     * Удобно, когда аргументы собираются в цикле (например, парсером).
     *
     * @param name Имя вызываемой функции.
     * @param args Вектор аргументов.
     * @return     Новый узел Call.
     */
    [[nodiscard]] ExprPtr MakeCall(std::string name, std::vector<ExprPtr> args);

    /**
     * @brief Удобный вариант MakeCall для переменного числа аргументов.
     *
     * Позволяет писать:
     *     MakeCallArgs("pow", MakeFunction("x"), MakeNumber(2.0))
     * вместо возни с std::vector<ExprPtr> вручную.
     *
     * Не работает с braced-init-list вида MakeCall("pow", {..., ...})
     * из-за того, что std::initializer_list требует копирования, а
     * unique_ptr не копируется. Поэтому и введена эта variadic-версия.
     *
     * @param name Имя вызываемой функции.
     * @param args Аргументы (ExprPtr, передаваемые по rvalue-ссылке).
     * @return     Новый узел Call.
     */
    template <typename... Args>
    [[nodiscard]] ExprPtr MakeCallArgs(std::string name, Args&&... args) {
        std::vector<ExprPtr> v;
        v.reserve(sizeof...(Args));
        (v.push_back(std::forward<Args>(args)), ...);
        return MakeCall(std::move(name), std::move(v));
    }

    /**
     * @brief Является ли узел листом — то есть узлом без детей-Expr.
     *
     * Листьями считаются Number, Function, Constant и Derivative. Для
     * Derivative это техническое определение: у неё нет детей-Expr, хотя
     * семантически она ссылается на функцию по имени.
     *
     * @param e Дерево для проверки.
     * @return  true, если узел — лист.
     */
    [[nodiscard]] bool IsLeaf(const Expr& e);

    /**
     * @brief Содержит ли поддерево хотя бы одну производную.
     *
     * @param e Дерево для проверки.
     * @return  true, если в поддереве есть хотя бы один узел Derivative.
     */
    [[nodiscard]] bool HasDerivative(const Expr& e);

    /**
     * @brief Максимальный порядок производной в поддереве.
     *
     * @param e Дерево для проверки.
     * @return  Максимальный порядок; 0, если производных нет.
     */
    [[nodiscard]] int GetMaxDerivativeOrder(const Expr& e);

    /**
     * @brief Имена, встречающиеся как узел Function.
     *
     * Порядок — pre-order обход дерева (узел, левое поддерево, правое).
     * Для Call — по аргументам слева направо. Повторы удаляются.
     *
     * Derivative::function_name и Constant::name сюда НЕ попадают — для
     * них есть отдельные функции CollectDifferentiatedNames и
     * CollectConstantNames.
     *
     * @param e Дерево для обхода.
     * @return  Вектор уникальных имён функций.
     */
    [[nodiscard]] std::vector<std::string> CollectFunctionNames(const Expr& e);

    /**
     * @brief Имена функций, по которым берётся производная в этом поддереве.
     *
     * Например, в "x'' + y'" вернёт {"x", "y"}.
     * Порядок — pre-order обход, повторы удаляются.
     *
     * @param e Дерево для обхода.
     * @return  Вектор уникальных имён дифференцируемых функций.
     */
    [[nodiscard]] std::vector<std::string> CollectDifferentiatedNames(const Expr& e);

    /**
     * @brief Имена констант, встречающихся в поддереве.
     *
     * Порядок — pre-order обход, повторы удаляются.
     *
     * @param e Дерево для обхода.
     * @return  Вектор уникальных имён констант.
     */
    [[nodiscard]] std::vector<std::string> CollectConstantNames(const Expr& e);

    /**
     * @brief Читаемое представление дерева — для отладки и тестов.
     *
     * Пример: "((x + y) * sin(t))".
     *
     * @param e Дерево для печати.
     * @return  Строковое представление дерева.
     */
    [[nodiscard]] std::string ToString(const Expr& e);

} // namespace diffuri