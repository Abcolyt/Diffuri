// ============================================================================
// tests/unit/test_library.cpp
//
// Тесты FunctionLibrary: наличие пяти расширений этапа, корректность
// Lookup и Contains, содержимое уравнений расширения.
// ============================================================================
#include <gtest/gtest.h>

#include <string>
#include <utility>

#include <algorithm>
#include "core/expression.h"
#include "input/parser.h"
#include "polynomization/library.h"
#include "polynomization/polynomization.h"
#include "simplify/simplify.h"

namespace diffuri {
    namespace {

        // Упростить и вернуть строку для сравнения.
        std::string SimplifyToString(const Expr& e) {
            ExprPtr copy = Clone(e);
            copy = Simplify(std::move(copy));
            return ToString(*copy);
        }

        // Проверить, что equation для f в exp структурно равен ожидаемому
        // выражению (после Simplify с обеих сторон).
        void ExpectEquationEquals(const Expansion& exp,
            const std::string& f,
            const std::string& expected_text) {
            auto it = exp.equations.find(f);
            ASSERT_NE(it, exp.equations.end()) << "no equation for " << f;

            ExprPtr expected = ParseExpression(expected_text);
            expected = Simplify(std::move(expected));

            ExprPtr actual = Clone(*it->second);
            actual = Simplify(std::move(actual));

            EXPECT_TRUE(ExprEquals(*actual, *expected))
                << "actual: " << ToString(*actual)
                << ", expected: " << ToString(*expected);
        }

        // ========================================================================
        // Contains
        // ========================================================================

        TEST(FunctionLibrary, ContainsPrimaryNames) {
            FunctionLibrary lib;
            EXPECT_TRUE(lib.Contains("inv"));
            EXPECT_TRUE(lib.Contains("ln"));
            EXPECT_TRUE(lib.Contains("exp"));
            EXPECT_TRUE(lib.Contains("sin"));
            EXPECT_TRUE(lib.Contains("cos"));
            EXPECT_TRUE(lib.Contains("sh"));
            EXPECT_TRUE(lib.Contains("ch"));
        }

        TEST(FunctionLibrary, ContainsPartnersOfExpansion) {
            FunctionLibrary lib;
            // inv входит в расширение ln; cos — в sin; ch — в sh. И наоборот.
            EXPECT_TRUE(lib.Contains("cos"));   // sin/cos
            EXPECT_TRUE(lib.Contains("inv"));   // ln/inv
            EXPECT_TRUE(lib.Contains("ch"));    // sh/ch
        }

        TEST(FunctionLibrary, DoesNotContainUnknown) {
            FunctionLibrary lib;
            EXPECT_FALSE(lib.Contains(""));
            EXPECT_FALSE(lib.Contains("atan2"));   // многоаргументные — не этап
            EXPECT_FALSE(lib.Contains("Dv"));      // Weber — не этап
            EXPECT_FALSE(lib.Contains("EK"));      // Kepler — не этап
            EXPECT_FALSE(lib.Contains("nonexistent"));
        }

        // ========================================================================
        // Lookup: пустые результаты
        // ========================================================================

        TEST(FunctionLibrary, LookupUnknownReturnsEmpty) {
            FunctionLibrary lib;
            EXPECT_FALSE(lib.Lookup("").has_value());
            EXPECT_FALSE(lib.Lookup("nonexistent").has_value());
            EXPECT_FALSE(lib.Lookup("atan2").has_value());
        }

        // ========================================================================
        // Lookup: inv
        // ========================================================================

        TEST(FunctionLibrary, LookupInv) {
            FunctionLibrary lib;
            auto exp = lib.Lookup("inv");
            ASSERT_TRUE(exp.has_value());
            ASSERT_EQ(exp->functions.size(), 1u);
            EXPECT_EQ(exp->functions[0], "inv");
            ASSERT_EQ(exp->equations.size(), 1u);
            ExpectEquationEquals(*exp, "inv", "-inv^2");
        }

        // ========================================================================
        // Lookup: ln
        // ========================================================================

