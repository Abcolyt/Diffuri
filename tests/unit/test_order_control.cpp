// ============================================================================
// tests/unit/test_order_control.cpp
//
// Тесты модуля order_control: адаптивный выбор порядка M (§2.1.6, §2.3 статьи).
//
// Покрывает:
//   - MVP-поведение при отключённой адаптации;
//   - выбор M на первом шаге (§2.1.6);
//   - триггер |h/H| > m_factor (§2.3);
//   - спуск и подъём при смене M;
//   - отсутствие смены M, если ничего лучше не найдено;
//   - детерминизм.
// ============================================================================
#include <gtest/gtest.h>

#include <cmath>
#include <string>
#include <vector>

#include "input/input.h"
#include "solver/order_control.h"
#include "solver/solver.h"
#include "solver/taylor_spec.h"
#include "solver/taylor_table.h"

namespace diffuri {
    namespace {

        struct Fixture {
            RawSystem           sys;
            TaylorSpec          spec;
            std::vector<double> x0;
            TaylorTable         table;
            SolveOptions        opts;
            std::vector<double> t_p;

            Fixture()
                : sys(ParseSystem("x' = -x\nx(0) = 1\n")),
                spec(BuildTaylorSpec(sys)),
                x0{ 1.0 },
                table(spec, x0, 25) {
                opts.M_min = 5;
                opts.M_max = 20;
                opts.m_factor = 5.0;
                opts.enable_order_adaptation = false;
                opts.h_min = 1e-12;
                opts.h_max = 10.0;
                opts.rtol = 1e-10;
                opts.atol = 1e-12;
                opts.K = 1;

                // Дефолтный t_p: линейно растёт с p.
                t_p.resize(opts.M_max - opts.M_min + 1);
                for (std::size_t i = 0; i < t_p.size(); ++i) {
                    t_p[i] = 1e-6 * static_cast<double>(i + 5);
                }
            }
        };

        // =========================================================================
        // MVP-поведение: адаптация отключена
        // =========================================================================

        TEST(OrderControl, MVPRetunsCurrentM) {
            Fixture f;
            f.opts.enable_order_adaptation = false;
            auto dec = PickOrder(f.spec, f.table, 7, 1e-3, 1e-3, false, f.opts, f.t_p);
            EXPECT_EQ(dec.M, 7u);
            EXPECT_DOUBLE_EQ(dec.h, 0.0);  // h не вычислен
        }

        TEST(OrderControl, MVPZeroMBecomesTwenty) {
            Fixture f;
            f.opts.enable_order_adaptation = false;
            auto dec = PickOrder(f.spec, f.table, 0, 1e-3, 1e-3, false, f.opts, f.t_p);
            EXPECT_EQ(dec.M, 20u);
        }

        TEST(OrderControl, MVPSmallMIsPassedThrough) {
            Fixture f;
            f.opts.enable_order_adaptation = false;
            auto dec1 = PickOrder(f.spec, f.table, 1, 1e-3, 1e-3, false, f.opts, f.t_p);
            EXPECT_EQ(dec1.M, 1u);
            auto dec3 = PickOrder(f.spec, f.table, 3, 1e-3, 1e-3, false, f.opts, f.t_p);
            EXPECT_EQ(dec3.M, 3u);
        }

        // =========================================================================
        // §2.1.6: Выбор M на первом шаге
        // =========================================================================

        TEST(OrderControl, FirstStepSelectsBestM) {
            Fixture f;
            f.opts.enable_order_adaptation = true;
            // t_p растёт линейно: меньшие M "быстрее".
            // Но h(p) растёт с p. Должен найтись баланс.
            auto dec = PickOrder(f.spec, f.table, 10, 0.1, 0.1, true, f.opts, f.t_p);

            // M должен быть в допустимом диапазоне.
            EXPECT_GE(dec.M, f.opts.M_min);
            EXPECT_LE(dec.M, f.opts.M_max);
            // h должен быть положительным.
            EXPECT_GT(dec.h, 0.0);
            // H на первом шаге равен h.
            EXPECT_DOUBLE_EQ(dec.H, dec.h);
        }

