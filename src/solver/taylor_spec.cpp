// ============================================================================
// src/solver/taylor_spec.cpp
//
// Реализация построения спецификации TaylorSpec из RawSystem.
// ============================================================================
#include "solver/taylor_spec.h"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <variant>

#include "solver/solver.h"

namespace diffuri {

    namespace {

        // Внутреннее представление монома при обходе: коэффициент + отсортированный
        // список имён сомножителей (пустой для константы).
        struct Term {
            double                   coeff = 0.0;
            std::vector<std::string> factors;
        };

        // Рекурсивный обход полиномиального RHS с раскрытием сумм и произведений.
        // sign — накопленный знак (для Sub и Unary::Neg).
        [[nodiscard]] std::vector<Term> CollectTerms(const Expr& e) {
            std::vector<Term> result;

            struct Walker {
                std::vector<Term>& out;

                void operator()(const Number& n) const {
                    out.push_back({ n.value, {} });
                }
                void operator()(const Function& f) const {
                    out.push_back({ 1.0, {f.name} });
                }
                void operator()(const Constant& c) const {
                    out.push_back({ c.value, {} });
                }
                void operator()(const Derivative&) const {
                    throw SolverError("TaylorSpec: RHS contains Derivative");
                }
                void operator()(const Unary& u) const {
                    auto sub = CollectTerms(*u.operand);
                    for (auto& t : sub) {
                        t.coeff = -t.coeff;
                        out.push_back(std::move(t));
                    }
                }
                void operator()(const Call&) const {
                    throw SolverError("TaylorSpec: RHS contains function call "
                        "(not polynomial)");
                }
                void operator()(const Binary& b) const {
                    using Op = Binary::Op;
                    switch (b.op) {
                    case Op::Add: {
                        auto l = CollectTerms(*b.lhs);
                        auto r = CollectTerms(*b.rhs);
                        out.insert(out.end(),
                            std::make_move_iterator(l.begin()),
                            std::make_move_iterator(l.end()));
                        out.insert(out.end(),
                            std::make_move_iterator(r.begin()),
                            std::make_move_iterator(r.end()));
                        break;
                    }
                    case Op::Sub: {
                        auto l = CollectTerms(*b.lhs);
                        auto r = CollectTerms(*b.rhs);
                        out.insert(out.end(),
                            std::make_move_iterator(l.begin()),
                            std::make_move_iterator(l.end()));
                        for (auto& t : r) {
                            t.coeff = -t.coeff;
                            out.push_back(std::move(t));
                        }
                        break;
                    }
                    case Op::Mul: {
                        auto l = CollectTerms(*b.lhs);
                        auto r = CollectTerms(*b.rhs);
                        for (const auto& a : l) {
                            for (const auto& c : r) {
                                Term t;
                                t.coeff = a.coeff * c.coeff;
                                t.factors = a.factors;
                                t.factors.insert(t.factors.end(),
                                    c.factors.begin(), c.factors.end());
                                if (t.factors.size() > 2) {
                                    throw SolverError("TaylorSpec: RHS contains "
                                        "monomial of degree > 2");
                                }
                                std::sort(t.factors.begin(), t.factors.end());
                                out.push_back(std::move(t));
                            }
                        }
                        break;
                    }
                    case Op::Pow: {
                        // Допустимо только Function^k для целого k ∈ {0,1,2};
                        // для остальных случаев — ошибка.
                        auto base = CollectTerms(*b.lhs);
                        if (base.size() != 1 || base[0].factors.size() != 1) {
                            throw SolverError("TaylorSpec: unsupported Pow in RHS");
                        }
                        if (!std::holds_alternative<Number>(b.rhs->value)) {
                            throw SolverError("TaylorSpec: non-constant exponent");
                        }
                        const double p = std::get<Number>(b.rhs->value).value;
                        const std::string& fname = base[0].factors[0];
                        if (std::abs(p) < 1e-12) {
                            out.push_back({ 1.0, {} });
                        }
                        else if (std::abs(p - 1.0) < 1e-12) {
                            out.push_back({ base[0].coeff, {fname} });
                        }
                        else if (std::abs(p - 2.0) < 1e-12) {
                            out.push_back({ base[0].coeff, {fname, fname} });
                        }
                        else {
                            throw SolverError("TaylorSpec: RHS contains monomial "
                                "of degree > 2");
                        }
                        break;
                    }
                    case Op::Div:
                        throw SolverError("TaylorSpec: RHS contains division "
                            "(not polynomial)");
                    }
                }
            };

            std::visit(Walker{ result }, e.value);
            return result;
        }

        [[nodiscard]] std::string MakeMonomialKey(const std::vector<std::string>& factors) {
            if (factors.empty()) return "1";
            std::string s = factors[0];
            for (std::size_t i = 1; i < factors.size(); ++i) {
                s += '*';
                s += factors[i];
            }
            return s;
        }

