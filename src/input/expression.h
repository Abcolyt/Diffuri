// ============================================================================
// src/input/expression.h
//
// Дерево выражений (AST) для левых и правых частей уравнений.
//
// Зависимости:
//   expression -> (ничего из проекта; только std)
//   expression <- parser, input, polynomization
//
// Модуль описывает ТОЛЬКО структуру дерева: какие бывают узлы, как они
// связаны, как их создавать и как по ним ходить. Он ничего не знает про:
//   - синтаксис ввода (штрихи, d²y/dx², sin, cos) — это parser;
//   - уравнения и системы — это input.h;
//   - полиномы и мономы — это polynomization.h.
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
// Используйте MakeCallArgs (для фиксированного числа аргументов) или
// MakeCall с явно собранным std::vector<ExprPtr> (для динамического списка).
// ============================================================================
#pragma once

#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace diffuri {

    // ----------------------------------------------------------------------------
    // Expr рекурсивна: у узлов-операций дети — того же типа Expr.
    // Поэтому дети хранятся как unique_ptr. Ровно один родитель на узел,
    // дерево не «шарится» между частями выражения — отсюда unique, не shared.
    // ----------------------------------------------------------------------------
    struct Expr;
    using ExprPtr = std::unique_ptr<Expr>;

    // ============================================================================
    // ЛИСТЬЯ
    // ============================================================================

    /**
     * @struct Number
     * @brief Числовой литерал: 2, 3.14, -1.0.
     */
    struct Number {
        double value = 0.0;
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
        std::string name;
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
        std::string name;
        double      value = 0.0;
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
        std::string function_name;
        int         order = 1;    // >= 1
    };

    // ============================================================================
    // ОПЕРАЦИИ
    // ============================================================================

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
        enum class Op { Add, Sub, Mul, Div, Pow };

        Op      op = Op::Add;
        ExprPtr lhs;
        ExprPtr rhs;
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
        std::string          name;
        std::vector<ExprPtr> args;
    };

    // ============================================================================
    // УЗЕЛ ДЕРЕВА
    // ============================================================================

    /**
     * @struct Expr
     * @brief Один узел дерева выражений.
     *
     * Вариант из шести альтернатив. Обход выполняется через std::visit:
     *
     *     std::visit([](const auto& node) { ... }, expr.value);
     *
     * Рекурсия по детям — обязанность вызывающего кода; Expr её не прячет.
     */
    struct Expr {
        std::variant<Number, Function, Constant, Derivative, Binary, Call> value;
    };

    // ============================================================================
    // ФАБРИКИ
    //
    // Единственный рекомендованный способ создавать узлы. Прямая инициализация
    // Expr{...} не запрещена, но фабрики:
    //   - держат инварианты в одном месте (order >= 1 у Derivative);
    //   - позволяют позже добавить проверки, не правя весь код;
    //   - делают вызывающий код короче и однозначнее.
    // ============================================================================

    /// Создать узел-число.
    [[nodiscard]] ExprPtr MakeNumber(double value);

    /// Создать узел-имя (Function). Пустое имя допускается технически,
    /// но осмысленным не является — семантику проверяет валидатор системы.
    [[nodiscard]] ExprPtr MakeFunction(std::string name);

    /// Создать узел-константу.
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
     */
    [[nodiscard]] ExprPtr MakeDerivative(std::string function_name, int order);

    /// Создать узел бинарной операции. lhs и rhs не должны быть nullptr.
    [[nodiscard]] ExprPtr MakeBinary(Binary::Op op, ExprPtr lhs, ExprPtr rhs);

    /// Создать узел вызова функции из готового вектора аргументов.
    /// Удобно, когда аргументы собираются в цикле (например, парсером).
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
     */
    template <typename... Args>
    [[nodiscard]] ExprPtr MakeCallArgs(std::string name, Args&&... args) {
        std::vector<ExprPtr> v;
        v.reserve(sizeof...(Args));
        (v.push_back(std::forward<Args>(args)), ...);
        return MakeCall(std::move(name), std::move(v));
    }

    // ============================================================================
    // ЗАПРОСЫ (только чтение, ничего не меняют)
    //
    // Все функции константны по отношению к дереву. Никакой свёртки,
    // подстановки, упрощения — это задачи других модулей.
    // ============================================================================

    /**
     * @brief Является ли узел листом — то есть узлом без детей-Expr.
     *
     * Листьями считаются Number, Function, Constant и Derivative. Для
     * Derivative это техническое определение: у неё нет детей-Expr, хотя
     * семантически она ссылается на функцию по имени.
     */
    [[nodiscard]] bool IsLeaf(const Expr& e);

    /// Содержит ли поддерево хотя бы одну производную.
    [[nodiscard]] bool HasDerivative(const Expr& e);

    /// Максимальный порядок производной в поддереве; 0, если производных нет.
    [[nodiscard]] int MaxDerivativeOrder(const Expr& e);

    /**
     * @brief Имена, встречающиеся как узел Function.
     *
     * Порядок — pre-order обход дерева (узел, левое поддерево, правое).
     * Для Call — по аргументам слева направо. Повторы удаляются.
     *
     * Derivative::function_name и Constant::name сюда НЕ попадают — для
     * них есть отдельные функции CollectDifferentiatedNames и
     * CollectConstantNames.
     */
    [[nodiscard]] std::vector<std::string> CollectFunctionNames(const Expr& e);

    /**
     * @brief Имена функций, по которым берётся производная в этом поддереве.
     *
     * Например, в "x'' + y'" вернёт {"x", "y"}.
     * Порядок — pre-order обход, повторы удаляются.
     */
    [[nodiscard]] std::vector<std::string> CollectDifferentiatedNames(const Expr& e);

    /**
     * @brief Имена констант, встречающихся в поддереве.
     *
     * Порядок — pre-order обход, повторы удаляются.
     */
    [[nodiscard]] std::vector<std::string> CollectConstantNames(const Expr& e);

    /// Читаемое представление дерева — для отладки и тестов.
    /// Пример: "((x + y) * sin(t))".
    [[nodiscard]] std::string ToString(const Expr& e);

} // namespace diffuri