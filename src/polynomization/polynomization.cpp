// ============================================================================
// src/polynomization/polynomization.cpp
//
// Реализация полиномизации методом дополнительных переменных.
//
// Структура файла:
//   1. Анонимный namespace: локальные утилиты (VariableCache, EvalAt,
//      CloneImpl, FindTargetImpl, SubstituteImpl).
//   2. Реализация исключений (PolynomizeError).
//   3. Реализация публичных функций (Clone, IsPolynomial, FindTarget,
//      Substitute, CalculateTimeDerivative, Polynomize).
// ============================================================================
#include "polynomization/polynomization.h"

// --- Стандартная библиотека (по алфавиту) ---
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

// --- Внутренние зависимости (по алфавиту) ---
#include "polynomization/library.h"
#include "simplify/simplify.h"

namespace diffuri {

    // ============================================================================
    // 1. АНОНИМНЫЙ NAMESPACE: локальные утилиты
    // ============================================================================
    namespace {

        // ------------------------------------------------------------------------
        // Кэш введённых переменных (внутренняя деталь)
        // ------------------------------------------------------------------------
        struct VariableCache {
            struct Entry {
                std::string name;
                ExprPtr     original;
            };
            struct GetResult {
                std::string name;
                bool        is_new = false;
            };

            std::vector<Entry>                 entries;
            std::map<std::string, std::size_t> by_key;
            std::set<std::string>              taken;
            int                                next_id = 1;

            void Reserve(const std::string& name) { taken.insert(name); }

            [[nodiscard]] GetResult GetOrCreate(const Expr& target) {
                const std::string key = ToString(target);
                auto it = by_key.find(key);
                if (it != by_key.end()) {
                    return { entries[it->second].name, false };
                }
                std::string name;
                do {
                    name = "v_" + std::to_string(next_id++);
                } while (taken.count(name));

                entries.push_back(Entry{ name, Clone(target) });
                by_key.emplace(key, entries.size() - 1);
                taken.insert(name);
                return { std::move(name), true };
            }
        };

        // ------------------------------------------------------------------------
        // Числовой вычислитель для IC новых переменных.
        // ------------------------------------------------------------------------
        double EvalAt(const Expr& e,
            const std::map<std::string, double>& values) {
            return std::visit([&](const auto& node) -> double {
                using T = std::decay_t<decltype(node)>;
                if constexpr (std::is_same_v<T, Number>) {
                    return node.value;
                }
                else if constexpr (std::is_same_v<T, Constant>) {
                    return node.value;
                }
                else if constexpr (std::is_same_v<T, Function>) {
                    auto it = values.find(node.name);
                    if (it == values.end()) {
                        throw PolynomizeError(
                            "EvalAt: unknown function " + node.name);
                    }
                    return it->second;
                }
                else if constexpr (std::is_same_v<T, Unary>) {
                    return -EvalAt(*node.operand, values);
                }
                else if constexpr (std::is_same_v<T, Binary>) {
                    const double a = EvalAt(*node.lhs, values);
                    const double b = EvalAt(*node.rhs, values);
                    switch (node.op) {
                    case Binary::Op::Add: return a + b;
                    case Binary::Op::Sub: return a - b;
                    case Binary::Op::Mul: return a * b;
                    case Binary::Op::Div: return a / b;
                    case Binary::Op::Pow: return std::pow(a, b);
                    }
                    return 0.0;
                }
                else if constexpr (std::is_same_v<T, Call>) {
                    std::vector<double> args;
                    args.reserve(node.args.size());
                    for (const auto& a : node.args) {
                        args.push_back(EvalAt(*a, values));
                    }
                    if (args.size() == 1) {
                        const double x = args[0];
                        if (node.name == "sin")  return std::sin(x);
                        if (node.name == "cos")  return std::cos(x);
                        if (node.name == "tan")  return std::tan(x);
                        if (node.name == "exp")  return std::exp(x);
                        if (node.name == "ln")   return std::log(x);
                        if (node.name == "log")  return std::log(x);
                        if (node.name == "sqrt") return std::sqrt(x);
                        if (node.name == "sh")   return std::sinh(x);
                        if (node.name == "ch")   return std::cosh(x);
                        if (node.name == "sinh") return std::sinh(x);
                        if (node.name == "cosh") return std::cosh(x);
                        if (node.name == "atan") return std::atan(x);
                        if (node.name == "inv")  return 1.0 / x;
                    }
                    if (args.size() == 2) {
                        if (node.name == "pow")   return std::pow(args[0], args[1]);
                        if (node.name == "atan2") return std::atan2(args[0], args[1]);
                    }
                    throw PolynomizeError(
                        "EvalAt: unknown function " + node.name);
                }
                else {
                    // Derivative в дереве IC не ожидается.
                    throw PolynomizeError(
                        "EvalAt: unexpected node in expression");
                }
                }, e.value);
        }