        [[nodiscard]] std::vector<std::string> SplitKey(const std::string& key) {
            if (key == "1") return {};
            std::vector<std::string> out;
            std::istringstream iss(key);
            std::string part;
            while (std::getline(iss, part, '*')) out.push_back(part);
            return out;
        }

    }  // namespace

    TaylorSpec BuildTaylorSpec(const RawSystem& sys) {
        TaylorSpec spec;
        spec.n = sys.functions.size();
        spec.function_names = sys.functions;

        // Карта: имя функции → 1-based линейный индекс.
        std::map<std::string, std::size_t> func_index;
        for (std::size_t j = 0; j < sys.functions.size(); ++j) {
            func_index[sys.functions[j]] = j + 1;
        }

        // Собираем термы всех уравнений.
        std::vector<std::vector<Term>> per_equation(sys.equations.size());
        std::vector<std::string>       nonlinear_keys;
        std::map<std::string, std::size_t> monomial_id;
        monomial_id["1"] = 0;
        for (std::size_t j = 0; j < sys.functions.size(); ++j) {
            monomial_id[sys.functions[j]] = j + 1;
        }

        for (std::size_t j = 0; j < sys.equations.size(); ++j) {
            const Equation& eq = sys.equations[j];

            // Валидация lhs: должен быть Derivative{f, 1}, f ∈ sys.functions.
            if (!std::holds_alternative<Derivative>(eq.lhs->value)) {
                throw SolverError("TaylorSpec: equation LHS is not Derivative");
            }
            const auto& d = std::get<Derivative>(eq.lhs->value);
            if (d.order != 1) {
                throw SolverError("TaylorSpec: equation LHS order != 1");
            }
            if (func_index.find(d.function_name) == func_index.end()) {
                throw SolverError("TaylorSpec: unknown function on LHS: " +
                    d.function_name);
            }

            auto terms = CollectTerms(*eq.rhs);

            // Проверяем, что все имена сомножителей — известные функции.
            for (const auto& t : terms) {
                for (const auto& f : t.factors) {
                    if (func_index.find(f) == func_index.end()) {
                        throw SolverError("TaylorSpec: unknown function in RHS: " + f);
                    }
                }
            }

            // Свёртка коэффициентов по одинаковым мономам.
            std::map<std::string, double> combined;
            for (const auto& t : terms) {
                const std::string key = MakeMonomialKey(t.factors);
                combined[key] += t.coeff;
            }

            // Запоминаем термы и регистрируем новые нелинейные мономы.
            per_equation[j].clear();
            for (auto& [key, coef] : combined) {
                auto factors = SplitKey(key);
                if (factors.size() >= 2 &&
                    monomial_id.find(key) == monomial_id.end()) {
                    monomial_id[key] = 0;  // placeholder
                    nonlinear_keys.push_back(key);
                }
                per_equation[j].push_back({ coef, std::move(factors) });
            }
        }

        // Сортируем нелинейные мономы для детерминизма.
        std::sort(nonlinear_keys.begin(), nonlinear_keys.end());

        // Заполняем monomial_keys.
        spec.u = spec.n + nonlinear_keys.size();
        spec.monomial_keys.assign(spec.u + 1, "");
        spec.monomial_keys[0] = "1";
        for (std::size_t j = 0; j < sys.functions.size(); ++j) {
            spec.monomial_keys[j + 1] = sys.functions[j];
        }
        for (std::size_t k = 0; k < nonlinear_keys.size(); ++k) {
            const std::size_t idx = spec.n + 1 + k;
            spec.monomial_keys[idx] = nonlinear_keys[k];
            monomial_id[nonlinear_keys[k]] = idx;
        }

        // Схема S.
        spec.scheme.assign(spec.u + 1, { 0, 0 });
        for (std::size_t r = spec.n + 1; r <= spec.u; ++r) {
            auto factors = SplitKey(spec.monomial_keys[r]);
            if (factors.size() != 2) {
                throw SolverError("TaylorSpec: unexpected nonlinear monomial degree");
            }
            auto it_p = func_index.find(factors[0]);
            auto it_q = func_index.find(factors[1]);
            if (it_p == func_index.end() || it_q == func_index.end()) {
                throw SolverError("TaylorSpec: unknown variable in monomial");
            }
            spec.scheme[r] = { it_p->second, it_q->second };
        }

        // Разреженные коэффициенты a[j][l].
        spec.a.assign(spec.n, {});
        for (std::size_t j = 0; j < sys.equations.size(); ++j) {
            const auto& d = std::get<Derivative>(sys.equations[j].lhs->value);
            const std::size_t jvar = func_index.at(d.function_name) - 1;
            for (const auto& t : per_equation[j]) {
                const std::string key = MakeMonomialKey(t.factors);
                auto it = monomial_id.find(key);
                if (it == monomial_id.end()) {
                    throw SolverError("TaylorSpec: internal error (monomial not indexed)");
                }
                spec.a[jvar][it->second] += t.coeff;
            }
        }

        return spec;
    }

}  // namespace diffuri