        TEST(OrderControl, FirstStepIsDeterministic) {
            Fixture f;
            f.opts.enable_order_adaptation = true;
            auto dec1 = PickOrder(f.spec, f.table, 10, 0.1, 0.1, true, f.opts, f.t_p);
            auto dec2 = PickOrder(f.spec, f.table, 10, 0.1, 0.1, true, f.opts, f.t_p);
            EXPECT_EQ(dec1.M, dec2.M);
            EXPECT_DOUBLE_EQ(dec1.h, dec2.h);
        }

        // =========================================================================
        // §2.3: Триггер |h/H| > m_factor
        // =========================================================================

        TEST(OrderControl, TriggerFiresWhenRatioExceedsFactor) {
            Fixture f;
            f.opts.enable_order_adaptation = true;
            f.opts.m_factor = 2.0;
            // h/H = 3.0 > 2.0 → триггер срабатывает.
            auto dec = PickOrder(f.spec, f.table, 10, 3.0, 1.0, false, f.opts, f.t_p);
            // M может измениться или остаться, но H должен обновиться если M изменился.
            EXPECT_GE(dec.M, f.opts.M_min);
            EXPECT_LE(dec.M, f.opts.M_max);
        }

        TEST(OrderControl, TriggerDoesNotFireWhenRatioBelowFactor) {
            Fixture f;
            f.opts.enable_order_adaptation = true;
            f.opts.m_factor = 5.0;
            // h/H = 2.0 < 5.0 → триггер НЕ срабатывает, M не меняется.
            auto dec = PickOrder(f.spec, f.table, 10, 2.0, 1.0, false, f.opts, f.t_p);
            EXPECT_EQ(dec.M, 10u);
            EXPECT_DOUBLE_EQ(dec.H, 1.0);  // H не изменился
        }

        TEST(OrderControl, TriggerHandlesHBelowAnchor) {
            Fixture f;
            f.opts.enable_order_adaptation = true;
            f.opts.m_factor = 2.0;
            // h/H = 1/3 ≈ 0.33, |h/H| = 1/0.33 ≈ 3.0 > 2.0 → триггер.
            auto dec = PickOrder(f.spec, f.table, 10, 1.0, 3.0, false, f.opts, f.t_p);
            EXPECT_GE(dec.M, f.opts.M_min);
        }

        // =========================================================================
        // §2.3: Спуск и подъём
        // =========================================================================

        TEST(OrderControl, DescentFindsBetterM) {
            Fixture f;
            f.opts.enable_order_adaptation = true;
            f.opts.m_factor = 2.0;
            // Делаем меньшие M искусственно "быстрее": t_p растёт ЭКСПОНЕНЦИАЛЬНО.
            // Это гарантирует, что V(p) = h(p)/t(p) будет ПАДАТЬ с ростом p,
            // и алгоритм выберет меньший M при спуске.
            // (Линейный рост не работает, так как h(p) тоже растёт с p.)
            for (std::size_t i = 0; i < f.t_p.size(); ++i) {
                f.t_p[i] = 1e-6 * std::pow(5.0, static_cast<double>(i));
            }
            // Текущий M = 15, h/H = 3.0 > 2.0 → триггер.
            // Спуск должен найти меньший M с лучшим V(p).
            auto dec = PickOrder(f.spec, f.table, 15, 3.0, 1.0, false, f.opts, f.t_p);
            EXPECT_LT(dec.M, 15u);
            EXPECT_DOUBLE_EQ(dec.H, dec.h);  // H обновлён
        }