        // ------------------------------------------------------------------------
        // Обходы дерева (реализация публичных функций).
        // ------------------------------------------------------------------------
        ExprPtr CloneImpl(const Expr& e) {
            return std::visit([&](const auto& node) -> ExprPtr {
                using T = std::decay_t<decltype(node)>;
                if constexpr (std::is_same_v<T, Number>) {
                    return MakeNumber(node.value);
                }
                else if constexpr (std::is_same_v<T, Function>) {
                    return MakeFunction(node.name);
                }
                else if constexpr (std::is_same_v<T, Constant>) {
                    return MakeConstant(node.name, node.value);
                }
                else if constexpr (std::is_same_v<T, Derivative>) {
                    return MakeDerivative(node.function_name, node.order);
                }
                else if constexpr (std::is_same_v<T, Unary>) {
                    return MakeUnary(node.op, CloneImpl(*node.operand));
                }
                else if constexpr (std::is_same_v<T, Binary>) {
                    return MakeBinary(node.op,
                        CloneImpl(*node.lhs), CloneImpl(*node.rhs));
                }
                else {
                    std::vector<ExprPtr> args;
                    args.reserve(node.args.size());
                    for (const auto& a : node.args) {
                        args.push_back(CloneImpl(*a));
                    }
                    return MakeCall(node.name, std::move(args));
                }
                }, e.value);
        }

        ExprPtr FindTargetImpl(const Expr& e) {
            return std::visit([&](const auto& node) -> ExprPtr {
                using T = std::decay_t<decltype(node)>;
                if constexpr (std::is_same_v<T, Binary>) {
                    if (auto l = FindTargetImpl(*node.lhs)) return l;
                    if (auto r = FindTargetImpl(*node.rhs)) return r;
                }
                else if constexpr (std::is_same_v<T, Unary>) {
                    return FindTargetImpl(*node.operand);
                }
                else if constexpr (std::is_same_v<T, Call>) {
                    for (const auto& a : node.args) {
                        if (auto inner = FindTargetImpl(*a)) return inner;
                    }
                    bool all_poly = true;
                    for (const auto& a : node.args) {
                        if (!IsPolynomial(*a)) { all_poly = false; break; }
                    }
                    if (all_poly) return CloneImpl(e);
                }
                return nullptr;
                }, e.value);
        }

