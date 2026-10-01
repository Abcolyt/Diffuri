// ============================================================================
// src/simplify/simplify.cpp
//
// Реализация алгебраического упрощения дерева выражений.
//
// Что здесь есть:
//   - Simplify           — рекурсивное упрощение (bottom-up);
//   - Compare            — лексикографическое сравнение деревьев;
//   - ExprEquals         — структурное равенство (через Compare);
//   - IsNumber/AsNumber  — предикат и экстрактор для Number;
//   - ExtractCoefficient — разбор слагаемого на {коэф, база}.
//
// Внутренние хелперы (анонимный namespace):
//   - FlattenAdd / FlattenMul — собрать плоский список;
//   - CombineAdd / CombineMul — свернуть подобные;
//   - SimplifyBinary          — правила для бинарных узлов.
// ============================================================================
#include "simplify/simplify.h"
#include  <algorithm >
#include  <cmath >
#include  <memory >
#include  <stdexcept >
#include  <string >
#include  <type_traits >
#include  <variant >
#include  <vector >
namespace diffuri {
    namespace {
        constexpr double kEps = 1e-12;

        // Порог для схлопывания результата Num+Num-свёртки в ноль.
        // Строже, чем kEps: отсекает только шум вида 0.1+0.2-0.3 ≈ 5.55e-17,
        // но не трогает осмысленно малые значения (1e-15 остаётся 1e-15).
        constexpr double kFoldEps = 1e-15;

        [[nodiscard]] bool IsZero(double v) noexcept {
            return std::abs(v) < kEps;
        }
        [[nodiscard]] bool AlmostEqual(double a, double b) noexcept {
            double diff = std::abs(a - b);
            double scale = std::max(1.0, std::max(std::abs(a), std::abs(b)));
            return diff < kEps * scale;
        }
        // ====================================================================
        // ВСПОМОГАТЕЛЬНОЕ: Flatten для Add и Mul.
        //
        // FlattenAdd разворачивает вложенные Add/Sub в плоский список
        // слагаемых. Sub(a, b) трактуется как Add(a, Mul(-1, b)).
        //
        // FlattenMul разворачивает вложенные Mul в плоский список
        // множителей (Div не трогается).
        // ====================================================================
        void FlattenAdd(ExprPtr e, std::vector<ExprPtr>& out) {
            auto* bin = std::get_if<Binary>(&e->value);
            if (bin) {
                if (bin->op == Binary::Op::Add) {
                    FlattenAdd(std::move(bin->lhs), out);
                    FlattenAdd(std::move(bin->rhs), out);
                    return;
                }
                if (bin->op == Binary::Op::Sub) {
                    FlattenAdd(std::move(bin->lhs), out);
                    auto neg = Simplify(MakeBinary(
                        Binary::Op::Mul, MakeNumber(-1.0), std::move(bin->rhs)));
                    FlattenAdd(std::move(neg), out);
                    return;
                }
            }
            out.push_back(std::move(e));
        }

