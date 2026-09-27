// ============================================================================
// tests/unit/test_simplify.cpp
//
// Тесты модуля simplify.
//
// Покрывает:
//   - Simplify: свёртка чисел, нейтральные элементы, Unary::Neg,
//     приведение подобных, вложенные случаи.
//   - Compare / ExprEquals.
//   - ExtractCoefficient.
// ============================================================================
#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "input/expression.h"
#include "input/parser.h"
#include "simplify/simplify.h"

namespace diffuri {
    namespace {

        // Хелпер: распарсить строку и упростить.
        ExprPtr SimplifyExpr(const std::string& text) {
            return Simplify(ParseExpression(text));
        }

        // Хелпер: получить текст упрощённого выражения.
        std::string SimplifyToString(const std::string& text) {
            return ToString(*SimplifyExpr(text));
        }

        // ====================================================================
        // Simplify: базовые случаи (листья)
        // ====================================================================

        TEST(Simplify, LeafNumberUnchanged) {
            auto e = Simplify(MakeNumber(42.0));
            ASSERT_NE(e, nullptr);
            EXPECT_TRUE(IsNumber(*e));
            EXPECT_DOUBLE_EQ(AsNumber(*e), 42.0);
        }

        TEST(Simplify, LeafFunctionUnchanged) {
            auto e = Simplify(MakeFunction("x"));
            EXPECT_EQ(ToString(*e), "x");
        }

        TEST(Simplify, LeafDerivativeUnchanged) {
            auto e = Simplify(MakeDerivative("y", 2));
            EXPECT_EQ(ToString(*e), "y''");
        }

        TEST(Simplify, LeafConstantUnchanged) {
            auto e = Simplify(MakeConstant("pi", 3.14159));
            EXPECT_EQ(ToString(*e), "pi");
        }

        // ====================================================================
        // Simplify: свёртка чисел
        // ====================================================================

        TEST(Simplify, FoldAddNumbers) {
            EXPECT_EQ(SimplifyToString("2 + 3"), "5");
        }

        TEST(Simplify, FoldSubNumbers) {
            EXPECT_EQ(SimplifyToString("10 - 4"), "6");
        }

        TEST(Simplify, FoldMulNumbers) {
            EXPECT_EQ(SimplifyToString("3 * 4"), "12");
        }

        TEST(Simplify, FoldDivNumbers) {
            EXPECT_EQ(SimplifyToString("10 / 2"), "5");
        }

        TEST(Simplify, FoldPowNumbers) {
            EXPECT_EQ(SimplifyToString("2 ^ 3"), "8");
        }

        TEST(Simplify, FoldNestedNumbers) {
            EXPECT_EQ(SimplifyToString("2 + 3 * 4"), "14");
        }

        // ====================================================================
        // Simplify: нейтральные элементы
        // ====================================================================

        TEST(Simplify, AddZeroLeft) {
            EXPECT_EQ(SimplifyToString("0 + x"), "x");
        }

        TEST(Simplify, AddZeroRight) {
            EXPECT_EQ(SimplifyToString("x + 0"), "x");
        }

        TEST(Simplify, SubZero) {
            EXPECT_EQ(SimplifyToString("x - 0"), "x");
        }

        TEST(Simplify, MulOneLeft) {
            EXPECT_EQ(SimplifyToString("1 * x"), "x");
        }

        TEST(Simplify, MulOneRight) {
            EXPECT_EQ(SimplifyToString("x * 1"), "x");
        }

        TEST(Simplify, MulZeroLeft) {
            EXPECT_EQ(SimplifyToString("0 * x"), "0");
        }

        TEST(Simplify, MulZeroRight) {
            EXPECT_EQ(SimplifyToString("x * 0"), "0");
        }

        TEST(Simplify, DivOne) {
            EXPECT_EQ(SimplifyToString("x / 1"), "x");
        }

        TEST(Simplify, PowZero) {
            EXPECT_EQ(SimplifyToString("x ^ 0"), "1");
        }

        TEST(Simplify, PowOne) {
            EXPECT_EQ(SimplifyToString("x ^ 1"), "x");
        }

        TEST(Simplify, ZeroPowAnything) {
            EXPECT_EQ(SimplifyToString("0 ^ x"), "0");
        }

        TEST(Simplify, OnePowAnything) {
            EXPECT_EQ(SimplifyToString("1 ^ x"), "1");
        }

        TEST(Simplify, ZeroDivNonZero) {
            EXPECT_EQ(SimplifyToString("0 / x"), "0");
        }

        // ====================================================================
        // Simplify: Unary::Neg разворачивается в Mul(-1, x)
        // ====================================================================

