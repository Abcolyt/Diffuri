// ============================================================================
// src/solver/taylor_table.cpp
//
// Реализация таблицы коэффициентов Тейлора (формулы (4)–(5) статьи).
// ============================================================================
#include "solver/taylor_table.h"

#include <stdexcept>

#include "solver/solver.h"

namespace diffuri {

    TaylorTable::TaylorTable(const TaylorSpec& spec,
        const std::vector<double>& x0,
        std::size_t max_order)
        : n_(spec.n), u_(spec.u), max_order_(max_order), x0_(x0) {
        if (x0.size() != spec.n) {
            throw SolverError("TaylorTable: x0.size() != spec.n");
        }
        table_.assign(spec.u + 1, std::vector<double>(max_order + 1, 0.0));

        // p = 0: начальные значения.
        table_[0][0] = 1.0;  // x^{i(0)} = 1
        for (std::size_t k = 1; k <= spec.n; ++k) {
            table_[k][0] = x0[k - 1];
        }
        for (std::size_t k = spec.n + 1; k <= spec.u; ++k) {
            const auto [p, q] = spec.scheme[k];
            table_[k][0] = table_[p][0] * table_[q][0];
        }

        // Рекуррентные формулы (4)–(5).
        for (std::size_t p = 0; p < max_order; ++p) {
            // Линейные: x_{k,p+1} = (p+1)^{-1} * Σ_l a[k-1][l] * x_{l,p}.
            for (std::size_t k = 1; k <= spec.n; ++k) {
                double acc = 0.0;
                for (const auto& [l, coef] : spec.a[k - 1]) {
                    acc += coef * table_[l][p];
                }
                table_[k][p + 1] = acc / static_cast<double>(p + 1);
            }
            // Нелинейные: x_{k,p+1} = Σ_{l=0}^{p+1} x_{p(k),l} * x_{q(k),p+1-l}.
            for (std::size_t k = spec.n + 1; k <= spec.u; ++k) {
                const auto [pk, qk] = spec.scheme[k];
                double acc = 0.0;
                for (std::size_t l = 0; l <= p + 1; ++l) {
                    acc += table_[pk][l] * table_[qk][p + 1 - l];
                }
                table_[k][p + 1] = acc;
            }
        }
    }

    const std::vector<double>& TaylorTable::X0() const noexcept {
        return x0_;
    }

    std::vector<double> TaylorTable::Evaluate(double h, std::size_t M) const {
        if (M > max_order_) {
            throw std::out_of_range("TaylorTable::Evaluate: M > max_order");
        }
        std::vector<double> result(n_, 0.0);
        double hp = 1.0;
        for (std::size_t p = 0; p <= M; ++p) {
            for (std::size_t i = 0; i < n_; ++i) {
                result[i] += table_[i + 1][p] * hp;
            }
            hp *= h;
        }
        return result;
    }

    std::vector<double> TaylorTable::DiffPoly(double h,
        std::size_t M,
        std::size_t K) const {
        if (M + K > max_order_) {
            throw std::out_of_range("TaylorTable::DiffPoly: M+K > max_order");
        }
        std::vector<double> result(n_, 0.0);
        // hp стартует как h^(M+1).
        double hp = 1.0;
        for (std::size_t p = 0; p <= M; ++p) hp *= h;
        for (std::size_t p = M + 1; p <= M + K; ++p) {
            for (std::size_t i = 0; i < n_; ++i) {
                result[i] += table_[i + 1][p] * hp;
            }
            hp *= h;
        }
        return result;
    }

    double TaylorTable::Coeff(std::size_t k, std::size_t p) const {
        return table_.at(k).at(p);
    }

    std::size_t TaylorTable::MaxOrder() const noexcept {
        return max_order_;
    }

    std::size_t TaylorTable::MonomialCount() const noexcept {
        return u_ + 1;
    }

}  // namespace diffuri