        void FlattenMul(ExprPtr e, std::vector<ExprPtr>& out) {
            auto* bin = std::get_if<Binary>(&e->value);
            if (bin && bin->op == Binary::Op::Mul) {
                FlattenMul(std::move(bin->lhs), out);
                FlattenMul(std::move(bin->rhs), out);
                return;
            }
            out.push_back(std::move(e));
        }
        // ====================================================================
        // CombineAdd: свернуть плоский список слагаемых.
        //
        // Алгоритм:
        //   1. Для каждого слагаемого извлечь {coeff, base}.
        //   2. Сгруппировать по base (через ExprEquals).
        //   3. Сложить коэффициенты внутри группы.
        //   4. Группы с coeff == 0 отбросить.
        //   5. Отсортировать по Compare для канонической формы.
        //   6. Пересобрать дерево.
        // ====================================================================
        struct Term {
            double  coeff = 1.0;
            ExprPtr base;
        };
        ExprPtr CombineAdd(std::vector<ExprPtr> terms) {
            std::vector<Term> decomposed;
            decomposed.reserve(terms.size());
            for (auto& t : terms) {
                auto dc = ExtractCoefficient(std::move(t));
                decomposed.push_back({ dc.coefficient, std::move(dc.base) });
            }
            // Группировка.
            std::vector<Term> grouped;
            for (auto& t : decomposed) {
                bool found = false;
                for (auto& g : grouped) {
                    if (ExprEquals(*g.base, *t.base)) {
                        g.coeff += t.coeff;
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    grouped.push_back(std::move(t));
                }
            }
            // Разделить на «чисто числовые» (base = Number(1)) и «с переменной частью».
            double numeric_sum = 0.0;
            std::vector<Term> non_numeric;
            for (auto& g : grouped) {
                if (IsNumber(*g.base)) {
                    numeric_sum += g.coeff * AsNumber(*g.base);
                }
                else if (g.coeff != 0.0) {
                    non_numeric.push_back(std::move(g));
                }
            }
            std::vector<ExprPtr> result;
            if (numeric_sum != 0.0) {
                result.push_back(MakeNumber(numeric_sum));
            }
            for (auto& g : non_numeric) {
                ExprPtr term;
                if (g.coeff == 1.0) {
                    term = std::move(g.base);
                }
                else {
                    term = MakeBinary(Binary::Op::Mul,
                        MakeNumber(g.coeff), std::move(g.base));
                }
                result.push_back(std::move(term));
            }
            if (result.empty()) return MakeNumber(0.0);
            if (result.size() == 1) return std::move(result[0]);
            // Сортировка для канонической формы.
            std::sort(result.begin(), result.end(),
                [](const ExprPtr& a, const ExprPtr& b) {
                    return Compare(*a, *b) < 0;
                });
            // Сборка левоассоциативного дерева.
            ExprPtr acc = std::move(result[0]);
            for (std::size_t i = 1; i < result.size(); ++i) {
                acc = MakeBinary(Binary::Op::Add,
                    std::move(acc), std::move(result[i]));
            }
            return acc;
        }
        // ====================================================================
        // CombineMul: свернуть плоский список множителей.
        //
        // Числовые множители перемножаются в один; остальные сортируются
        // по Compare и собираются обратно. Если произведение чисел равно 0 —
        // всё выражение схлопывается в Number(0).
        // ====================================================================
        ExprPtr CombineMul(std::vector<ExprPtr> factors) {
            double numeric_product = 1.0;
            std::vector<ExprPtr> non_numeric;
            non_numeric.reserve(factors.size());
            for (auto& f : factors) {
                if (IsNumber(*f)) {
                    numeric_product *= AsNumber(*f);
                }
                else {
                    non_numeric.push_back(std::move(f));
                }
            }
            if (numeric_product == 0.0) return MakeNumber(0.0);
            std::sort(non_numeric.begin(), non_numeric.end(),
                [](const ExprPtr& a, const ExprPtr& b) {
                    return Compare(*a, *b) < 0;
                });
            std::vector<ExprPtr> result;
            if (numeric_product != 1.0 || non_numeric.empty()) {
                result.push_back(MakeNumber(numeric_product));
            }
            for (auto& f : non_numeric) {
                result.push_back(std::move(f));
            }
            if (result.empty()) return MakeNumber(1.0);
            if (result.size() == 1) return std::move(result[0]);
            // Сборка ПРАВОассоциативная:
            //     [c, x, y]  ->  Mul(c, Mul(x, y))
            // а не Mul(Mul(c, x), y).
            //
            // Это нужно для согласованности с CombineAdd:
            // CombineAdd, собирая обратно Mul(Number(c), base), всегда
            // строит Mul(c, base) — то есть правоассоциативно. Если
            // CombineMul собирает левоассоциативно, то
            //     Simplify(e)              = Mul(Mul(-1, x), y)
            //     Simplify(e + 0)          = Mul(-1, Mul(x, y))
            // — деревья структурно разные, ExprEquals даёт false,
            // и свойство e + 0 == e (P5) ломается.
            ExprPtr acc = std::move(result.back());
            for (std::size_t i = result.size() - 1; i > 0; --i) {
                acc = MakeBinary(Binary::Op::Mul,
                    std::move(result[i - 1]), std::move(acc));
            }
            return acc;
        }
        // ====================================================================
        // SimplifyBinary: правила для бинарных узлов.
        //
        // Вызывается после того, как оба ребёнка уже упрощены.
        // ====================================================================
        ExprPtr SimplifyBinary(Binary::Op op, ExprPtr lhs, ExprPtr rhs) {
            // Обе стороны — числа: сворачиваем в одно.
            if (IsNumber(*lhs) && IsNumber(*rhs)) {
                double l = AsNumber(*lhs);
                double r = AsNumber(*rhs);
                double result = 0.0;
                bool   fold = true;
                switch (op) {
                case Binary::Op::Add: result = l + r; break;
                case Binary::Op::Sub: result = l - r; break;
                case Binary::Op::Mul: result = l * r; break;
                case Binary::Op::Div:
                    if (r == 0.0) { fold = false; break; }
                    result = l / r; break;
                case Binary::Op::Pow: result = std::pow(l, r); break;
                }
                if (fold) {
                    return MakeNumber(std::abs(result) < kFoldEps ? 0.0 : result);
                }
            }
            switch (op) {
            case Binary::Op::Add: {
                std::vector<ExprPtr> terms;
                FlattenAdd(MakeBinary(Binary::Op::Add,
                    std::move(lhs), std::move(rhs)), terms);
                return CombineAdd(std::move(terms));
            }
            case Binary::Op::Sub: {
                // x - x  ->  0.
                //
                // Проверяем структурное равенство до раскрытия. lhs и rhs сюда
                // приходят уже упрощёнными (Simplify вызывает SimplifyBinary
                // после рекурсивного Simplify детей), а Simplify детерминирована
                // (P1). Значит для одного и того же e обе стороны дают идентичное
                // дерево — и Sub можно схлопнуть в ноль.
                //
                // Без этой проверки (x+y) - (x+y) не сокращается: FlattenAdd
                // раскрывает внешнюю сумму, но Mul(-1, Add(x,y)) остаётся единым
                // термом с base = Add(x,y) — а CombineAdd умеет группировать
                // только по совпадающему base.
                if (ExprEquals(*lhs, *rhs)) return MakeNumber(0.0);

                // x - y  ≡  x + (-1)*y
                auto neg_rhs = Simplify(MakeBinary(Binary::Op::Mul,
                    MakeNumber(-1.0), std::move(rhs)));
                std::vector<ExprPtr> terms;
                FlattenAdd(MakeBinary(Binary::Op::Add,
                    std::move(lhs), std::move(neg_rhs)), terms);
                return CombineAdd(std::move(terms));
            }
            case Binary::Op::Mul: {
                std::vector<ExprPtr> factors;
                FlattenMul(MakeBinary(Binary::Op::Mul,
                    std::move(lhs), std::move(rhs)), factors);
                return CombineMul(std::move(factors));
            }
            case Binary::Op::Div: {
                if (IsNumber(*rhs) && AsNumber(*rhs) == 1.0) return std::move(lhs);
                if (IsNumber(*lhs) && AsNumber(*lhs) == 0.0) return MakeNumber(0.0);
                return MakeBinary(Binary::Op::Div,
                    std::move(lhs), std::move(rhs));
            }
            case Binary::Op::Pow: {
                if (IsNumber(*rhs)) {
                    double r = AsNumber(*rhs);
                    if (r == 0.0) return MakeNumber(1.0);
                    if (r == 1.0) return std::move(lhs);
                }
                if (IsNumber(*lhs)) {
                    double l = AsNumber(*lhs);
                    if (l == 0.0) return MakeNumber(0.0);
                    if (l == 1.0) return MakeNumber(1.0);
                }
                return MakeBinary(Binary::Op::Pow,
                    std::move(lhs), std::move(rhs));
            }
            }
            return nullptr; // недостижимо
        }
    } // namespace
    // ========================================================================
    // ОСНОВНАЯ ФУНКЦИЯ
    // ========================================================================
    ExprPtr Simplify(ExprPtr expr) {
        if (!expr) return nullptr;
        if (auto* num = std::get_if<Number>(&expr->value)) {
            return std::make_unique<Expr>(Expr{ Number{num->value} });
        }
        if (auto* f = std::get_if<Function>(&expr->value)) {
            return std::make_unique<Expr>(
                Expr{ Function{std::move(f->name)} });
        }
        if (auto* c = std::get_if<Constant>(&expr->value)) {
            return std::make_unique<Expr>(
                Expr{ Constant{std::move(c->name), c->value} });
        }
        if (auto* d = std::get_if<Derivative>(&expr->value)) {
            return std::make_unique<Expr>(
                Expr{ Derivative{std::move(d->function_name), d->order} });
        }
        if (auto* u = std::get_if<Unary>(&expr->value)) {
            auto op = u->op;
            auto operand = Simplify(std::move(u->operand));
            // Unary::Neg разворачиваем в Mul(-1, x) и упрощаем дальше.
            if (op == Unary::Op::Neg) {
                return Simplify(MakeBinary(Binary::Op::Mul,
                    MakeNumber(-1.0), std::move(operand)));
            }
            return MakeUnary(op, std::move(operand));
        }
        if (auto* b = std::get_if<Binary>(&expr->value)) {
            auto op = b->op;
            auto lhs = Simplify(std::move(b->lhs));
            auto rhs = Simplify(std::move(b->rhs));
            return SimplifyBinary(op, std::move(lhs), std::move(rhs));
        }
        if (auto* call = std::get_if<Call>(&expr->value)) {
            std::vector<ExprPtr> new_args;
            new_args.reserve(call->args.size());
            for (auto& a : call->args) {
                new_args.push_back(Simplify(std::move(a)));
            }
            return MakeCall(std::move(call->name), std::move(new_args));
        }
        return nullptr; // недостижимо
    }
    // ========================================================================
    // СРАВНЕНИЕ И РАВЕНСТВО
    // ========================================================================
    int Compare(const Expr& a, const Expr& b) {
        // Определяем ранг типа для лексикографического порядка.
        auto rank = [](const Expr& e) -> int {
            if (std::holds_alternative<Number>(e.value))     return 0;
            if (std::holds_alternative<Function>(e.value))   return 1;
            if (std::holds_alternative<Constant>(e.value))   return 2;
            if (std::holds_alternative<Derivative>(e.value)) return 3;
            if (std::holds_alternative<Unary>(e.value))      return 4;
            if (std::holds_alternative<Binary>(e.value))     return 5;
            if (std::holds_alternative<Call>(e.value))       return 6;
            return 7;
            };
        int ra = rank(a);
        int rb = rank(b);
        if (ra != rb) return ra < rb ? -1 : 1;
        // Одинаковый тип — сравниваем внутри типа.
        if (auto* na = std::get_if<Number>(&a.value)) {
            auto* nb = std::get_if<Number>(&b.value);
            if (na->value < nb->value) return -1;
            if (na->value > nb->value) return 1;
            return 0;
        }
        if (auto* na = std::get_if<Function>(&a.value)) {
            auto* nb = std::get_if<Function>(&b.value);
            int c = na->name.compare(nb->name);
            return c < 0 ? -1 : (c > 0 ? 1 : 0);
        }
        if (auto* na = std::get_if<Constant>(&a.value)) {
            auto* nb = std::get_if<Constant>(&b.value);
            int c = na->name.compare(nb->name);
            return c < 0 ? -1 : (c > 0 ? 1 : 0);
        }
        if (auto* na = std::get_if<Derivative>(&a.value)) {
            auto* nb = std::get_if<Derivative>(&b.value);
            int c = na->function_name.compare(nb->function_name);
            if (c != 0) return c < 0 ? -1 : 1;
            if (na->order < nb->order) return -1;
            if (na->order > nb->order) return 1;
            return 0;
        }
        if (auto* na = std::get_if<Unary>(&a.value)) {
            auto* nb = std::get_if<Unary>(&b.value);
            int oa = static_cast<int>(na->op);
            int ob = static_cast<int>(nb->op);
            if (oa != ob) return oa < ob ? -1 : 1;
            return Compare(*na->operand, *nb->operand);
        }
        if (auto* na = std::get_if<Binary>(&a.value)) {
            auto* nb = std::get_if<Binary>(&b.value);
            int oa = static_cast<int>(na->op);
            int ob = static_cast<int>(nb->op);
            if (oa != ob) return oa < ob ? -1 : 1;
            int c = Compare(*na->lhs, *nb->lhs);
            if (c != 0) return c;
            return Compare(*na->rhs, *nb->rhs);
        }
        if (auto* na = std::get_if<Call>(&a.value)) {
            auto* nb = std::get_if<Call>(&b.value);
            int c = na->name.compare(nb->name);
            if (c != 0) return c < 0 ? -1 : 1;
            if (na->args.size() < nb->args.size()) return -1;
            if (na->args.size() > nb->args.size()) return 1;
            for (std::size_t i = 0; i < na->args.size(); ++i) {
                int cc = Compare(*na->args[i], *nb->args[i]);
                if (cc != 0) return cc;
            }
            return 0;
        }
        return 0; // недостижимо
    }
    bool ExprEquals(const Expr& a, const Expr& b) {
        return Compare(a, b) == 0;
    }
    // ========================================================================
    // ВСПОМОГАТЕЛЬНОЕ
    // ========================================================================
    bool IsNumber(const Expr& e) {
        return std::holds_alternative<Number>(e.value);
    }
    double AsNumber(const Expr& e) {
        if (auto* n = std::get_if<Number>(&e.value)) {
            return n->value;
        }
        throw std::runtime_error("AsNumber: not a Number");
    }
    
