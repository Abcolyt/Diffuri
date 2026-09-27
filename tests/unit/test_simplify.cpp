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

        // ====================================================================
// НОВЫЕ ТЕСТЫ — Compare как строгий порядок
//
// std::sort в CombineAdd/CombineMul полагается на то, что Compare —
// полный лексикографический порядок. Компаратор, нарушающий
// рефлексивность / антисимметричность / транзитивность, даёт UB
// (в debug-STL — падение с рандомным сообщением).
// ====================================================================

        std::vector<ExprPtr> CompareSamples() {
            std::vector<ExprPtr> v;
            v.push_back(MakeNumber(-1.0));
            v.push_back(MakeNumber(0.0));
            v.push_back(MakeNumber(1.0));
            v.push_back(MakeFunction("x"));
            v.push_back(MakeFunction("y"));
            v.push_back(MakeConstant("pi", 3.14));
            v.push_back(MakeDerivative("x", 1));
            v.push_back(MakeDerivative("x", 2));
            v.push_back(MakeDerivative("y", 1));
            v.push_back(MakeBinary(Binary::Op::Add,
                MakeFunction("x"), MakeFunction("y")));
            v.push_back(MakeBinary(Binary::Op::Mul,
                MakeNumber(2.0), MakeFunction("x")));
            v.push_back(MakeBinary(Binary::Op::Pow,
                MakeFunction("x"), MakeNumber(2.0)));
            v.push_back(MakeCallArgs("sin", MakeFunction("x")));
            v.push_back(MakeCallArgs("cos", MakeFunction("x")));
            return v;
        }

        TEST(CompareOrder, Reflexive) {
            for (auto& e : CompareSamples()) {
                EXPECT_EQ(Compare(*e, *e), 0);
            }
        }

        TEST(CompareOrder, Antisymmetric) {
            auto s = CompareSamples();
            for (std::size_t i = 0; i < s.size(); ++i) {
                for (std::size_t j = 0; j < s.size(); ++j) {
                    int ij = Compare(*s[i], *s[j]);
                    int ji = Compare(*s[j], *s[i]);
                    EXPECT_EQ(ij, -ji) << "i=" << i << " j=" << j;
                }
            }
        }

        TEST(CompareOrder, Transitive) {
            auto s = CompareSamples();
            for (std::size_t i = 0; i < s.size(); ++i) {
                for (std::size_t j = 0; j < s.size(); ++j) {
                    for (std::size_t k = 0; k < s.size(); ++k) {
                        if (Compare(*s[i], *s[j]) < 0 &&
                            Compare(*s[j], *s[k]) < 0) {
                            EXPECT_LT(Compare(*s[i], *s[k]), 0)
                                << "i=" << i << " j=" << j << " k=" << k;
                        }
                        if (Compare(*s[i], *s[j]) == 0 &&
                            Compare(*s[j], *s[k]) == 0) {
                            EXPECT_EQ(Compare(*s[i], *s[k]), 0)
                                << "i=" << i << " j=" << j << " k=" << k;
                        }
                    }
                }
            }
        }

        TEST(CompareOrder, EqConsistentWithCompare) {
            auto s = CompareSamples();
            for (std::size_t i = 0; i < s.size(); ++i) {
                for (std::size_t j = 0; j < s.size(); ++j) {
                    EXPECT_EQ(ExprEquals(*s[i], *s[j]),
                        Compare(*s[i], *s[j]) == 0)
                        << "i=" << i << " j=" << j;
                }
            }
        }

        // ====================================================================
        // НОВЫЕ ТЕСТЫ — ToString round-trip для выражений
        //
        // ToString — единственный способ показать результат пользователю.
        // Round-trip ловит рассинхрон между печатью и парсингом.
        // ====================================================================

        TEST(ToStringRoundTripExpr, Basic) {
            const char* inputs[] = {
                "x + y",
                "x * y + z",
                "sin(x) + cos(y)",
                "x'' + 3 * x'",
                "(a + b) * c",
                "2 ^ x",
                "-x",
            };
            for (const char* s : inputs) {
                auto e1 = ParseExpression(s);
                std::string printed = ToString(*e1);
                auto e2 = ParseExpression(printed);
                EXPECT_TRUE(ExprEquals(*e1, *e2))
                    << "input: " << s << "\nprinted: " << printed;
            }
        }

        TEST(ToStringRoundTripExpr, SimplifiedIsStable) {
            const char* inputs[] = {
                "x + x + x",
                "2 * x * 3",
                "x * y + y * x",
                "1 + x + 2",
                "3 * x - 3 * x + 1",
            };
            for (const char* s : inputs) {
                auto e = Simplify(ParseExpression(s));
                std::string printed = ToString(*e);
                auto e2 = ParseExpression(printed);
                EXPECT_TRUE(ExprEquals(*e, *e2))
                    << "input: " << s << "\nprinted: " << printed;
            }
        }

        // ====================================================================
        // НОВЫЕ ТЕСТЫ — Parser: Unary::Neg
        //
        // Регрессия: раньше парсер строил Binary{Sub, 0, x} для "-x".
        // Это ломало HasSpecificDerivative в input.cpp (не находил
        // Derivative внутри Unary). Тесты фиксируют контракт парсера:
        // унарный минус — это Unary{Neg, x}, не Binary(Sub, 0, x).
        // ====================================================================

        TEST(ParserUnary, SimpleNeg) {
            auto e = ParseExpression("-x");
            ASSERT_TRUE(std::holds_alternative<Unary>(e->value));
            auto& u = std::get<Unary>(e->value);
            EXPECT_EQ(u.op, Unary::Op::Neg);
            ASSERT_TRUE(std::holds_alternative<Function>(u.operand->value));
            EXPECT_EQ(std::get<Function>(u.operand->value).name, "x");
        }

        TEST(ParserUnary, NegBindsLooserThanPower) {
            // -x^2 должно читаться как -(x^2), а не (-x)^2
            auto e = ParseExpression("-x^2");
            ASSERT_TRUE(std::holds_alternative<Unary>(e->value));
            auto& u = std::get<Unary>(e->value);
            ASSERT_TRUE(std::holds_alternative<Binary>(u.operand->value));
            EXPECT_EQ(std::get<Binary>(u.operand->value).op, Binary::Op::Pow);
        }

        TEST(ParserUnary, DoubleNeg) {
            auto e = ParseExpression("--x");
            ASSERT_TRUE(std::holds_alternative<Unary>(e->value));
            auto& outer = std::get<Unary>(e->value);
            ASSERT_TRUE(std::holds_alternative<Unary>(outer.operand->value));
            EXPECT_EQ(std::get<Unary>(outer.operand->value).op, Unary::Op::Neg);
        }

        TEST(ParserUnary, PlusIsNoop) {
            auto e = ParseExpression("+x");
            EXPECT_TRUE(std::holds_alternative<Function>(e->value));
        }

        TEST(ParserUnary, NegOfDerivative) {
            auto e = ParseExpression("-x'");
            ASSERT_TRUE(std::holds_alternative<Unary>(e->value));
            auto& u = std::get<Unary>(e->value);
            ASSERT_TRUE(std::holds_alternative<Derivative>(u.operand->value));
            EXPECT_EQ(std::get<Derivative>(u.operand->value).function_name, "x");
            EXPECT_EQ(std::get<Derivative>(u.operand->value).order, 1);
        }

        TEST(ParserUnary, NegOfSecondDerivative) {
            auto e = ParseExpression("-x''");
            ASSERT_TRUE(std::holds_alternative<Unary>(e->value));
            auto& u = std::get<Unary>(e->value);
            ASSERT_TRUE(std::holds_alternative<Derivative>(u.operand->value));
            EXPECT_EQ(std::get<Derivative>(u.operand->value).order, 2);
        }

    } // namespace
} // namespace diffuri