        TEST(FunctionLibrary, LookupLn) {
            FunctionLibrary lib;
            auto exp = lib.Lookup("ln");
            ASSERT_TRUE(exp.has_value());
            ASSERT_EQ(exp->functions.size(), 2u);
            EXPECT_EQ(exp->functions[0], "ln");
            EXPECT_EQ(exp->functions[1], "inv");
            ASSERT_EQ(exp->equations.size(), 2u);
            ExpectEquationEquals(*exp, "ln", "inv");
            ExpectEquationEquals(*exp, "inv", "-inv^2");
        }

        TEST(FunctionLibrary, LookupInvFromLn) {
            FunctionLibrary lib;
            auto a = lib.Lookup("ln");
            auto b = lib.Lookup("inv");
            ASSERT_TRUE(a.has_value());
            ASSERT_TRUE(b.has_value());

            // ln-расширение содержит inv как вторую функцию.
            EXPECT_NE(std::find(a->functions.begin(), a->functions.end(), "inv"),
                a->functions.end());

            // inv-расширение — только {inv}.
            ASSERT_EQ(b->functions.size(), 1u);
            EXPECT_EQ(b->functions[0], "inv");
        }

        // ========================================================================
        // Lookup: exp
        // ========================================================================

        TEST(FunctionLibrary, LookupExp) {
            FunctionLibrary lib;
            auto exp = lib.Lookup("exp");
            ASSERT_TRUE(exp.has_value());
            ASSERT_EQ(exp->functions.size(), 1u);
            EXPECT_EQ(exp->functions[0], "exp");
            ASSERT_EQ(exp->equations.size(), 1u);
            ExpectEquationEquals(*exp, "exp", "exp");
        }

        // ========================================================================
        // Lookup: sin / cos
        // ========================================================================

        TEST(FunctionLibrary, LookupSin) {
            FunctionLibrary lib;
            auto exp = lib.Lookup("sin");
            ASSERT_TRUE(exp.has_value());
            ASSERT_EQ(exp->functions.size(), 2u);
            EXPECT_EQ(exp->functions[0], "sin");
            EXPECT_EQ(exp->functions[1], "cos");
            ASSERT_EQ(exp->equations.size(), 2u);
            ExpectEquationEquals(*exp, "sin", "cos");
            ExpectEquationEquals(*exp, "cos", "-sin");
        }

        TEST(FunctionLibrary, LookupCosSharesExpansionWithSin) {
            FunctionLibrary lib;
            auto s = lib.Lookup("sin");
            auto c = lib.Lookup("cos");
            ASSERT_TRUE(s.has_value());
            ASSERT_TRUE(c.has_value());
            EXPECT_EQ(s->functions, c->functions);
        }

        // ========================================================================
        // Lookup: sh / ch
        // ========================================================================

        TEST(FunctionLibrary, LookupSh) {
            FunctionLibrary lib;
            auto exp = lib.Lookup("sh");
            ASSERT_TRUE(exp.has_value());
            ASSERT_EQ(exp->functions.size(), 2u);
            EXPECT_EQ(exp->functions[0], "sh");
            EXPECT_EQ(exp->functions[1], "ch");
            ASSERT_EQ(exp->equations.size(), 2u);
            ExpectEquationEquals(*exp, "sh", "ch");
            ExpectEquationEquals(*exp, "ch", "sh");
        }

        TEST(FunctionLibrary, LookupChSharesExpansionWithSh) {
            FunctionLibrary lib;
            auto a = lib.Lookup("sh");
            auto b = lib.Lookup("ch");
            ASSERT_TRUE(a.has_value());
            ASSERT_TRUE(b.has_value());
            EXPECT_EQ(a->functions, b->functions);
        }

        // ========================================================================
        // Идемпотентность Lookup: повторные вызовы дают одинаковый результат
        // ========================================================================

        TEST(FunctionLibrary, RepeatedLookupIsConsistent) {
            FunctionLibrary lib;
            auto a = lib.Lookup("sin");
            auto b = lib.Lookup("sin");
            ASSERT_TRUE(a.has_value());
            ASSERT_TRUE(b.has_value());
            EXPECT_EQ(a->functions, b->functions);
            ASSERT_EQ(a->equations.size(), b->equations.size());
            for (const auto& kv : a->equations) {
                ASSERT_TRUE(b->equations.count(kv.first));
                EXPECT_TRUE(ExprEquals(*kv.second, *b->equations.at(kv.first)));
            }
        }

    } // namespace
} // namespace diffuri