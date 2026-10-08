// ============================================================================
// src/quadratize/quadratize.cpp
// ============================================================================
#include "quadratize/quadratize.h"

#include <limits>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "input/expression.h"
#include "polynomization/polynomization.h"
#include "simplify/simplify.h"

namespace diffuri {

    QuadratizeError::QuadratizeError(const std::string& what)
        : std::runtime_error("Quadratize: " + what) {
    }

    // ============================================================================
    // IsQuadratic
    // ============================================================================

    namespace {

        int PolyDegree(const Expr& e) {
            return std::visit([&](const auto& node) -> int {
                using T = std::decay_t<decltype(node)>;
                if constexpr (std::is_same_v<T, Number>)     return 0;
                if constexpr (std::is_same_v<T, Constant>)   return 0;
                if constexpr (std::is_same_v<T, Function>)   return 1;
                if constexpr (std::is_same_v<T, Derivative>) return -1;
                if constexpr (std::is_same_v<T, Unary>)      return -1;
                if constexpr (std::is_same_v<T, Call>)       return -1;
                if constexpr (std::is_same_v<T, Binary>) {
                    switch (node.op) {
                    case Binary::Op::Add:
                    case Binary::Op::Sub: {
                        int dl = PolyDegree(*node.lhs);
                        int dr = PolyDegree(*node.rhs);
                        if (dl < 0 || dr < 0) return -1;
                        return dl > dr ? dl : dr;
                    }
                    case Binary::Op::Mul: {
                        int dl = PolyDegree(*node.lhs);
                        int dr = PolyDegree(*node.rhs);
                        if (dl < 0 || dr < 0) return -1;
                        return dl + dr;
                    }
                    case Binary::Op::Pow: {
                        auto* f = std::get_if<Function>(&node.lhs->value);
                        auto* n = std::get_if<Number>(&node.rhs->value);
                        if (!f || !n) return -1;
                        double k = n->value;
                        if (k < 0.0 || k != std::floor(k)) return -1;
                        return static_cast<int>(k);
                    }
                    case Binary::Op::Div: return -1;
                    }
                }
                return -1;
                }, e.value);
        }

    } // namespace

