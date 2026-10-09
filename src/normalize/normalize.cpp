// ============================================================================
// src/normalize/normalize.cpp
//
// Реализация нормализации системы ОДУ: разрешение каждого уравнения
// относительно старшей производной своей неизвестной функции.
//
// Что здесь есть:
//   - NormalizeError   — тип семантической ошибки нормализации;
//   - NormalizeSystem  — главный цикл по уравнениям;
//   - IsLinearIn       — проверка линейности по заданной производной;
//   - CalculateTotalCoefficient — извлечение числового коэффициента;
//   - FindTargetFunction   — поиск целевой функции уравнения.
//
// Структура файла:
//   1. Анонимный namespace: локальные утилиты (HasSpecificDerivative,
//      ContainsAnyDerivativeOf, FlattenTerms, IsLinearInRec).
//   2. Реализация исключений.
//   3. Реализация публичных функций.
// ============================================================================
#include "normalize/normalize.h"

#include <cmath>
#include <functional>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "core/expression.h"
#include "input/input.h"
#include "simplify/simplify.h"

namespace diffuri {

    // ============================================================================
    // 1. АНОНИМНЫЙ NAMESPACE: локальные утилиты
    // ============================================================================
    namespace {

        // ------------------------------------------------------------------------
        // Есть ли в поддереве Derivative{func, order}?
        // ------------------------------------------------------------------------
        bool HasSpecificDerivative(const Expr& e,
            const std::string& func, int order) {
            return std::visit([&](const auto& n) -> bool {
                using T = std::decay_t<decltype(n)>;
                if constexpr (std::is_same_v<T, Derivative>) {
                    return n.function_name == func && n.order == order;
                }
                else if constexpr (std::is_same_v<T, Unary>) {
                    return HasSpecificDerivative(*n.operand, func, order);
                }
                else if constexpr (std::is_same_v<T, Binary>) {
                    return HasSpecificDerivative(*n.lhs, func, order)
                        || HasSpecificDerivative(*n.rhs, func, order);
                }
                else if constexpr (std::is_same_v<T, Call>) {
                    for (const auto& arg : n.args) {
                        if (HasSpecificDerivative(*arg, func, order)) return true;
                    }
                    return false;
                }
                else {
                    return false;
                }
                }, e.value);
        }

        // ------------------------------------------------------------------------
        // Содержит ли поддерево ЛЮБУЮ производную функции func (любого порядка)?
        // Нужно для проверки линейности: x'' * x' нелинейно по x'',
        // т.к. x' — производная той же функции.
        // ------------------------------------------------------------------------
        bool ContainsAnyDerivativeOf(const Expr& e, const std::string& func) {
            return std::visit([&](const auto& n) -> bool {
                using T = std::decay_t<decltype(n)>;
                if constexpr (std::is_same_v<T, Derivative>) {
                    return n.function_name == func;
                }
                else if constexpr (std::is_same_v<T, Unary>) {
                    return ContainsAnyDerivativeOf(*n.operand, func);
                }
                else if constexpr (std::is_same_v<T, Binary>) {
                    return ContainsAnyDerivativeOf(*n.lhs, func)
                        || ContainsAnyDerivativeOf(*n.rhs, func);
                }
                else if constexpr (std::is_same_v<T, Call>) {
                    for (const auto& arg : n.args) {
                        if (ContainsAnyDerivativeOf(*arg, func)) return true;
                    }
                    return false;
                }
                else {
                    return false;
                }
                }, e.value);
        }

        // ------------------------------------------------------------------------
        // Плоский список слагаемых со знаками.
        //
        // Add(a, b) разворачивается в {a, b} с одинаковым знаком;
        // Sub(a, b) — в {a, -b}.
        // Знак «входа» (sign) умножается: для lhs уравнения = +1,
        // для rhs = -1 (перенос в левую часть с инверсией).
        // ------------------------------------------------------------------------
        struct SignedTerm {
            double  sign = 1.0;
            ExprPtr expr;
        };

        void FlattenTerms(ExprPtr e, double sign,
            std::vector<SignedTerm>& out) {
            auto* bin = std::get_if<Binary>(&e->value);
            if (bin && (bin->op == Binary::Op::Add ||
                bin->op == Binary::Op::Sub)) {
                FlattenTerms(std::move(bin->lhs), sign, out);
                double rsign = (bin->op == Binary::Op::Add) ? sign : -sign;
                FlattenTerms(std::move(bin->rhs), rsign, out);
                return;
            }
            out.push_back({ sign, std::move(e) });
        }

