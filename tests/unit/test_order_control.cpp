// ============================================================================
// tests/unit/test_order_control.cpp
//
// Тесты MVP order_control: чистый pass-through.
//
// Полные 2.1.5, 2.1.6, 2.3 статьи [Бабаджанянц, Большаков 2012] появятся
// в итерации 3; тогда же потребуются тесты на поведение при |h/H| > m
// и на кламп в [Mmin, Mmax]. Сейчас такие тесты смысла не имеют:
// функция возвращает M независимо от h и H.
// ============================================================================
#include <gtest/gtest.h>

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

            Fixture()
                : sys(ParseSystem("x' = -x\nx(0) = 1\n")),
                spec(BuildTaylorSpec(sys)),
                x0{ 1.0 },
                table(spec, x0, 4) {
            }
        };

    }  // namespace

    // --- PickOrder: возвращает M_current, если M != 0 ---------------------------

    TEST(OrderControl, ReturnsCurrentM) {
        Fixture f;
        // h и H в MVP не используются, но должны быть переданы.
        EXPECT_EQ(PickOrder(f.table, 7, 1e-3, 1e-3, f.opts), 7u);
        EXPECT_EQ(PickOrder(f.table, 20, 1e-2, 1e-2, f.opts), 20u);
        EXPECT_EQ(PickOrder(f.table, 1, 1e-4, 1e-3, f.opts), 1u);
    }

    // --- PickOrder: аргумент M == 0 → 20 ----------------------------------------

    TEST(OrderControl, ZeroMBecomesTwenty) {
        Fixture f;
        EXPECT_EQ(PickOrder(f.table, 0, 1e-3, 1e-3, f.opts), 20u);
    }

    // --- PickOrder: малые M проходят без клампа ---------------------------------
    //
    // В MVP клампа в [Mmin, Mmax] нет — он появится вместе с полной 2.3.
    // Тест фиксирует, что pass-through честный: Solve с opts.M = 1
    // вернёт order_used = 1 (совместимо с Solver-тестами, не завязанными
    // на адаптивный порядок).

    TEST(OrderControl, SmallMIsPassedThroughUnchanged) {
        Fixture f;
        EXPECT_EQ(PickOrder(f.table, 1, 1e-3, 1e-3, f.opts), 1u);
        EXPECT_EQ(PickOrder(f.table, 3, 1e-3, 1e-3, f.opts), 3u);
    }

    // --- PickOrder: детерминизм -------------------------------------------------

    TEST(OrderControl, Deterministic) {
        Fixture f;
        const auto a = PickOrder(f.table, 10, 1e-3, 1e-3, f.opts);
        const auto b = PickOrder(f.table, 10, 1e-3, 1e-3, f.opts);
        EXPECT_EQ(a, b);
    }

    // --- PickOrder: отсутствие побочных эффектов --------------------------------
    //
    // Функция не должна кэшировать состояние в static-переменных:
    // разные входные M дают разные выходные M, а повторный вызов с тем же M
    // не «помнит» предыдущий результат. Это свойство важно сохранить и в
    // полной 2.3 (там появится соблазн кэшировать t(p) в static).

    TEST(OrderControl, SideEffectFree) {
        Fixture f;
        EXPECT_EQ(PickOrder(f.table, 5, 1e-3, 1e-3, f.opts), 5u);
        EXPECT_EQ(PickOrder(f.table, 15, 1e-3, 1e-3, f.opts), 15u);
        EXPECT_EQ(PickOrder(f.table, 5, 1e-3, 1e-3, f.opts), 5u);  // снова 5
    }

    // =========================================================================
// Дополнительное покрытие: большие M, независимость от opts.M,
// независимость от содержимого таблицы, устойчивость правила M == 0.
// =========================================================================

// --- Большие M проходят без обрезки ----------------------------------------
//
// В MVP границ [Mmin, Mmax] нет — кламп появится вместе с полной 2.3.
// Тест фиксирует, что pass-through честный и на больших значениях
// (иначе неявный кламп в 60 «сломает» Solver.AccuracyGrowsWithM
// при opts.M > 60).
    TEST(OrderControl, LargeMPassedThrough) {
        Fixture f;
        EXPECT_EQ(PickOrder(f.table, 100, 1e-3, 1e-3, f.opts), 100u);
        EXPECT_EQ(PickOrder(f.table, 1000, 1e-3, 1e-3, f.opts), 1000u);
    }

    // --- opts.M игнорируется ---------------------------------------------------
    //
    // Единственный источник M в MVP — позиционный аргумент M, а не opts.M.
    // Тест ловит случайное «прочитал opts.M вместо arg M».
    TEST(OrderControl, IndependentFromOptsM) {
        Fixture f;
        f.opts.M = 5;
        EXPECT_EQ(PickOrder(f.table, 10, 1e-3, 1e-3, f.opts), 10u);
        f.opts.M = 100;
        EXPECT_EQ(PickOrder(f.table, 10, 1e-3, 1e-3, f.opts), 10u);
        f.opts.M = 0;
        EXPECT_EQ(PickOrder(f.table, 10, 1e-3, 1e-3, f.opts), 10u);
    }

    // --- Правило M == 0 → 20 не «запоминается» ---------------------------------
    //
    // Проверяет, что семантика 0 → 20 не залипает в static-состоянии:
    // после вызова с M == 7 следующий вызов с M == 0 всё равно даёт 20,
    // а следующий за ним с M == 7 — снова 7. Дополняет SideEffectFree,
    // который проверяет только сохранение самого M.
    TEST(OrderControl, ZeroRuleIsStateless) {
        Fixture f;
        EXPECT_EQ(PickOrder(f.table, 7, 1e-3, 1e-3, f.opts), 7u);
        EXPECT_EQ(PickOrder(f.table, 0, 1e-3, 1e-3, f.opts), 20u);
        EXPECT_EQ(PickOrder(f.table, 0, 1e-3, 1e-3, f.opts), 20u);
        EXPECT_EQ(PickOrder(f.table, 7, 1e-3, 1e-3, f.opts), 7u);
    }

    // --- Результат не зависит от содержимого таблицы ---------------------------
    //
    // В MVP PickOrder не смотрит в table. Тест фиксирует это: две разные
    // системы (линейная и квадратичная) с одинаковым M дают одинаковый
    // результат. Ловит случайное «заглядывание» в spec/table при будущих
    // правках, которое сломало бы pass-through контракт.
    TEST(OrderControl, IndependentFromTableContents) {
        Fixture f;

        const auto sys2 = ParseSystem("x' = x^2\nx(0) = 1\n");
        const auto spec2 = BuildTaylorSpec(sys2);
        TaylorTable table2(spec2, { 1.0 }, 25);

        EXPECT_EQ(PickOrder(f.table, 10, 1e-3, 1e-3, f.opts),
            PickOrder(table2, 10, 1e-3, 1e-3, f.opts));
    }

}  // namespace diffuri