        TEST(Simplify, UnaryNegBecomesMulMinusOne) {
            auto e = Simplify(MakeUnary(Unary::Op::Neg, MakeFunction("x")));
            // После упрощения Unary в дереве нет — остался Mul(-1, x).
            auto* bin = std::get_if<Binary>(&e->value);
            ASSERT_NE(bin, nullptr);
            EXPECT_EQ(bin->op, Binary::Op::Mul);
            EXPECT_TRUE(IsNumber(*bin->lhs));
            EXPECT_DOUBLE_EQ(AsNumber(*bin->lhs), -1.0);
            EXPECT_EQ(ToString(*bin->rhs), "x");
        }

        TEST(Simplify, UnaryNegOfNumber) {
            EXPECT_EQ(SimplifyToString("-5"), "-5");
        }

        TEST(Simplify, DoubleUnaryNeg) {
            // --x = -1 * -1 * x = x
            EXPECT_EQ(SimplifyToString("--x"), "x");
        }

        TEST(Simplify, UnaryNegInSum) {
            // x + (-x) = 0
            EXPECT_EQ(SimplifyToString("x + -x"), "0");
        }

        // ====================================================================
        // Simplify: приведение подобных
        // ====================================================================

        TEST(Simplify, CombineLikeTermsXPlusX) {
            EXPECT_EQ(SimplifyToString("x + x"), "(2 * x)");
        }

        TEST(Simplify, CombineLikeTermsCoeffs) {
            EXPECT_EQ(SimplifyToString("2 * x + 3 * x"), "(5 * x)");
        }

        TEST(Simplify, CombineLikeTermsCancellation) {
            EXPECT_EQ(SimplifyToString("3 * x - 3 * x"), "0");
        }

        TEST(Simplify, CombineLikeTermsMixed) {
            // 2*x + y + 3*x - y = 5*x
            EXPECT_EQ(SimplifyToString("2 * x + y + 3 * x - y"), "(5 * x)");
        }

        TEST(Simplify, CommutativeMulCombine) {
            // x*y + y*x = 2*x*y
            auto s = SimplifyToString("x * y + y * x");
            // Оба множителя должны быть упорядочены, поэтому x*y и y*x после
            // упрощения дают одно и то же дерево.
            EXPECT_EQ(s, "(2 * (x * y))");
        }

        TEST(Simplify, AddConstantsCollapse) {
            // x + 1 + 2 + y = x + y + 3
            auto s = SimplifyToString("x + 1 + 2 + y");
            // Сборка левоассоциативная: ((3 + x) + y)
            EXPECT_EQ(s, "((3 + x) + y)");
        }

        // ====================================================================
        // Simplify: Mul canonical form
        // ====================================================================

        TEST(Simplify, MulCollapsesNumbers) {
            // 2 * 3 * x = 6 * x
            EXPECT_EQ(SimplifyToString("2 * 3 * x"), "(6 * x)");
        }

        TEST(Simplify, MulSortsFactors) {
            // y * x -> x * y (после сортировки)
            EXPECT_EQ(SimplifyToString("y * x"), "(x * y)");
        }

        // ====================================================================
        // Simplify: Sub becomes Add with negative
        // ====================================================================

        TEST(Simplify, SubBecomesAddNegative) {
            // x - y = x + (-1)*y
            // Function (x) < Binary (Mul) по Compare, поэтому x первый.
            auto s = SimplifyToString("x - y");
            EXPECT_EQ(s, "(x + (-1 * y))");
        }

        TEST(Simplify, SubOfSameIsZero) {
            EXPECT_EQ(SimplifyToString("x - x"), "0");
        }

        // ====================================================================
        // Simplify: вызовы функций
        // ====================================================================

        TEST(Simplify, CallArgsSimplified) {
            // sin(2 + 3) -> sin(5)
            EXPECT_EQ(SimplifyToString("sin(2 + 3)"), "sin(5)");
        }

        TEST(Simplify, CallUnchanged) {
            EXPECT_EQ(SimplifyToString("sin(x)"), "sin(x)");
        }

        // ====================================================================
        // Simplify: комплексные случаи
        // ====================================================================

        TEST(Simplify, ComplexLinear) {
            // 2*x + 3 - x + 1 = x + 4
            auto s = SimplifyToString("2 * x + 3 - x + 1");
            EXPECT_EQ(s, "(4 + x)");
        }

        TEST(Simplify, DerivativesCombine) {
            // x' + x' = 2 * x'
            EXPECT_EQ(SimplifyToString("x' + x'"), "(2 * x')");
        }

        TEST(Simplify, NullptrInput) {
            auto r = Simplify(nullptr);
            EXPECT_EQ(r, nullptr);
        }

        // ====================================================================
        // Compare
        // ====================================================================

        TEST(Compare, NumbersByValue) {
            auto a = MakeNumber(1.0);
            auto b = MakeNumber(2.0);
            EXPECT_LT(Compare(*a, *b), 0);
            EXPECT_GT(Compare(*b, *a), 0);
            EXPECT_EQ(Compare(*a, *a), 0);
        }