        // ------------------------------------------------------------------------
        // Рекурсивная проверка линейности.
        //
        // Правило для каждого типа узла:
        //   Number/Function/Constant/Derivative — всегда линейно.
        //   Unary     — recurse в operand.
        //   Add/Sub   — оба ребёнка линейны.
        //   Mul       — не более одного ребёнка содержит target derivative;
        //               тот, который содержит, должен быть линейным.
        //   Div       — только lhs может содержать target derivative и быть
        //               линейным; rhs не содержит.
        //   Pow       — ни base, ни exp не содержат target derivative.
        //   Call      — ни один аргумент не содержит target derivative.
        // ------------------------------------------------------------------------
        bool IsLinearInRec(const Expr& e,
            const std::string& func, int order) {
            return std::visit([&](const auto& n) -> bool {
                using T = std::decay_t<decltype(n)>;
                if constexpr (std::is_same_v<T, Number>
                    || std::is_same_v<T, Function>
                    || std::is_same_v<T, Constant>
                    || std::is_same_v<T, Derivative>) {
                    return true;
                }
                else if constexpr (std::is_same_v<T, Unary>) {
                    return IsLinearInRec(*n.operand, func, order);
                }
                else if constexpr (std::is_same_v<T, Binary>) {
                    bool l_has = HasSpecificDerivative(*n.lhs, func, order);
                    bool r_has = HasSpecificDerivative(*n.rhs, func, order);
                    switch (n.op) {
                    case Binary::Op::Add:
                    case Binary::Op::Sub:
                        return IsLinearInRec(*n.lhs, func, order)
                            && IsLinearInRec(*n.rhs, func, order);
                    case Binary::Op::Mul:
                        if (l_has && r_has) return false;
                        if (l_has) {
                            // Второй множитель не должен содержать НИКАКОЙ
                            // производной той же функции.
                            if (ContainsAnyDerivativeOf(*n.rhs, func)) return false;
                            return IsLinearInRec(*n.lhs, func, order);
                        }
                        if (r_has) {
                            if (ContainsAnyDerivativeOf(*n.lhs, func)) return false;
                            return IsLinearInRec(*n.rhs, func, order);
                        }
                        return true;
                    case Binary::Op::Div:
                        if (r_has) return false;
                        return IsLinearInRec(*n.lhs, func, order);
                    case Binary::Op::Pow:
                        return !l_has && !r_has;
                    }
                    return false;
                }
                else if constexpr (std::is_same_v<T, Call>) {
                    for (const auto& arg : n.args) {
                        if (HasSpecificDerivative(*arg, func, order)) {
                            return false;
                        }
                    }
                    return true;
                }
                return true;
                }, e.value);
        }

    } // namespace

    // ============================================================================
    // 2. РЕАЛИЗАЦИЯ ИСКЛЮЧЕНИЙ
    // ============================================================================

    NormalizeError::NormalizeError(const std::string& what)
        : std::runtime_error(what) {}

    // ============================================================================
    // 3. РЕАЛИЗАЦИЯ ПУБЛИЧНЫХ ФУНКЦИЙ
    // ============================================================================

    bool IsLinearIn(const Expr& expr,
        const std::string& func_name, int order) {
        return IsLinearInRec(expr, func_name, order);
    }

    double CalculateTotalCoefficient(const Expr& expr,
        const std::string& func_name, int order) {
        // Рекурсивный обход без копирования дерева.
        std::function<double(const Expr&, double)> collect =
            [&](const Expr& e, double sign) -> double {
            auto* bin = std::get_if<Binary>(&e.value);
            if (bin && (bin->op == Binary::Op::Add ||
                bin->op == Binary::Op::Sub)) {
                double rsign = (bin->op == Binary::Op::Add) ? sign : -sign;
                return collect(*bin->lhs, sign) + collect(*bin->rhs, rsign);
            }
            if (HasSpecificDerivative(e, func_name, order)) {
                // Извлекаем числовой коэффициент без копирования.
                if (bin && bin->op == Binary::Op::Mul) {
                    if (auto* num = std::get_if<Number>(&bin->lhs->value)) {
                        return sign * num->value;
                    }
                    if (auto* num = std::get_if<Number>(&bin->rhs->value)) {
                        return sign * num->value;
                    }
                    // Mul без Number — символьный коэффициент, не поддерживается.
                    throw NormalizeError(
                        "symbolic coefficient for highest derivative "
                        "is not supported");
                }
                // Просто Derivative без множителя — коэффициент 1.
                if (std::get_if<Derivative>(&e.value)) {
                    return sign * 1.0;
                }
                // Что-то более сложное (Unary, вложенный Mul и т.п.)
                throw NormalizeError(
                    "failed to extract coefficient for highest derivative");
            }
            return 0.0;
            };
        return collect(expr, 1.0);
    }