    bool IsQuadratic(const Expr& e) {
        return std::visit([&](const auto& node) -> bool {
            using T = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<T, Number>)     return true;
            if constexpr (std::is_same_v<T, Constant>)   return true;
            if constexpr (std::is_same_v<T, Function>)   return true;
            if constexpr (std::is_same_v<T, Derivative>) return false;
            if constexpr (std::is_same_v<T, Unary>)      return false;
            if constexpr (std::is_same_v<T, Call>)       return false;
            if constexpr (std::is_same_v<T, Binary>) {
                switch (node.op) {
                case Binary::Op::Add:
                case Binary::Op::Sub:
                    return IsQuadratic(*node.lhs) && IsQuadratic(*node.rhs);
                case Binary::Op::Mul: {
                    if (!IsQuadratic(*node.lhs)) return false;
                    if (!IsQuadratic(*node.rhs)) return false;
                    int dl = PolyDegree(*node.lhs);
                    int dr = PolyDegree(*node.rhs);
                    if (dl < 0 || dr < 0) return false;
                    return dl + dr <= 2;
                }
                case Binary::Op::Pow: {
                    auto* f = std::get_if<Function>(&node.lhs->value);
                    auto* n = std::get_if<Number>(&node.rhs->value);
                    if (!f || !n) return false;
                    return n->value == 2.0;
                }
                case Binary::Op::Div: return false;
                }
            }
            return false;
            }, e.value);
    }

    // ============================================================================
    // Внутреннее состояние
    // ============================================================================

    namespace {

        struct State {
            std::map<std::string, std::string> cache;
            std::map<std::string, std::pair<std::string, std::string>> aux_defs;
            std::map<std::string, std::vector<std::string>> aux_monomial;
            std::vector<std::string> aux_order;
            std::set<std::string> used_names;
            std::string prefix;
            int next_index = 1;
        };

        std::string UnitsKey(std::vector<std::string> units) {
            std::sort(units.begin(), units.end());
            std::string key;
            std::size_t i = 0;
            while (i < units.size()) {
                std::size_t j = i;
                while (j < units.size() && units[j] == units[i]) ++j;
                int exp = static_cast<int>(j - i);
                if (!key.empty()) key += "*";
                key += units[i];
                if (exp > 1) key += "^" + std::to_string(exp);
                i = j;
            }
            return key;
        }

        std::string NextAuxName(State& st) {
            for (;;) {
                std::string name = st.prefix + "q_" + std::to_string(st.next_index);
                ++st.next_index;
                if (st.used_names.insert(name).second) return name;
            }
        }

        std::vector<std::string> ExpandFactor(const std::string& name, const State& st) {
            auto it = st.aux_monomial.find(name);
            if (it != st.aux_monomial.end()) return it->second;
            return { name };
        }

        void CollectSourceUnits(const Expr& e, const State& st,
            std::vector<std::string>& out) {
            std::visit([&](const auto& node) {
                using T = std::decay_t<decltype(node)>;
                if constexpr (std::is_same_v<T, Function>) {
                    auto ex = ExpandFactor(node.name, st);
                    out.insert(out.end(), ex.begin(), ex.end());
                }
                else if constexpr (std::is_same_v<T, Binary>) {
                    if (node.op == Binary::Op::Mul) {
                        CollectSourceUnits(*node.lhs, st, out);
                        CollectSourceUnits(*node.rhs, st, out);
                    }
                    else if (node.op == Binary::Op::Pow) {
                        auto* f = std::get_if<Function>(&node.lhs->value);
                        auto* n = std::get_if<Number>(&node.rhs->value);
                        if (f && n && n->value >= 1 && std::floor(n->value) == n->value) {
                            int k = static_cast<int>(n->value);
                            auto ex = ExpandFactor(f->name, st);
                            for (int i = 0; i < k; ++i)
                                out.insert(out.end(), ex.begin(), ex.end());
                        }
                    }
                }
                }, e.value);
        }

        std::vector<std::string> SourceUnitsOf(const Expr& e, const State& st) {
            std::vector<std::string> units;
            CollectSourceUnits(e, st, units);
            return units;
        }

        std::pair<std::size_t, std::size_t> ChoosePair(
            const std::vector<std::string>& units, const State& st) {
            if (units.size() < 2) return { 0, 0 };

            std::map<std::string, int> count;
            std::map<std::string, std::size_t> first;
            for (std::size_t i = 0; i < units.size(); ++i) {
                ++count[units[i]];
                if (first.find(units[i]) == first.end()) first[units[i]] = i;
            }

            std::size_t best_i = 0;
            std::size_t best_j = 1;
            long long best_score = -1;
            std::size_t best_first = std::numeric_limits<std::size_t>::max();

            for (std::size_t i = 0; i < units.size(); ++i) {
                for (std::size_t j = i + 1; j < units.size(); ++j) {
                    long long score = 0;
                    std::vector<std::string> pair{ units[i], units[j] };

                    // Уже существующая квадратичная переменная — самый лучший выбор.
                    if (st.cache.find(UnitsKey(pair)) != st.cache.end())
                        score += 1000000;

                    // Квадраты одной переменной тоже обычно хороши.
                    if (units[i] == units[j])
                        score += 10000;

                    // Чем чаще встречаются переменные, тем обычно выгоднее пара.
                    score += 100LL * (count[units[i]] + count[units[j]]);

                    std::size_t f = std::min(first[units[i]], first[units[j]]);
                    if (score > best_score ||
                        (score == best_score && f < best_first)) {
                        best_score = score;
                        best_first = f;
                        best_i = i;
                        best_j = j;
                    }
                }
            }

            return { best_i, best_j };
        }

        std::vector<std::string> OrderUnitsForChain(
            std::vector<std::string> units, const State& st) {
            if (units.size() <= 2) return units;

            std::map<std::string, int> count;
            std::map<std::string, std::size_t> first;
            for (std::size_t i = 0; i < units.size(); ++i) {
                ++count[units[i]];
                if (first.find(units[i]) == first.end()) first[units[i]] = i;
            }

            auto [bi, bj] = ChoosePair(units, st);

            std::vector<bool> used(units.size(), false);
            used[bi] = true;
            used[bj] = true;

            std::vector<std::string> ordered;
            ordered.reserve(units.size());
            ordered.push_back(units[bi]);
            ordered.push_back(units[bj]);

            std::vector<std::string> prefix = ordered;

            while (ordered.size() < units.size()) {
                std::size_t pick = std::numeric_limits<std::size_t>::max();

                // Сначала пробуем добавить такой моном, который уже есть в кэше.
                for (std::size_t k = 0; k < units.size(); ++k) {
                    if (used[k]) continue;

                    std::vector<std::string> p = prefix;
                    p.push_back(units[k]);

                    if (st.cache.find(UnitsKey(p)) != st.cache.end()) {
                        pick = k;
                        break;
                    }
                }

                // Иначе берём самую «частую» переменную, при равенстве — первую.
                if (pick == std::numeric_limits<std::size_t>::max()) {
                    int best_count = -1;
                    std::size_t best_first = std::numeric_limits<std::size_t>::max();

                    for (std::size_t k = 0; k < units.size(); ++k) {
                        if (used[k]) continue;

                        int c = count[units[k]];
                        std::size_t f = first[units[k]];

                        if (c > best_count ||
                            (c == best_count && f < best_first)) {
                            best_count = c;
                            best_first = f;
                            pick = k;
                        }
                    }
                }

                // Страховка.
                if (pick == std::numeric_limits<std::size_t>::max()) {
                    for (std::size_t k = 0; k < units.size(); ++k) {
                        if (!used[k]) {
                            pick = k;
                            break;
                        }
                    }
                }

                used[pick] = true;
                ordered.push_back(units[pick]);
                prefix.push_back(units[pick]);
            }

            return ordered;
        }

        void FlattenMulInto(ExprPtr e, std::vector<ExprPtr>& out) {
            if (auto* b = std::get_if<Binary>(&e->value);
                b && b->op == Binary::Op::Mul) {
                FlattenMulInto(std::move(b->lhs), out);
                FlattenMulInto(std::move(b->rhs), out);
            }
            else {
                out.push_back(std::move(e));
            }
        }

        ExprPtr RebuildMul(std::vector<ExprPtr> factors) {
            if (factors.empty()) return MakeNumber(1.0);
            ExprPtr result = std::move(factors.back());
            for (int i = static_cast<int>(factors.size()) - 2; i >= 0; --i) {
                result = MakeBinary(Binary::Op::Mul, std::move(factors[i]),
                    std::move(result));
            }
            return result;
        }

        std::string GetOrCreateAux(State& st,
            const std::string& a,
            const std::string& b,
            const std::vector<std::string>& source_units) {
            std::string key = UnitsKey(source_units);
            auto it = st.cache.find(key);
            if (it != st.cache.end()) return it->second;
            std::string name = NextAuxName(st);
            st.cache[key] = name;
            st.aux_defs[name] = { a, b };
            st.aux_monomial[name] = source_units;
            st.aux_order.push_back(name);
            return name;
        }

        // ============================================================================
        // Дистрибутивность: Mul(Add(a,b), c) -> Add(Mul(a,c), Mul(b,c))
        // ============================================================================

        ExprPtr Distribute(ExprPtr e) {
            if (e == nullptr) return e;
            auto* b = std::get_if<Binary>(&e->value);
            if (!b) return e;
            if (b->op == Binary::Op::Add || b->op == Binary::Op::Sub) {
                auto lhs = Distribute(std::move(b->lhs));
                auto rhs = Distribute(std::move(b->rhs));
                return MakeBinary(b->op, std::move(lhs), std::move(rhs));
            }
            if (b->op != Binary::Op::Mul) return e;

            auto lhs = Distribute(std::move(b->lhs));
            auto rhs = Distribute(std::move(b->rhs));

            auto* lb = std::get_if<Binary>(&lhs->value);
            if (lb && (lb->op == Binary::Op::Add || lb->op == Binary::Op::Sub)) {
                auto op = lb->op;
                ExprPtr a = std::move(lb->lhs);
                ExprPtr b2 = std::move(lb->rhs);
                ExprPtr rhs_copy = Clone(*rhs);
                ExprPtr t1 = Distribute(MakeBinary(Binary::Op::Mul,
                    std::move(a), std::move(rhs)));
                ExprPtr t2 = Distribute(MakeBinary(Binary::Op::Mul,
                    std::move(b2), std::move(rhs_copy)));
                return MakeBinary(op, std::move(t1), std::move(t2));
            }
            auto* rb = std::get_if<Binary>(&rhs->value);
            if (rb && (rb->op == Binary::Op::Add || rb->op == Binary::Op::Sub)) {
                auto op = rb->op;
                ExprPtr a = std::move(rb->lhs);
                ExprPtr b2 = std::move(rb->rhs);
                ExprPtr lhs_copy = Clone(*lhs);
                ExprPtr t1 = Distribute(MakeBinary(Binary::Op::Mul,
                    std::move(lhs), std::move(a)));
                ExprPtr t2 = Distribute(MakeBinary(Binary::Op::Mul,
                    std::move(lhs_copy), std::move(b2)));
                return MakeBinary(op, std::move(t1), std::move(t2));
            }
            return MakeBinary(Binary::Op::Mul, std::move(lhs), std::move(rhs));
        }

        // ============================================================================
        // Обработка исходного RHS
        // ============================================================================

        bool IsSourceMonomial(const Expr& e, const State& st,
            std::vector<std::string>& units) {
            return std::visit([&](const auto& node) -> bool {
                using T = std::decay_t<decltype(node)>;
                if constexpr (std::is_same_v<T, Number> ||
                    std::is_same_v<T, Constant>) return true;
                if constexpr (std::is_same_v<T, Function>) {
                    if (st.aux_defs.count(node.name)) return false;
                    units.push_back(node.name);
                    return true;
                }
                if constexpr (std::is_same_v<T, Binary>) {
                    if (node.op == Binary::Op::Mul) {
                        return IsSourceMonomial(*node.lhs, st, units)
                            && IsSourceMonomial(*node.rhs, st, units);
                    }
                    if (node.op == Binary::Op::Pow) {
                        auto* f = std::get_if<Function>(&node.lhs->value);
                        auto* n = std::get_if<Number>(&node.rhs->value);
                        if (f && n && n->value >= 1 &&
                            std::floor(n->value) == n->value) {
                            if (st.aux_defs.count(f->name)) return false;
                            int k = static_cast<int>(n->value);
                            for (int i = 0; i < k; ++i) units.push_back(f->name);
                            return true;
                        }
                    }
                    return false;
                }
                return false;
                }, e.value);
        }

        ExprPtr SubstituteMonos(ExprPtr e, State& st);

        // FIX: d == 3 -> одна переменная q_1 = u_0·u_1, RHS = q_1 · u_2.
        //      d >= 4 -> полная цепочка.
        //      FIX: специальный случай для двух разных имён с чётными кратностями
        //      (x^2·y^2) — вводим две квадратичные переменные и возвращаем
        //      их произведение.
        ExprPtr ProcessSourceMonomial(std::vector<std::string> units, State& st) {
            int d = static_cast<int>(units.size());

            if (d <= 2) {
                std::vector<ExprPtr> fs;
                for (auto& u : units) fs.push_back(MakeFunction(u));
                return RebuildMul(std::move(fs));
            }

            // Если такой моном уже представлен вспомогательной переменной,
            // сразу используем её.
            std::string full_key = UnitsKey(units);
            if (auto it = st.cache.find(full_key); it != st.cache.end()) {
                return MakeFunction(it->second);
            }

            // Для степени 3 сознательно не создаём сразу переменную для всего монома.
            // Например, x^3 -> q1 = x*x, RHS = q1 * x.
            // Это нужно, чтобы тесты вида x' = x^3 давали ровно одну переменную.
            if (d == 3) {
                auto [i, j] = ChoosePair(units, st);

                std::string a = units[i];
                std::string b = units[j];
                std::string c;

                for (std::size_t k = 0; k < units.size(); ++k) {
                    if (k != i && k != j) {
                        c = units[k];
                        break;
                    }
                }

                std::string q = GetOrCreateAux(st, a, b, { a, b });
                return MakeBinary(Binary::Op::Mul,
                    MakeFunction(q), MakeFunction(c));
            }

            // Для степени >= 4 строим цепочку, но порядок выбираем умнее:
            // сначала кэшированные/повторяющиеся пары, затем остальные.
            units = OrderUnitsForChain(std::move(units), st);

            std::vector<std::string> prefix = { units[0], units[1] };
            std::string prev = GetOrCreateAux(st, units[0], units[1], prefix);

            for (std::size_t k = 2; k < units.size(); ++k) {
                prefix.push_back(units[k]);
                std::string q = GetOrCreateAux(st, prev, units[k], prefix);
                prev = q;
            }

            return MakeFunction(prev);
        }

        ExprPtr ProcessSourceRhs(ExprPtr e, State& st) {
            if (auto* b = std::get_if<Binary>(&e->value)) {
                if (b->op == Binary::Op::Add || b->op == Binary::Op::Sub) {
                    auto lhs = ProcessSourceRhs(std::move(b->lhs), st);
                    auto rhs = ProcessSourceRhs(std::move(b->rhs), st);
                    return MakeBinary(b->op, std::move(lhs), std::move(rhs));
                }
                if (b->op == Binary::Op::Mul) {
                    auto lhs = ProcessSourceRhs(std::move(b->lhs), st);
                    auto rhs = ProcessSourceRhs(std::move(b->rhs), st);

                    ExprPtr combined = MakeBinary(Binary::Op::Mul,
                        std::move(lhs), std::move(rhs));

                    std::vector<std::string> units;
                    if (IsSourceMonomial(*combined, st, units)
                        && units.size() >= 3) {
                        return ProcessSourceMonomial(units, st);
                    }

                    // FIX: произведение может уже содержать вспомогательные
                    // переменные, например:
                    //
                    //   q_1 * v_1 * v_2
                    //
                    // IsSourceMonomial для такого выражения вернёт false,
                    // но степень всё равно может быть больше 2.
                    // Поэтому нужно прогнать его через общий механизм
                    // подстановки и квадратизации мономов.
                    if (IsQuadratic(*combined)) {
                        return combined;
                    }

                    return SubstituteMonos(std::move(combined), st);
                }
                if (b->op == Binary::Op::Pow) {
                    auto* f = std::get_if<Function>(&b->lhs->value);
                    auto* n = std::get_if<Number>(&b->rhs->value);
                    if (f && n && n->value >= 3 &&
                        std::floor(n->value) == n->value) {
                        if (!st.aux_defs.count(f->name)) {
                            int k = static_cast<int>(n->value);
                            std::vector<std::string> units(k, f->name);
                            return ProcessSourceMonomial(std::move(units), st);
                        }
                    }
                    return e;
                }
            }
            return e;
        }

        // ============================================================================
        // Подстановка кэшированных мономов
        // ============================================================================

        ExprPtr SubstituteMonos(ExprPtr e, State& st);

        ExprPtr ProcessMulNode(ExprPtr e, State& st) {
            std::vector<ExprPtr> factors;
            FlattenMulInto(std::move(e), factors);
            for (auto& f : factors) f = SubstituteMonos(std::move(f), st);

            // Шаг редукции: заменяем не-Function факторы на Function.
            //  - source-degree 0/1: не трогаем (Number, Constant, Function).
            //  - source-degree 2: заменяем на Function через GetOrCreateAux.
            //  - source-degree >= 3: строим цепочку через ProcessSourceMonomial.
            {
                std::vector<ExprPtr> reduced;
                for (auto& f : factors) {
                    if (std::holds_alternative<Number>(f->value) ||
                        std::holds_alternative<Constant>(f->value) ||
                        std::holds_alternative<Function>(f->value)) {
                        reduced.push_back(std::move(f));
                        continue;
                    }
                    std::vector<std::string> units;
                    CollectSourceUnits(*f, st, units);
                    if (units.empty()) {
                        reduced.push_back(std::move(f));
                        continue;
                    }
                    std::sort(units.begin(), units.end());
                    std::string key = UnitsKey(units);
                    auto it = st.cache.find(key);
                    if (it != st.cache.end()) {
                        reduced.push_back(MakeFunction(it->second));
                        continue;
                    }
                    if (units.size() == 2) {
                        // FIX: source-degree == 2 -> один aux.
                        std::string name = GetOrCreateAux(st, units[0], units[1], units);
                        reduced.push_back(MakeFunction(name));
                        continue;
                    }
                    // source-degree >= 3 -> цепочка.
                    ExprPtr r = ProcessSourceMonomial(std::move(units), st);
                    FlattenMulInto(std::move(r), reduced);
                }
                factors = std::move(reduced);
            }

            ExprPtr rebuilt = RebuildMul(std::move(factors));
            if (IsQuadratic(*rebuilt)) return rebuilt;

            factors.clear();
            FlattenMulInto(std::move(rebuilt), factors);

            // Pass 2: пары через кэш.
            bool changed = false;
            for (std::size_t i = 0; i < factors.size() && !changed; ++i) {
                if (!std::holds_alternative<Function>(factors[i]->value)) continue;
                for (std::size_t j = i + 1; j < factors.size() && !changed; ++j) {
                    if (!std::holds_alternative<Function>(factors[j]->value)) continue;
                    std::vector<std::string> units;
                    CollectSourceUnits(*factors[i], st, units);
                    CollectSourceUnits(*factors[j], st, units);
                    std::string key = UnitsKey(units);
                    auto it = st.cache.find(key);
                    if (it != st.cache.end()) {
                        factors[i] = MakeFunction(it->second);
                        factors.erase(factors.begin() + j);
                        changed = true;
                    }
                }
            }
            if (changed) {
                rebuilt = RebuildMul(std::move(factors));
                return SubstituteMonos(std::move(rebuilt), st);
            }

            // Pass 3: ввести новую aux из пары Functions.
//
// Старая версия брала первую попавшуюся пару, предпочитая только
// source-source пары. Из-за этого для x^2*y^2 могла быть выбрана
// пара q1*q2 вместо более полезной x*y / x*q2 / q2*y, и алгоритм
// начинал бесконечно плодить вспомогательные переменные.
//
// Теперь выбираем пару с минимальной исходной степенью.
// Например, для факторов {q3, q4, y}:
//   q3*q4 может иметь исходную степень 6,
//   q4*y  — степень 3,
// поэтому q4*y лучше.
            constexpr std::size_t kNone = static_cast<std::size_t>(-1);

            std::size_t best_i = kNone;
            std::size_t best_j = kNone;
            std::size_t best_degree = std::numeric_limits<std::size_t>::max();
            int best_score = -1;
            std::size_t best_max = std::numeric_limits<std::size_t>::max();

            std::vector<std::vector<std::string>> factor_units(factors.size());
            std::vector<bool> has_units(factors.size(), false);

            for (std::size_t i = 0; i < factors.size(); ++i) {
                if (std::holds_alternative<Function>(factors[i]->value)) {
                    factor_units[i] = SourceUnitsOf(*factors[i], st);
                    has_units[i] = true;
                }
            }

            for (std::size_t i = 0; i < factors.size(); ++i) {
                if (!has_units[i]) continue;

                const std::string& name_i =
                    std::get<Function>(factors[i]->value).name;
                bool i_src = st.aux_defs.find(name_i) == st.aux_defs.end();

                for (std::size_t j = i + 1; j < factors.size(); ++j) {
                    if (!has_units[j]) continue;

                    const std::string& name_j =
                        std::get<Function>(factors[j]->value).name;
                    bool j_src = st.aux_defs.find(name_j) == st.aux_defs.end();

                    const std::size_t degree =
                        factor_units[i].size() + factor_units[j].size();

                    const int score =
                        (i_src && j_src) ? 2 : ((i_src || j_src) ? 1 : 0);

                    const std::size_t mx = std::max(
                        factor_units[i].size(),
                        factor_units[j].size());

                    if (best_i == kNone ||
                        degree < best_degree ||
                        (degree == best_degree && score > best_score) ||
                        (degree == best_degree && score == best_score && mx < best_max)) {
                        best_i = i;
                        best_j = j;
                        best_degree = degree;
                        best_score = score;
                        best_max = mx;
                    }
                }
            }

            if (best_i != kNone) {
                std::vector<std::string> units;
                units.reserve(best_degree);

                units.insert(units.end(),
                    factor_units[best_i].begin(),
                    factor_units[best_i].end());

                units.insert(units.end(),
                    factor_units[best_j].begin(),
                    factor_units[best_j].end());

                const std::string name_i =
                    std::get<Function>(factors[best_i]->value).name;
                const std::string name_j =
                    std::get<Function>(factors[best_j]->value).name;

                const std::string new_name =
                    GetOrCreateAux(st, name_i, name_j, units);

                factors[best_i] = MakeFunction(new_name);
                factors.erase(factors.begin() + best_j);

                rebuilt = RebuildMul(std::move(factors));
                return SubstituteMonos(std::move(rebuilt), st);
            }

            return RebuildMul(std::move(factors));
        }

        ExprPtr SubstituteMonos(ExprPtr e, State& st) {
            if (auto* b = std::get_if<Binary>(&e->value)) {
                if (b->op == Binary::Op::Add || b->op == Binary::Op::Sub) {
                    auto lhs = SubstituteMonos(std::move(b->lhs), st);
                    auto rhs = SubstituteMonos(std::move(b->rhs), st);
                    return MakeBinary(b->op, std::move(lhs), std::move(rhs));
                }
                if (b->op == Binary::Op::Mul) {
                    return ProcessMulNode(std::move(e), st);
                }
                if (b->op == Binary::Op::Pow) {
                    auto* f = std::get_if<Function>(&b->lhs->value);
                    auto* n = std::get_if<Number>(&b->rhs->value);
                    if (f && n && n->value >= 2 &&
                        std::floor(n->value) == n->value) {
                        int k = static_cast<int>(n->value);
                        auto ex = ExpandFactor(f->name, st);

                        std::vector<std::string> full;
                        for (int i = 0; i < k; ++i)
                            full.insert(full.end(), ex.begin(), ex.end());

                        // FIX: если source-degree <= 2 — оставляем Pow как есть,
                        // IsQuadratic(Pow(Function, 2)) = true.
                        if (full.size() <= 2) return e;

                        std::string full_key = UnitsKey(full);
                        auto it = st.cache.find(full_key);
                        if (it != st.cache.end())
                            return MakeFunction(it->second);

                        // FIX: иначе строим цепочку через ProcessSourceMonomial.
                        return ProcessSourceMonomial(std::move(full), st);
                    }
                    return e;
                }
                return e;
            }
            return e;
        }

        // ============================================================================
        // Валидация и вспомогательное
        // ============================================================================

        void ValidatePreconditions(const RawSystem& sys) {
            for (const auto& eq : sys.equations) {
                auto* d = std::get_if<Derivative>(&eq.lhs->value);
                if (!d) throw QuadratizeError(
                    "lhs of an equation is not a Derivative "
                    "(run NormalizeSystem and OrderReducer first)");
                if (d->order != 1) throw QuadratizeError(
                    "system is not first-order (run OrderReducer first)");
                if (!IsPolynomial(*eq.rhs)) throw QuadratizeError(
                    "RHS is not polynomial (run Polynomize first)");
            }
            auto t0s = CollectT0s(sys);
            if (t0s.size() > 1) throw QuadratizeError(
                "initial conditions have different t0");
        }

        const Expr* FindRhs(const RawSystem& sys, const std::string& f) {
            for (const auto& eq : sys.equations) {
                auto* d = std::get_if<Derivative>(&eq.lhs->value);
                if (d && d->order == 1 && d->function_name == f) {
                    return eq.rhs.get();
                }
            }
            return nullptr;
        }

        bool AllRhsQuadraticInternal(const RawSystem& sys) {
            for (const auto& eq : sys.equations) {
                if (!IsQuadratic(*eq.rhs)) return false;
            }
            return true;
        }

    } // namespace

    // ============================================================================
    // Quadratize
    // ============================================================================

    QuadratizeAuxiliary Quadratize(RawSystem& sys) {
        ValidatePreconditions(sys);
        if (AllRhsQuadraticInternal(sys)) return {};

        State st;
        for (const auto& f : sys.functions) st.used_names.insert(f);

        if (st.used_names.count("q_1")) {
            if (st.used_names.count("_q_1")) throw QuadratizeError(
                "both q_1 and _q_1 are already used as function names");
            st.prefix = "_";
        }

        // Шаг 1: переписываем исходные RHS.
        for (auto& eq : sys.equations) {
            eq.rhs = ProcessSourceRhs(std::move(eq.rhs), st);
        }

        // Шаг 2: динамически выводим уравнения новых переменных.
        // st.aux_order растёт внутри цикла (GetOrCreateAux вызывает
        // SubstituteMonos), поэтому проверяем лимит на каждой итерации,
        // а не один раз до цикла.
        constexpr std::size_t kMaxAux = 1000;

        for (std::size_t i = 0; i < st.aux_order.size(); ++i) {
            if (st.aux_order.size() > kMaxAux) {
                throw QuadratizeError(
                    "auxiliary variable count exceeded limit (" +
                    std::to_string(kMaxAux) + "); "
                    "the system likely has an unresolved high-degree monomial");
            }

            const std::string name = st.aux_order[i];
            const auto def = st.aux_defs.at(name);
            const std::string& a = def.first;
            const std::string& b = def.second;

            const Expr* rhs_a = FindRhs(sys, a);
            const Expr* rhs_b = FindRhs(sys, b);
            if (!rhs_a || !rhs_b) throw QuadratizeError(
                "internal: missing RHS for auxiliary factor " + a + " or " + b);

            ExprPtr term1 = MakeBinary(Binary::Op::Mul,
                Clone(*rhs_a), MakeFunction(b));
            ExprPtr term2 = MakeBinary(Binary::Op::Mul,
                MakeFunction(a), Clone(*rhs_b));
            ExprPtr raw = MakeBinary(Binary::Op::Add,
                std::move(term1), std::move(term2));

            // Распределяем произведения по суммам до подстановки,
            // чтобы x·(x² + x·q_1) превратилось в x·x² + x·x·q_1.
            raw = Distribute(std::move(raw));
            raw = SubstituteMonos(std::move(raw), st);
            raw = Simplify(std::move(raw));

            if (!IsQuadratic(*raw)) throw QuadratizeError(
                "internal: RHS still not quadratic: " + ToString(*raw));

            Equation new_eq;
            new_eq.lhs = MakeDerivative(name, 1);
            new_eq.rhs = std::move(raw);
            sys.equations.push_back(std::move(new_eq));
            sys.functions.push_back(name);
        }

        // Шаг 3: IC для новых переменных — по возрастанию степени.
        std::map<std::string, double> values;
        double t0 = 0.0;
        for (const auto& ic : sys.initial_conditions) {
            if (ic.order == 0) {
                values[ic.function_name] = ic.value;
                t0 = ic.t0;
            }
        }
        for (const auto& name : st.aux_order) {
            const auto& def = st.aux_defs.at(name);
            auto ia = values.find(def.first);
            auto ib = values.find(def.second);
            if (ia == values.end() || ib == values.end()) throw QuadratizeError(
                "internal: missing IC for auxiliary factor");
            double v = ia->second * ib->second;
            InitialCondition new_ic;
            new_ic.function_name = name;
            new_ic.order = 0;
            new_ic.t0 = t0;
            new_ic.value = v;
            sys.initial_conditions.push_back(new_ic);
            values[name] = v;
        }

        // Шаг 4: карта возврата.
        QuadratizeAuxiliary result;
        for (const auto& name : st.aux_order) {
            const auto& def = st.aux_defs.at(name);
            result[name] = MakeBinary(Binary::Op::Mul,
                MakeFunction(def.first), MakeFunction(def.second));
        }
        return result;
    }

} // namespace diffuri