        TEST(OrderControl, AscentFindsBetterM) {
            Fixture f;
            f.opts.enable_order_adaptation = true;
            f.opts.m_factor = 2.0;
            f.opts.M_min = 5;
            f.opts.M_max = 20;
            // Делаем большие M искусственно "быстрее": t_p убывает.
            for (std::size_t i = 0; i < f.t_p.size(); ++i) {
                f.t_p[i] = 1e-3 / static_cast<double>(i + 5);
            }
            // Текущий M = 5, h/H = 3.0 > 2.0 → триггер.
            // Спуск не найдёт ничего (M = M_min). Подъём должен найти больший M.
            auto dec = PickOrder(f.spec, f.table, 5, 3.0, 1.0, false, f.opts, f.t_p);
            EXPECT_GT(dec.M, 5u);
        }

        TEST(OrderControl, NoBetterMFoundKeepsCurrent) {
            Fixture f;
            f.opts.enable_order_adaptation = true;
            f.opts.m_factor = 2.0;
            // t_p одинаковый для всех M: V(p) = h(p)/const.
            // h(p) монотонно растёт с p, значит лучший M = M_max.
            // Но если текущий M = M_max, спуск не найдёт ничего лучше,
            // подъём некуда идти.
            for (std::size_t i = 0; i < f.t_p.size(); ++i) {
                f.t_p[i] = 1e-6;
            }
            auto dec = PickOrder(f.spec, f.table, f.opts.M_max, 3.0, 1.0,
                false, f.opts, f.t_p);
            EXPECT_EQ(dec.M, f.opts.M_max);
        }

        // =========================================================================
        // Детерминизм и отсутствие побочных эффектов
        // =========================================================================

        TEST(OrderControl, DeterministicAcrossCalls) {
            Fixture f;
            f.opts.enable_order_adaptation = true;
            auto dec1 = PickOrder(f.spec, f.table, 10, 2.0, 1.0, false, f.opts, f.t_p);
            auto dec2 = PickOrder(f.spec, f.table, 10, 2.0, 1.0, false, f.opts, f.t_p);
            EXPECT_EQ(dec1.M, dec2.M);
            EXPECT_DOUBLE_EQ(dec1.h, dec2.h);
            EXPECT_DOUBLE_EQ(dec1.H, dec2.H);
        }

        TEST(OrderControl, SideEffectFree) {
            Fixture f;
            f.opts.enable_order_adaptation = false;
            auto dec1 = PickOrder(f.spec, f.table, 5, 1e-3, 1e-3, false, f.opts, f.t_p);
            auto dec2 = PickOrder(f.spec, f.table, 15, 1e-3, 1e-3, false, f.opts, f.t_p);
            auto dec3 = PickOrder(f.spec, f.table, 5, 1e-3, 1e-3, false, f.opts, f.t_p);
            EXPECT_EQ(dec1.M, 5u);
            EXPECT_EQ(dec2.M, 15u);
            EXPECT_EQ(dec3.M, 5u);
        }

        // =========================================================================
        // Граничные случаи
        // =========================================================================

        TEST(OrderControl, EmptyTpDisablesAdaptation) {
            Fixture f;
            f.opts.enable_order_adaptation = true;
            std::vector<double> empty_tp;
            // Пустой t_p + enable_order_adaptation = true — функция должна
            // корректно отработать (адаптация фактически отключена).
            EXPECT_NO_THROW(
                PickOrder(f.spec, f.table, 10, 1.0, 1.0, false, f.opts, empty_tp)
            );
        }

        TEST(OrderControl, ClampsMToRange) {
            Fixture f;
            f.opts.enable_order_adaptation = false;
            f.opts.M_min = 5;
            f.opts.M_max = 20;
            // M = 100 вне диапазона, но при отключённой адаптации — pass-through.
            auto dec = PickOrder(f.spec, f.table, 100, 1e-3, 1e-3, false, f.opts, f.t_p);
            // В MVP (отключённая адаптация) клампа нет, M возвращается как есть.
            EXPECT_EQ(dec.M, 100u);
        }