    std::string FindTargetFunction(const Equation& eq, const RawSystem& sys) {
        auto orders = DerivativeOrders(sys);
        std::string target;
        for (const auto& [func, max_order] : orders) {
            bool in_lhs = HasSpecificDerivative(*eq.lhs, func, max_order);
            bool in_rhs = HasSpecificDerivative(*eq.rhs, func, max_order);
            if (in_lhs || in_rhs) {
                if (!target.empty()) {
                    throw NormalizeError(
                        "equation contains highest derivatives "
                        "of multiple functions (" + target + " and " + func + ")");
                }
                target = func;
            }
        }
        if (target.empty()) {
            throw NormalizeError(
                "no highest derivative found "
                "for any function in equation");
        }
        return target;
    }

    void NormalizeSystem(RawSystem& sys) {
        auto orders = DerivativeOrders(sys);

        for (auto& eq : sys.equations) {
            // --- Шаг 1: упростить обе части. ---
            eq.lhs = Simplify(std::move(eq.lhs));
            eq.rhs = Simplify(std::move(eq.rhs));

            // --- Шаг 2: найти целевую функцию и её старший порядок. ---
            std::string target = FindTargetFunction(eq, sys);
            int max_order = orders.at(target);

            // --- Шаг 3: проверить линейность. ---
            if (!IsLinearIn(*eq.lhs, target, max_order)
                || !IsLinearIn(*eq.rhs, target, max_order)) {
                throw NormalizeError(
                    "equation is non-linear in highest "
                    "derivative of " + target);
            }

            // --- Шаг 4: собрать плоский список всех слагаемых. ---
            // lhs со знаком +1, rhs со знаком -1 (перенос в lhs).
            std::vector<SignedTerm> terms;
            FlattenTerms(std::move(eq.lhs), +1.0, terms);
            FlattenTerms(std::move(eq.rhs), -1.0, terms);

            // --- Шаг 5: разделить на «с производной» и «без». ---
            double C = 0.0;
            std::vector<SignedTerm> rest;
            rest.reserve(terms.size());

            for (auto& t : terms) {
                if (HasSpecificDerivative(*t.expr, target, max_order)) {
                    auto dc = ExtractCoefficient(std::move(t.expr));
                    auto* d = std::get_if<Derivative>(&dc.base->value);
                    if (!d || d->function_name != target
                        || d->order != max_order) {
                        throw NormalizeError(
                            "symbolic coefficient for highest "
                            "derivative of " + target +
                            " is not supported at this stage");
                    }
                    C += t.sign * dc.coefficient;
                }
                else {
                    rest.push_back(std::move(t));
                }
            }

            if (C == 0.0) {
                throw NormalizeError(
                    "coefficient for highest derivative of " + target +
                    " is zero");
            }

            // --- Шаг 6: собрать новую rhs. ---
            // Уравнение было: (слагаемые с D)*D + (остальные lhs) = (остальные rhs)
            // После переноса: C*D + sum(lhs_rest) - sum(rhs_rest) = 0
            // => C*D = sum(rhs_rest) - sum(lhs_rest)
            // => D   = (sum(rhs_rest) - sum(lhs_rest)) / C
            //
            // В SignedTerm: lhs_rest имеют sign = +1, rhs_rest имеют sign = -1.
            // Нужно: для lhs_rest добавить в rhs с минусом,
            //        для rhs_rest добавить в rhs с плюсом.
            // Иными словами: rhs += -sign * term.expr
            ExprPtr rhs = MakeNumber(0.0);
            for (auto& t : rest) {
                double inv_sign = -t.sign;
                ExprPtr addend;
                if (inv_sign == 1.0) {
                    addend = std::move(t.expr);
                }
                else if (inv_sign == -1.0) {
                    addend = MakeUnary(Unary::Op::Neg, std::move(t.expr));
                }
                else {
                    addend = MakeBinary(Binary::Op::Mul,
                        MakeNumber(inv_sign), std::move(t.expr));
                }
                rhs = MakeBinary(Binary::Op::Add,
                    std::move(rhs), std::move(addend));
            }

            // --- Шаг 7: деление на C. ---
            if (C != 1.0) {
                rhs = MakeBinary(Binary::Op::Mul,
                    MakeNumber(1.0 / C), std::move(rhs));
            }

            // --- Шаг 8: финальное упрощение rhs. ---
            rhs = Simplify(std::move(rhs));

            // --- Шаг 9: заменить lhs на y^(n). ---
            eq.lhs = MakeDerivative(target, max_order);
            eq.rhs = std::move(rhs);
        }
    }

} // namespace diffuri