    CoefficientDecomposition ExtractCoefficient(ExprPtr expr) {
        if (IsNumber(*expr)) {
            double c = AsNumber(*expr);
            return { c, MakeNumber(1.0) };
        }
        if (auto* bin = std::get_if<Binary>(&expr->value)) {
            if (bin->op == Binary::Op::Mul) {
                if (IsNumber(*bin->lhs)) {
                    return { AsNumber(*bin->lhs), std::move(bin->rhs) };
                }
                if (IsNumber(*bin->rhs)) {
                    return { AsNumber(*bin->rhs), std::move(bin->lhs) };
                }
                // Канонический Mul левоассоциативен, число (если есть)
                // всегда сидит в самом левом поддереве. Спускаемся туда,
                // чтобы вытащить коэффициент:
                //     Mul(Mul(-1, x), y)  ->  (-1, Mul(x, y))
                // Без этого x*y - x*y не сокращается до 0.
                if (auto* lhs_mul = std::get_if<Binary>(&bin->lhs->value)) {
                    if (lhs_mul->op == Binary::Op::Mul) {
                        auto dc = ExtractCoefficient(std::move(bin->lhs));
                        ExprPtr new_base = MakeBinary(Binary::Op::Mul,
                            std::move(dc.base), std::move(bin->rhs));
                        return { dc.coefficient, std::move(new_base) };
                    }
                }
            }
        }
        return { 1.0, std::move(expr) };
    }
} // namespace diffuri