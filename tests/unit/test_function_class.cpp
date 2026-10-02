// ============================================================================
// tests/unit/test_function_class.cpp
//
// Unit-тесты структуры Expansion (описание расширения библиотечной
// функции). Логики в самой структуре нет — проверяем, что она
// конструируется, наполняется и переносится.
// ============================================================================
#include <gtest/gtest.h>

#include <string>
#include <utility>

#include "input/expression.h"
#include "polynomization/function_class.h"

namespace diffuri {
    namespace {

        TEST(Expansion, DefaultConstructible) {
            Expansion e;
            EXPECT_TRUE(e.functions.empty());
            EXPECT_TRUE(e.equations.empty());
        }

        TEST(Expansion, PopulatedFieldsReadable) {
            Expansion e;
            e.functions = { "sin", "cos" };
            e.equations.emplace("sin", MakeFunction("cos"));
            e.equations.emplace("cos",
                MakeUnary(Unary::Op::Neg, MakeFunction("sin")));

            ASSERT_EQ(e.functions.size(), 2u);
            EXPECT_EQ(e.functions[0], "sin");
            EXPECT_EQ(e.functions[1], "cos");
            ASSERT_EQ(e.equations.size(), 2u);
            EXPECT_TRUE(e.equations.count("sin") == 1);
            EXPECT_TRUE(e.equations.count("cos") == 1);
        }

        TEST(Expansion, EquationsValuesAreNonNull) {
            Expansion e;
            e.functions = { "exp" };
            e.equations.emplace("exp", MakeFunction("exp"));
            ASSERT_EQ(e.equations.size(), 1u);
            ASSERT_NE(e.equations.at("exp"), nullptr);
        }

        TEST(Expansion, MoveConstructible) {
            Expansion a;
            a.functions = { "sin", "cos" };
            a.equations.emplace("sin", MakeFunction("cos"));
            a.equations.emplace("cos",
                MakeUnary(Unary::Op::Neg, MakeFunction("sin")));

            Expansion b = std::move(a);
            EXPECT_EQ(b.functions.size(), 2u);
            EXPECT_EQ(b.equations.size(), 2u);
        }

        TEST(Expansion, MoveAssignable) {
            Expansion a;
            a.functions = { "sin" };
            a.equations.emplace("sin", MakeFunction("cos"));

            Expansion b;
            b = std::move(a);
            EXPECT_EQ(b.functions.size(), 1u);
            EXPECT_EQ(b.equations.size(), 1u);
        }

    } // namespace
} // namespace diffuri