        TEST(OrderControl, InvalidRangeDisablesAdaptation) {
            Fixture f;
            f.opts.enable_order_adaptation = true;
            f.opts.M_min = 20;
            f.opts.M_max = 5;  // M_min > M_max — некорректный диапазон
            auto dec = PickOrder(f.spec, f.table, 10, 1.0, 1.0, true, f.opts, f.t_p);
            // Должен вернуть M как есть (pass-through).
            EXPECT_EQ(dec.M, 10u);
        }

        // =========================================================================
    // Граничные случаи диапазона и триггера
    // =========================================================================

    // Граница триггера: |h/H| == m_factor НЕ должно срабатывать (ТЗ: строго >).
        TEST(OrderControl, TriggerAtExactFactorDoesNotFire) {
            Fixture f;
            f.opts.enable_order_adaptation = true;
            f.opts.m_factor = 5.0;
            // h/H = 5.0 == m_factor → триггер НЕ срабатывает.
            auto dec = PickOrder(f.spec, f.table, 10, 5.0, 1.0, false, f.opts, f.t_p);
            EXPECT_EQ(dec.M, 10u);            // M не меняется
            EXPECT_DOUBLE_EQ(dec.H, 1.0);     // H не меняется
        }

        // Спуск на нижнем краю: M == M_min, спуск невозможен, не падает.
        TEST(OrderControl, DescentAtMMinNoUnderflow) {
            Fixture f;
            f.opts.enable_order_adaptation = true;
            f.opts.m_factor = 2.0;
            auto dec = PickOrder(f.spec, f.table, f.opts.M_min, 3.0, 1.0,
                false, f.opts, f.t_p);
            EXPECT_GE(dec.M, f.opts.M_min);
            EXPECT_LE(dec.M, f.opts.M_max);
        }

        // Подъём на верхнем краю: M == M_max, подъём невозможен, не падает.
        TEST(OrderControl, AscentAtMMaxNoOverflow) {
            Fixture f;
            f.opts.enable_order_adaptation = true;
            f.opts.m_factor = 2.0;
            auto dec = PickOrder(f.spec, f.table, f.opts.M_max, 3.0, 1.0,
                false, f.opts, f.t_p);
            EXPECT_GE(dec.M, f.opts.M_min);
            EXPECT_LE(dec.M, f.opts.M_max);
        }

        // M ниже M_min при включённой адаптации клампится в диапазон.
        TEST(OrderControl, MBelowMinClampedWhenEnabled) {
            Fixture f;
            f.opts.enable_order_adaptation = true;
            f.opts.m_factor = 5.0;  // h/H = 1 → триггера нет
            auto dec = PickOrder(f.spec, f.table, 1, 1.0, 1.0, false, f.opts, f.t_p);
            EXPECT_GE(dec.M, f.opts.M_min);
        }

        // H == 0 обрабатывается без деления на ноль.
        TEST(OrderControl, ZeroHAnchorHandled) {
            Fixture f;
            f.opts.enable_order_adaptation = true;
            EXPECT_NO_THROW(
                PickOrder(f.spec, f.table, 10, 1.0, 0.0, false, f.opts, f.t_p));
        }

        // Защита от бесконечного цикла при M_min == 0 (unsigned underflow в спуске).
        // Без фикса в order_control.cpp этот тест зависнет.
        TEST(OrderControl, DescentWithZeroMMinTerminates) {
            Fixture f;
            f.opts.enable_order_adaptation = true;
            f.opts.M_min = 0;
            f.opts.M_max = 20;
            f.opts.m_factor = 2.0;
            f.t_p.resize(f.opts.M_max - f.opts.M_min + 1);
            for (std::size_t i = 0; i < f.t_p.size(); ++i) {
                f.t_p[i] = 1e-6 * std::pow(5.0, static_cast<double>(i));
            }
            auto dec = PickOrder(f.spec, f.table, 2, 3.0, 1.0, false, f.opts, f.t_p);
            EXPECT_LE(dec.M, 2u);
        }

    }  // namespace
}  // namespace diffuri