        ExprPtr SubstituteImpl(const Expr& tree,
            const Expr& target,
            const Expr& replacement) {
            if (ExprEquals(tree, target)) {
                return CloneImpl(replacement);
            }
            return std::visit([&](const auto& node) -> ExprPtr {
                using T = std::decay_t<decltype(node)>;
                if constexpr (std::is_same_v<T, Number>) {
                    return MakeNumber(node.value);
                }
                else if constexpr (std::is_same_v<T, Function>) {
                    return MakeFunction(node.name);
                }
                else if constexpr (std::is_same_v<T, Constant>) {
                    return MakeConstant(node.name, node.value);
                }
                else if constexpr (std::is_same_v<T, Derivative>) {
                    return MakeDerivative(node.function_name, node.order);
                }
                else if constexpr (std::is_same_v<T, Unary>) {
                    return MakeUnary(node.op,
                        SubstituteImpl(*node.operand, target, replacement));
                }
                else if constexpr (std::is_same_v<T, Binary>) {
                    return MakeBinary(node.op,
                        SubstituteImpl(*node.lhs, target, replacement),
                        SubstituteImpl(*node.rhs, target, replacement));
                }
                else {
                    std::vector<ExprPtr> args;
                    args.reserve(node.args.size());
                    for (const auto& a : node.args) {
                        args.push_back(SubstituteImpl(*a, target, replacement));
                    }
                    return MakeCall(node.name, std::move(args));
                }
                }, tree.value);
        }

    } // namespace

    // ============================================================================
    // 2. РЕАЛИЗАЦИЯ ИСКЛЮЧЕНИЙ
    // ============================================================================

    PolynomizeError::PolynomizeError(const std::string& what)
        : std::runtime_error(what) {}

    // ============================================================================
    // 3. РЕАЛИЗАЦИЯ ПУБЛИЧНЫХ ФУНКЦИЙ
    // ============================================================================

    // --- Clone ---
    ExprPtr Clone(const Expr& e) { return CloneImpl(e); }