        TEST(Compare, FunctionsByName) {
            auto x = MakeFunction("x");
            auto y = MakeFunction("y");
            EXPECT_LT(Compare(*x, *y), 0);
            EXPECT_GT(Compare(*y, *x), 0);
        }

        TEST(Compare, NumberLessThanFunction) {
            auto n = MakeNumber(100.0);
            auto x = MakeFunction("x");
            EXPECT_LT(Compare(*n, *x), 0);
        }

        TEST(Compare, DerivativesByFunctionThenOrder) {
            auto x1 = MakeDerivative("x", 1);
            auto x2 = MakeDerivative("x", 2);
            auto y1 = MakeDerivative("y", 1);
            EXPECT_LT(Compare(*x1, *x2), 0);
            EXPECT_LT(Compare(*x1, *y1), 0);
        }

        TEST(Compare, BinaryByOpThenChildren) {
            // x+y vs x+z
            auto a = MakeBinary(Binary::Op::Add,
                MakeFunction("x"), MakeFunction("y"));
            auto b = MakeBinary(Binary::Op::Add,
                MakeFunction("x"), MakeFunction("z"));
            EXPECT_LT(Compare(*a, *b), 0);
        }

        TEST(Compare, CallByArity) {
            auto c1 = MakeCallArgs("f", MakeFunction("x"));
            auto c2 = MakeCallArgs("f", MakeFunction("x"), MakeFunction("y"));
            EXPECT_LT(Compare(*c1, *c2), 0);
        }

        // ====================================================================
        // ExprEquals
        // ====================================================================

        TEST(ExprEquals, StructurallyIdentical) {
            auto a = MakeBinary(Binary::Op::Add,
                MakeFunction("x"), MakeNumber(1.0));
            auto b = MakeBinary(Binary::Op::Add,
                MakeFunction("x"), MakeNumber(1.0));
            EXPECT_TRUE(ExprEquals(*a, *b));
        }

        TEST(ExprEquals, Different) {
            auto a = MakeFunction("x");
            auto b = MakeFunction("y");
            EXPECT_FALSE(ExprEquals(*a, *b));
        }

        TEST(ExprEquals, SimplifiedFormsMatch) {
            // После упрощения x*y и y*x должны быть равны.
            auto a = SimplifyExpr("x * y");
            auto b = SimplifyExpr("y * x");
            EXPECT_TRUE(ExprEquals(*a, *b));
        }

        // ====================================================================
        // ExtractCoefficient
        // ====================================================================

        TEST(ExtractCoefficient, PureNumber) {
            auto e = MakeNumber(42.0);
            auto dc = ExtractCoefficient(std::move(e));
            EXPECT_DOUBLE_EQ(dc.coefficient, 42.0);
            EXPECT_TRUE(IsNumber(*dc.base));
            EXPECT_DOUBLE_EQ(AsNumber(*dc.base), 1.0);
        }

        TEST(ExtractCoefficient, CoeffTimesBase) {
            auto e = MakeBinary(Binary::Op::Mul,
                MakeNumber(3.5), MakeFunction("x"));
            auto dc = ExtractCoefficient(std::move(e));
            EXPECT_DOUBLE_EQ(dc.coefficient, 3.5);
            EXPECT_EQ(ToString(*dc.base), "x");
        }

        TEST(ExtractCoefficient, CoeffOnRight) {
            // x * 5 — тоже должно распознаваться.
            auto e = MakeBinary(Binary::Op::Mul,
                MakeFunction("x"), MakeNumber(5.0));
            auto dc = ExtractCoefficient(std::move(e));
            EXPECT_DOUBLE_EQ(dc.coefficient, 5.0);
            EXPECT_EQ(ToString(*dc.base), "x");
        }

        TEST(ExtractCoefficient, NoCoefficient) {
            auto e = MakeFunction("x");
            auto dc = ExtractCoefficient(std::move(e));
            EXPECT_DOUBLE_EQ(dc.coefficient, 1.0);
            EXPECT_EQ(ToString(*dc.base), "x");
        }

        // ====================================================================
        // IsNumber / AsNumber
        // ====================================================================

        TEST(IsNumber, TrueForNumber) {
            auto e = MakeNumber(1.0);
            EXPECT_TRUE(IsNumber(*e));
        }

        TEST(IsNumber, FalseForFunction) {
            auto e = MakeFunction("x");
            EXPECT_FALSE(IsNumber(*e));
        }

        TEST(AsNumber, ReturnsValue) {
            auto e = MakeNumber(2.5);
            EXPECT_DOUBLE_EQ(AsNumber(*e), 2.5);
        }

        TEST(AsNumber, ThrowsForNonNumber) {
            auto e = MakeFunction("x");
            EXPECT_THROW(AsNumber(*e), std::runtime_error);
        }

    } // namespace
} // namespace diffuri