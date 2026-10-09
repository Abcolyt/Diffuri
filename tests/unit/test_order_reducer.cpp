// ============================================================================
// tests/unit/test_reduce_order.cpp
//
// Unit- и property-тесты OrderReducer.
// ============================================================================
#include <gtest/gtest.h>

#include <map>
#include <set>
#include <string>
#include <variant>
#include <vector>

#include "input/input.h"
#include "input/parser.h"
#include "normalize/normalize.h"
#include "order_reducer/order_reducer.h"

namespace diffuri {
    namespace {

        // Хелпер: распарсить (ParseSystem сам валидирует), нормализовать,
        // понизить порядок.
        RawSystem ReduceText(const std::string& text) {
            RawSystem sys = ParseSystem(text);
            NormalizeSystem(sys);
            ReduceOrder(sys);
            return sys;
        }

        std::string LhsString(const Equation& eq) { return ToString(*eq.lhs); }
        std::string RhsString(const Equation& eq) { return ToString(*eq.rhs); }

        int LhsOrder(const Equation& eq) {
            return std::get<Derivative>(eq.lhs->value).order;
        }

        std::string LhsFunc(const Equation& eq) {
            return std::get<Derivative>(eq.lhs->value).function_name;
        }

        // ========================================================================
        // Unit: базовые сценарии
        // ========================================================================

        TEST(ReduceOrder, FirstOrderUnchanged) {
            auto sys = ReduceText("x' = -x\nx(0) = 1\n");
            ASSERT_EQ(sys.equations.size(), 1u);
            ASSERT_EQ(sys.functions.size(), 1u);
            EXPECT_EQ(sys.functions[0], "x");
            EXPECT_EQ(LhsString(sys.equations[0]), "x'");
        }

        TEST(ReduceOrder, SecondOrderSimple) {
            auto sys = ReduceText("x'' = -x\nx(0) = 1\nx'(0) = 0\n");
            ASSERT_EQ(sys.equations.size(), 2u);
            ASSERT_EQ(sys.functions.size(), 2u);
            EXPECT_EQ(sys.functions[0], "x");
            EXPECT_EQ(sys.functions[1], "x_1");

            EXPECT_EQ(LhsString(sys.equations[0]), "x'");
            EXPECT_EQ(RhsString(sys.equations[0]), "x_1");
            EXPECT_EQ(LhsString(sys.equations[1]), "x_1'");
            EXPECT_NE(RhsString(sys.equations[1]).find("x"), std::string::npos);

            ASSERT_EQ(sys.initial_conditions.size(), 2u);
            EXPECT_EQ(sys.initial_conditions[0].function_name, "x");
            EXPECT_EQ(sys.initial_conditions[0].order, 0);
            EXPECT_DOUBLE_EQ(sys.initial_conditions[0].value, 1.0);
            EXPECT_EQ(sys.initial_conditions[1].function_name, "x_1");
            EXPECT_EQ(sys.initial_conditions[1].order, 0);
            EXPECT_DOUBLE_EQ(sys.initial_conditions[1].value, 0.0);
        }

        TEST(ReduceOrder, ThirdOrder) {
            auto sys = ReduceText("x''' = x\nx(0) = 0\nx'(0) = 0\nx''(0) = 0\n");
            ASSERT_EQ(sys.equations.size(), 3u);
            ASSERT_EQ(sys.functions.size(), 3u);  // x, x_1, x_2

            EXPECT_EQ(LhsString(sys.equations[0]), "x'");
            EXPECT_EQ(RhsString(sys.equations[0]), "x_1");
            EXPECT_EQ(LhsString(sys.equations[1]), "x_1'");
            EXPECT_EQ(RhsString(sys.equations[1]), "x_2");
            EXPECT_EQ(LhsString(sys.equations[2]), "x_2'");
            EXPECT_EQ(RhsString(sys.equations[2]), "x");
        }

        TEST(ReduceOrder, TwoFunctionsOnlyOneReduced) {
            auto sys = ReduceText("x'' = y\ny' = -x\n"
                "x(0) = 1\nx'(0) = 0\ny(0) = 0\n");
            std::set<std::string> fset(sys.functions.begin(), sys.functions.end());
            EXPECT_TRUE(fset.count("x"));
            EXPECT_TRUE(fset.count("y"));
            EXPECT_TRUE(fset.count("x_1"));
            EXPECT_FALSE(fset.count("y_1"));
            ASSERT_EQ(sys.equations.size(), 3u);
        }

