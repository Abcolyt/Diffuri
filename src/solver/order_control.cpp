// ============================================================================
// src/solver/order_control.cpp
//
// MVP: чистый pass-through. Возвращает текущий M (либо 20, если M == 0).
//
// Полные алгоритмы 2.1.5, 2.1.6, 2.3 статьи
// [Бабаджанянц, Большаков 2012] «Реализация метода рядов Тейлора…»,
// Вычислительные методы и программирование, 2012, т. 13, с. 497–510,
// НЕ реализованы. Обоснование и точка расширения — ниже.
//
// ----------------------------------------------------------------------------
// ПОЧЕМУ НЕ РЕАЛИЗОВАНО В MVP
// ----------------------------------------------------------------------------
//
// 1) §2.1.5 (градуировка) требует измерить t(p) — процессорное время
//    построения таблицы Тейлора до порядка p — ОДИН РАЗ на старте Solve.
//    PickOrder не имеет состояния: он вызывается на каждом шаге и не может
//    ни запомнить t(p), ни отличить первый вызов от последующих. Локальный
//    static std::vector<double> решил бы задачу, но привносит:
//      - недетерминизм между повторными Solve / тестами / потоками;
//      - нарушение property "SideEffectFree"
//        (см. tests/unit/test_order_control.cpp).
//    Правильное решение — хранить градуировку в solver.cpp и передавать её
//    в PickOrder через opts или отдельным параметром. Это изменение
//    архитектуры, выходящее за рамки ТЗ агента 4.
//
// 2) §2.3 (переключение порядка) требует для каждого p вычислить h(p) —
//    шаг, который вернул бы PickStep на таблице порядка p:
//
//        TaylorTable table_p(spec, table.X0(), p + opts.K);
//        double h_p = PickStep(table_p, h, p, opts);
//
//    Параметр spec в сигнатуру PickOrder НЕ проброшен. Пересобрать
//    таблицу порядка p > MaxOrder() невозможно; использовать существующую
//    table с p < MaxOrder() можно лишь как грубую замену (под-таблица при
//    p+K ≤ MaxOrder() корректна, но V(p) всё равно нечем нормировать,
//    см. п. 3).
//
// 3) Без t(p) замена V(p) ≈ h(p) даёт систематический сдвиг: время
//    построения таблицы растёт как минимум линейно по p, поэтому
//    V(p) = h(p)/t(p) при больших p ведёт себя иначе, чем h(p). Такой
//    «упрощённый 2.3» может уводить M вверх (или вниз — зависит от
//    системы) и работать хуже, чем честный no-op. ТЗ прямо запрещает
//    имитировать градуировку.
//
// Кламп в [Mmin, Mmax] здесь СОЗНАТЕЛЬНО отсутствует: он — часть
// алгоритма 2.3 и без градуировки t(p) бессмыслен. Единственное
// преобразование MVP — «M == 0» трактуется как «не задан» и заменяется
// на литерал 20 (совместимо с Solver.ZeroMMeansTwenty).
//
// ----------------------------------------------------------------------------
// ПСЕВДОКОД §2.3 ДЛЯ ИТЕРАЦИИ 3
// ----------------------------------------------------------------------------
//
//   constexpr std::size_t kMmin = 5;
//   constexpr std::size_t kMmax = 60;
//   constexpr double      kSwitchFactor = 5.0;   // m из статьи
//
//   std::size_t PickOrder(const TaylorSpec& spec,
//                         const TaylorTable& table,
//                         std::size_t M,
//                         double h, double H,
//                         const SolveOptions& opts,
//                         const std::vector<double>& t /* t(p), p=Mmin..Mmax */) {
//       if (M == 0) M = 20;                       // «не задан»
//       M = std::clamp(M, kMmin, kMmax);
//
//       // Триггер §2.3: шаг изменился более чем в m раз с момента
//       // последней смены M.
//       if (!(std::abs(h / H) > kSwitchFactor)) return M;
//
//       // V(M) = h(M) / t(M) — базовая скорость.
//       const double hM = PickStep(table, h, M, opts);
//       const double VM = hM / t[M - kMmin];
//
//       // Спуск: p = M-1, ..., Mmin — ищем p с V(p) >= V(M).
//       for (std::size_t p = M - 1; p + 1 > kMmin; --p) {
//           if (p + opts.K > table.MaxOrder()) continue;   // нельзя пересобрать
//           TaylorTable table_p(spec, table.X0(), p + opts.K);
//           const double h_p = PickStep(table_p, h, p, opts);
//           const double V_p = h_p / t[p - kMmin];
//           if (V_p >= VM) return p;
//       }
//
//       // Подъём: p = M+1, ..., Mmax — первая p с V(p) >= V(M).
//       for (std::size_t p = M + 1; p <= kMmax; ++p) {
//           if (p + opts.K > table.MaxOrder()) break;      // дальше нет смысла
//           TaylorTable table_p(spec, table.X0(), p + opts.K);
//           const double h_p = PickStep(table_p, h, p, opts);
//           const double V_p = h_p / t[p - kMmin];
//           if (V_p >= VM) return p;
//       }
//
//       return M;   // ничего не нашли — порядок не меняется
//   }
//
// Переход к этой версии требует:
//   - пробросить spec в сигнатуру PickOrder (или в opts);
//   - провести градуировку t(p) в solver.cpp до главного цикла и передать
//     её в PickOrder;
//   - обновить Solver-тесты: ZeroMMeansTwenty и AccuracyGrowsWithM
//     придётся ослабить (order_used >= 1) либо зафиксировать M через opts.
//
// ============================================================================
#include "solver/order_control.h"

namespace diffuri {

    std::size_t PickOrder(const TaylorTable& /*table*/,
        std::size_t M,
        double /*h*/,
        double /*H*/,
        const SolveOptions& /*opts*/) {
        // MVP: pass-through. Единственное преобразование — M == 0
        // («не задан») превращается в литерал 20. Кламп в [Mmin, Mmax]
        // и триггер |h/H| > m отсутствуют — см. шапку файла.
        if (M == 0) return 20;
        return M;
    }

}  // namespace diffuri