    // --- IsPolynomial ---
    bool IsPolynomial(const Expr& e) {
        return std::visit([&](const auto& node) -> bool {
            using T = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<T, Number> ||
                std::is_same_v<T, Function> ||
                std::is_same_v<T, Constant>) {
                return true;
            }
            else if constexpr (std::is_same_v<T, Derivative> ||
                std::is_same_v<T, Unary> ||
                std::is_same_v<T, Call>) {
                return false;
            }
            else {
                switch (node.op) {
                case Binary::Op::Add:
                case Binary::Op::Sub:
                case Binary::Op::Mul:
                    return IsPolynomial(*node.lhs) && IsPolynomial(*node.rhs);
                case Binary::Op::Div:
                    return false;
                case Binary::Op::Pow: {
                    if (!IsPolynomial(*node.lhs)) return false;
                    if (!std::holds_alternative<Number>(node.rhs->value)) {
                        return false;
                    }
                    const double v = std::get<Number>(node.rhs->value).value;
                    if (v < 0.0) return false;
                    if (std::floor(v) != v) return false;
                    return true;
                }
                }
                return false;
            }
            }, e.value);
    }

    // --- FindTarget ---
    ExprPtr FindTarget(const Expr& e) { return FindTargetImpl(e); }

    // --- Substitute ---
    ExprPtr Substitute(const Expr& tree,
        const Expr& target,
        const Expr& replacement) {
        return SubstituteImpl(tree, target, replacement);
    }

    // --- CalculateTimeDerivative ---
    ExprPtr CalculateTimeDerivative(const Expr& poly, const RawSystem& sys) {
        return std::visit([&](const auto& node) -> ExprPtr {
            using T = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<T, Number> ||
                std::is_same_v<T, Constant>) {
                return MakeNumber(0.0);
            }
            else if constexpr (std::is_same_v<T, Function>) {
                if (node.name == sys.independent_variable) {
                    return MakeNumber(1.0);
                }
                for (const auto& eq : sys.equations) {
                    if (auto* d = std::get_if<Derivative>(&eq.lhs->value)) {
                        if (d->function_name == node.name && d->order == 1) {
                            return Clone(*eq.rhs);
                        }
                    }
                }
                throw PolynomizeError(
                    "CalculateTimeDerivative: function not found in system: " + node.name);
            }
            else if constexpr (std::is_same_v<T, Derivative>) {
                throw PolynomizeError(
                    "CalculateTimeDerivative: unexpected derivative in polynomial");
            }
            else if constexpr (std::is_same_v<T, Unary>) {
                return Simplify(MakeUnary(node.op,
                    CalculateTimeDerivative(*node.operand, sys)));
            }
            else if constexpr (std::is_same_v<T, Binary>) {
                switch (node.op) {
                case Binary::Op::Add:
                case Binary::Op::Sub:
                    return Simplify(MakeBinary(node.op,
                        CalculateTimeDerivative(*node.lhs, sys),
                        CalculateTimeDerivative(*node.rhs, sys)));
                case Binary::Op::Mul: {
                    auto da = CalculateTimeDerivative(*node.lhs, sys);
                    auto db = CalculateTimeDerivative(*node.rhs, sys);
                    auto t1 = MakeBinary(Binary::Op::Mul, std::move(da), Clone(*node.rhs));
                    auto t2 = MakeBinary(Binary::Op::Mul, Clone(*node.lhs), std::move(db));
                    return Simplify(MakeBinary(Binary::Op::Add,
                        std::move(t1), std::move(t2)));
                }
                case Binary::Op::Div: {
                    auto da = CalculateTimeDerivative(*node.lhs, sys);
                    auto db = CalculateTimeDerivative(*node.rhs, sys);
                    auto n1 = MakeBinary(Binary::Op::Mul, std::move(da), Clone(*node.rhs));
                    auto n2 = MakeBinary(Binary::Op::Mul, Clone(*node.lhs), std::move(db));
                    auto num = MakeBinary(Binary::Op::Sub, std::move(n1), std::move(n2));
                    auto den = MakeBinary(Binary::Op::Pow, Clone(*node.rhs), MakeNumber(2.0));
                    return Simplify(MakeBinary(Binary::Op::Div,
                        std::move(num), std::move(den)));
                }
                case Binary::Op::Pow: {
                    if (!std::holds_alternative<Number>(node.rhs->value)) {
                        throw PolynomizeError(
                            "CalculateTimeDerivative: non-integer exponent in polynomial");
                    }
                    const double n = std::get<Number>(node.rhs->value).value;
                    auto base = Clone(*node.lhs);
                    auto da = CalculateTimeDerivative(*node.lhs, sys);
                    auto pw = MakeBinary(Binary::Op::Pow,
                        std::move(base), MakeNumber(n - 1.0));
                    auto term = MakeBinary(Binary::Op::Mul,
                        MakeNumber(n), std::move(pw));
                    return Simplify(MakeBinary(Binary::Op::Mul,
                        std::move(term), std::move(da)));
                }
                }
                return MakeNumber(0.0);
            }
            else {
                throw PolynomizeError(
                    "CalculateTimeDerivative: Call in polynomial (should not happen)");
            }
            }, poly.value);
    }

    // --- Polynomize (главный цикл) ---
    PolynomizeAuxiliary Polynomize(RawSystem& sys) {
        // --- 1. Проверка: система должна быть первого порядка. ---
        for (const auto& eq : sys.equations) {
            if (!std::holds_alternative<Derivative>(eq.lhs->value)) {
                throw PolynomizeError(
                    "Polynomize: lhs is not a Derivative — system is not in canonical form");
            }
            if (std::get<Derivative>(eq.lhs->value).order != 1) {
                throw PolynomizeError(
                    "Polynomize: system is not first-order; "
                    "call ReduceOrder first");
            }
        }

        VariableCache cache;
        for (const auto& f : sys.functions) cache.Reserve(f);
        FunctionLibrary library;
        constexpr std::size_t kMaxIter = 10000;
        std::size_t iter = 0;

        // --- 2. Главный цикл. ---
        while (true) {
            bool has_non_poly = false;
            ExprPtr target;
            for (auto& eq : sys.equations) {
                if (!IsPolynomial(*eq.rhs)) {
                    has_non_poly = true;
                    target = FindTarget(*eq.rhs);
                    break;
                }
            }
            if (!has_non_poly) break;

            if (!target) {
                throw PolynomizeError(
                    "Polynomize: could not find substitution target "
                    "(RHS is non-polynomial but contains no library call)");
            }
            if (++iter > kMaxIter) {
                throw PolynomizeError(
                    "Polynomize: iteration limit exceeded");
            }

            const auto& call = std::get<Call>(target->value);
            if (call.args.empty()) {
                throw PolynomizeError(
                    "Polynomize: call without arguments is not supported: " + call.name);
            }

            auto expansion_opt = library.Lookup(call.name);
            if (!expansion_opt) {
                throw PolynomizeError(
                    "Polynomize: function not found in library: " + call.name);
            }
            Expansion& expansion = *expansion_opt;

            // --- 2a. Регистрируем все функции расширения. ---
            std::map<std::string, VariableCache::GetResult> reg;
            for (const auto& f : expansion.functions) {
                ExprPtr key_expr = MakeCallArgs(f, Clone(*call.args[0]));
                reg.emplace(f, cache.GetOrCreate(*key_expr));
            }
            const std::string target_name = reg.at(call.name).name;

            // --- 2b. Заменяем target во всех уже существующих уравнениях. ---
            const std::size_t eq_count_before = sys.equations.size();
            {
                ExprPtr repl = MakeFunction(target_name);
                for (std::size_t i = 0; i < eq_count_before; ++i) {
                    sys.equations[i].rhs = Substitute(
                        *sys.equations[i].rhs, *target, *repl);
                }
            }

            // --- 2c. Добавляем уравнения для новых переменных. ---
            for (const auto& f : expansion.functions) {
                const auto& g = reg.at(f);
                if (!g.is_new) continue;

                // df_dp: Function{g} → Function{v_g}
                ExprPtr df_dp = Clone(*expansion.equations.at(f));
                for (const auto& h : expansion.functions) {
                    const std::string vh = reg.at(h).name;
                    ExprPtr from = MakeFunction(h);
                    ExprPtr to = MakeFunction(vh);
                    df_dp = Substitute(*df_dp, *from, *to);
                }

                ExprPtr arg_prime = CalculateTimeDerivative(*call.args[0], sys);
                ExprPtr new_rhs = Simplify(MakeBinary(
                    Binary::Op::Mul, std::move(df_dp), std::move(arg_prime)));

                sys.equations.push_back(Equation{
                    MakeDerivative(g.name, 1),
                    std::move(new_rhs)
                    });
                sys.functions.push_back(g.name);
            }
        }

        // --- 3. Собрать карту. ---
        PolynomizeAuxiliary aux;
        for (auto& e : cache.entries) {
            aux.emplace(e.name, std::move(e.original));
        }

        // --- 4. Начальные условия для новых переменных. ---
        if (!aux.empty()) {
            double t0 = sys.initial_conditions.empty()
                ? 0.0
                : sys.initial_conditions.front().t0;
            for (const auto& ic : sys.initial_conditions) {
                if (ic.t0 != t0) {
                    throw PolynomizeError(
                        "Polynomize: different t0 in initial conditions");
                }
            }

            std::map<std::string, double> values;
            for (const auto& ic : sys.initial_conditions) {
                if (ic.order == 0) values[ic.function_name] = ic.value;
            }

            // Итерируемся по cache.entries (порядок создания v_1, v_2, ...),
            // а не по aux (std::map — лексикографический порядок имён).
            // Иначе при >9 новых переменных v_10 идёт между v_1 и v_2,
            // и зависимость v_N от v_M (M < N) может не разрешиться.
            for (const auto& entry : cache.entries) {
                const double value = EvalAt(*aux.at(entry.name), values);
                values[entry.name] = value;
                sys.initial_conditions.push_back(
                    InitialCondition{ entry.name, 0, t0, value });
            }
        }

        return aux;
    }

} // namespace diffuri