        TEST(ReduceOrder, RhsWithDerivatives) {
            auto sys = ReduceText("x'' = x' + x\nx(0) = 1\nx'(0) = 0\n");
            ASSERT_EQ(sys.equations.size(), 2u);
            const std::string rhs = RhsString(sys.equations[1]);
            EXPECT_NE(rhs.find("x_1"), std::string::npos);
            EXPECT_NE(rhs.find("x"), std::string::npos);
        }

        // ========================================================================
        // Auxiliary map
        // ========================================================================

        TEST(ReduceOrder, AuxiliaryMap) {
            RawSystem sys = ParseSystem("x'' = -x\nx(0) = 1\nx'(0) = 0\n");
            NormalizeSystem(sys);
            auto aux = ReduceOrder(sys);
            ASSERT_EQ(aux.size(), 1u);
            ASSERT_TRUE(aux.count("x_1"));
            const Expr& def = *aux.at("x_1");
            ASSERT_TRUE(std::holds_alternative<Derivative>(def.value));
            const auto& d = std::get<Derivative>(def.value);
            EXPECT_EQ(d.function_name, "x");
            EXPECT_EQ(d.order, 1);
        }

        TEST(ReduceOrder, AuxiliaryMapThirdOrder) {
            RawSystem sys = ParseSystem(
                "x''' = x\nx(0) = 0\nx'(0) = 0\nx''(0) = 0\n");
            NormalizeSystem(sys);
            auto aux = ReduceOrder(sys);
            ASSERT_EQ(aux.size(), 2u);
            ASSERT_TRUE(aux.count("x_1"));
            ASSERT_TRUE(aux.count("x_2"));
            EXPECT_EQ(std::get<Derivative>(aux.at("x_1")->value).order, 1);
            EXPECT_EQ(std::get<Derivative>(aux.at("x_2")->value).order, 2);
        }

        TEST(ReduceOrder, AuxiliaryMapEmptyForFirstOrder) {
            RawSystem sys = ParseSystem("x' = -x\nx(0) = 1\n");
            NormalizeSystem(sys);
            auto aux = ReduceOrder(sys);
            EXPECT_TRUE(aux.empty());
        }

        // ========================================================================
        // Конфликты имён
        // ========================================================================

        TEST(ReduceOrder, NameCollisionUsesPrefix) {
            // x_1 уже существует как функция → используется _x_1.
            RawSystem sys = ParseSystem(
                "x'' = x_1\n"
                "x_1' = 0\n"
                "x(0) = 1\n"
                "x'(0) = 0\n"
                "x_1(0) = 0\n");
            NormalizeSystem(sys);
            auto aux = ReduceOrder(sys);
            EXPECT_TRUE(aux.count("_x_1"));
            EXPECT_FALSE(aux.count("x_1"));
            std::set<std::string> fset(sys.functions.begin(), sys.functions.end());
            EXPECT_TRUE(fset.count("_x_1"));
        }

        TEST(ReduceOrder, NameCollisionDoubleThrows) {
            RawSystem sys = ParseSystem(
                "x'' = x_1 + _x_1\n"
                "x_1' = 0\n"
                "_x_1' = 0\n"
                "x(0) = 1\n"
                "x'(0) = 0\n"
                "x_1(0) = 0\n"
                "_x_1(0) = 0\n");
            NormalizeSystem(sys);
            EXPECT_THROW(ReduceOrder(sys), OrderReducerError);
        }

        TEST(ReduceOrder, NonDerivativeLhsThrows) {
            RawSystem sys = ParseSystem("x' = -x\nx(0) = 1\n");
            // Искусственно ломаем lhs — нормализатор такого не породит,
            // но проверка в ReduceOrder должна сработать.
            sys.equations[0].lhs = MakeFunction("x");
            EXPECT_THROW(ReduceOrder(sys), OrderReducerError);
        }

        // ========================================================================
        // Постусловия
        // ========================================================================

