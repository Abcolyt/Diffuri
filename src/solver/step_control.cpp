// ============================================================================
// src/solver/step_control.cpp
//
// Адаптивный выбор шага по статье
// [Бабаджанянц, Большаков 2012] «Реализация метода рядов Тейлора…»,
// Вычислительные методы и программирование, 2012, т. 13, с. 497–510.
//
// Реализовано:
//   - §2.1.4 — стандартный апостериорный пересчёт шага по нормированной
//     локальной погрешности:
//
//         ε(h)  = ErrorEstimate(table, x, h, M, K, opts)   — норма L² (§2.1.4)
//         h_b   = h · (1 / ε(h))^{1/(M+1)}
//
//     Показатель 1/(M+1) отвечает первому отбрасываемому члену δT_{M,K},
//     стартующему с h^{M+1}.
//   - Ограничение роста h_new ≤ kMaxGrowthFactor · h.
//     В статье (§2.1.3) эту роль играет итеративная коррекция шага,
//     в MVP отсутствующая; 2.0 — консервативный стандарт для адаптивных
//     RK-методов (Hairer, Wanner, Norsett). Без него шаг прыгает
//     с h_init прямо в h_max за один вызов.
//   - Финальный зажим результата в [opts.h_min, opts.h_max].
//
// НЕ реализовано в MVP и почему:
//   - §2.1.2 (априорный шаг h_a = τ · ρ) и §2.2 (h = max(h_a, h_b)):
//     ρ требует ConvergenceRadius(spec, α), но spec в сигнатуру PickStep
//     не проброшен. При ρ = 1.0 и rtol = 1e-10 обратная функция остатка
//     даёт τ ≈ 0.33, что для дефолтного h_max = 0.1 всегда упирается
//     в верхнюю границу и вырождает выбор шага в константу h_max.
//   - §2.1.3 (итеративная коррекция): дорого (несколько ErrorEstimate).
//
// Точка расширения: при появлении spec в сигнатуре —
//   α = convergence::ScalingMultipliers(x);
//   ρ = convergence::ConvergenceRadius(spec, α);
//   τ = convergence::InverseV(opts.rtol);
//   h_a = τ · ρ;
//   return clamp(max(h_a, h_b), h_min, h_max);
// ============================================================================
#include "solver/step_control.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

#include "solver/error_control.h"

namespace diffuri {

    namespace {

        // Максимальный рост шага за один вызов PickStep.
        // Компенсирует отсутствующий §2.1.3 статьи.
        constexpr double kMaxGrowthFactor = 2.0;

    }  // namespace

    double PickStep(const TaylorTable& table,
        double h,
        std::size_t M,
        const SolveOptions& opts) {
        // Защита от некорректных входных данных.
        if (!(h > 0.0)) return opts.h_min;
        if (M == 0)     return std::clamp(h, opts.h_min, opts.h_max);

        const std::vector<double>& x = table.X0();

        // §2.1.4: нормированная локальная погрешность на пробном шаге h.
        // ε(h) = sqrt( (1/n) Σ_i ( δT_i(h) /
        //                          (atol + rtol · max(|x_i|, |T_{M,i}(h)|)) )² )
        const double eps = ErrorEstimate(table, x, h, M, opts.K, opts);

        // §2.1.4: h_b = h · (1/ε)^{1/(M+1)}.
        // ε ≤ 0 (в т.ч. машинный нуль) трактуем как «шаг можно смело
        // увеличивать»: h_b → +∞, дальше его ограничит kMaxGrowthFactor.
        double h_b;
        if (eps > 0.0) {
            const double inv_exponent = 1.0 / static_cast<double>(M + 1);
            h_b = h * std::pow(1.0 / eps, inv_exponent);
        }
        else {
            h_b = std::numeric_limits<double>::infinity();
        }

        // Ограничение роста + зажим в пользовательские границы.
        const double h_capped = std::min(h_b, kMaxGrowthFactor * h);
        return std::clamp(h_capped, opts.h_min, opts.h_max);
    }

}  // namespace diffuri