        TEST(ReduceOrder, PostConditionsLhsAllOrderOne) {
            auto sys = ReduceText("x''' = x\nx(0) = 0\nx'(0) = 0\nx''(0) = 0\n");
            for (const auto& eq : sys.equations) {
                ASSERT_NE(eq.lhs, nullptr);
                ASSERT_TRUE(std::holds_alternative<Derivative>(eq.lhs->value));
                EXPECT_EQ(LhsOrder(eq), 1);
            }
        }

        TEST(ReduceOrder, PostConditionsOneEquationPerFunction) {
            auto sys = ReduceText("x'' = y\ny' = -x\n"
                "x(0) = 1\nx'(0) = 0\ny(0) = 0\n");
            std::map<std::string, int> count;
            for (const auto& eq : sys.equations) {
                count[LhsFunc(eq)]++;
            }
            for (const auto& f : sys.functions) {
                EXPECT_EQ(count[f], 1) << "function: " << f;
            }
            EXPECT_EQ(count.size(), sys.functions.size());
        }

        TEST(ReduceOrder, PostConditionsAllICsOrderZero) {
            auto sys = ReduceText("x''' = x\nx(0) = 0\nx'(0) = 0\nx''(0) = 0\n");
            for (const auto& ic : sys.initial_conditions) {
                EXPECT_EQ(ic.order, 0) << "function: " << ic.function_name;
            }
        }

        // ========================================================================
        // Property-based
        // ========================================================================

        TEST(PropertyOrderReducer, P1_Idempotent) {
            RawSystem sys = ParseSystem(
                "x''' = -x\nx(0) = 1\nx'(0) = 0\nx''(0) = 0\n");
            NormalizeSystem(sys);
            ReduceOrder(sys);
            const std::string once = ToString(sys);

            ReduceOrder(sys);  // повторно
            const std::string twice = ToString(sys);

            EXPECT_EQ(once, twice);
        }

        TEST(PropertyOrderReducer, P2_OriginalFunctionsPreserved) {
            RawSystem sys = ParseSystem(
                "x'' = y\ny' = -x\n"
                "x(0) = 1\nx'(0) = 0\ny(0) = 0\n");
            NormalizeSystem(sys);
            std::vector<std::string> original = sys.functions;
            ReduceOrder(sys);
            std::set<std::string> after(sys.functions.begin(), sys.functions.end());
            for (const auto& f : original) {
                EXPECT_TRUE(after.count(f)) << "lost function: " << f;
            }
        }

        TEST(PropertyOrderReducer, P3_EquationCountFormula) {
            const char* kCases[] = {
                 "x' = -x\nx(0) = 1\n",
                 "x'' = -x\nx(0) = 1\nx'(0) = 0\n",
                 "x''' = x\nx(0) = 0\nx'(0) = 0\nx''(0) = 0\n",
                 "x'' = y\ny' = -x\nx(0) = 1\nx'(0) = 0\ny(0) = 0\n",
                 // Две функции, у каждой своя старшая производная в своём
                 // уравнении. Прошлый кейс "x'' = y''\ny'' = -x\n" был
                 // невалиден: NormalizeSystem отвергает уравнение с двумя
                 // старшими производными (см. Normalize.TwoHighestDerivatives...).
                 "x'' = -x\ny'' = -y\n"
                 "x(0) = 1\nx'(0) = 0\ny(0) = 0\ny'(0) = 0\n",
            };
            for (const char* text : kCases) {
                SCOPED_TRACE(std::string("input: ") + text);
                RawSystem sys = ParseSystem(text);
                NormalizeSystem(sys);
                const std::size_t before = sys.equations.size();
                auto orders = DerivativeOrders(sys);
                int delta = 0;
                for (const auto& kv : orders) {
                    if (kv.second > 1) delta += (kv.second - 1);
                }
                ReduceOrder(sys);
                EXPECT_EQ(sys.equations.size(),
                    before + static_cast<std::size_t>(delta));
            }
        }

        TEST(PropertyOrderReducer, P4_Determinism) {
            const char* kText = "x''' = -x\nx(0) = 1\nx'(0) = 0\nx''(0) = 0\n";

            RawSystem a = ParseSystem(kText);
            NormalizeSystem(a);
            ReduceOrder(a);

            RawSystem b = ParseSystem(kText);
            NormalizeSystem(b);
            ReduceOrder(b);

            EXPECT_EQ(ToString(a), ToString(b));
        }

    } // namespace
} // namespace diffuri