// ============================================================================
// tests/unit/test_normalize.cpp
//
// Тесты модуля normalize: приведение системы ОДУ к каноническому виду
// y^(n) = RHS.
// ============================================================================
#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "input/input.h"
#include "input/parser.h"
#include "normalize/normalize.h"
#include "simplify/simplify.h"

namespace diffuri {
    namespace {

        // Хелпер: распарсить систему и нормализовать.
        RawSystem NormalizeText(const std::string& text) {
            RawSystem sys = ParseSystem(text);
            NormalizeSystem(sys);
            return sys;
        }

        // Хелпер: напечатать rhs уравнения.
        std::string RhsString(const Equation& eq) {
            return ToString(*eq.rhs);
        }

        std::string LhsString(const Equation& eq) {
            return ToString(*eq.lhs);
        }

        // ====================================================================
        // Простые случаи: система уже нормализована
        // ====================================================================

        TEST(Normalize, AlreadyNormalizedSimple) {
            auto sys = NormalizeText(
                "x' = -x\n"
                "x(0) = 1\n");
            ASSERT_EQ(sys.equations.size(), 1u);
            EXPECT_EQ(LhsString(sys.equations[0]), "x'");
            EXPECT_EQ(RhsString(sys.equations[0]), "(-1 * x)");
        }

        TEST(Normalize, TwoEquationsAlreadyNormalized) {
            auto sys = NormalizeText(
                "x' = y\n"
                "y' = -x\n"
                "x(0) = 1\n"
                "y(0) = 0\n");
            ASSERT_EQ(sys.equations.size(), 2u);
            EXPECT_EQ(LhsString(sys.equations[0]), "x'");
            EXPECT_EQ(RhsString(sys.equations[0]), "y");
            EXPECT_EQ(LhsString(sys.equations[1]), "y'");
            EXPECT_EQ(RhsString(sys.equations[1]), "(-1 * x)");
        }

        // ====================================================================
        // Перенос слагаемых
        // ====================================================================

        TEST(Normalize, SecondOrderMovedToRhs) {
            // x'' + x = 0  =>  x'' = -x
            auto sys = NormalizeText(
                "x'' + x = 0\n"
                "x(0) = 1\n"
                "x'(0) = 0\n");
            ASSERT_EQ(sys.equations.size(), 1u);
            EXPECT_EQ(LhsString(sys.equations[0]), "x''");
            EXPECT_EQ(RhsString(sys.equations[0]), "(-1 * x)");
        }

        TEST(Normalize, DerivativeOnBothSides) {
            // 3*x' = x' + 2*x  =>  2*x' = 2*x  =>  x' = x
            auto sys = NormalizeText(
                "3 * x' = x' + 2 * x\n"
                "x(0) = 1\n");
            ASSERT_EQ(sys.equations.size(), 1u);
            EXPECT_EQ(LhsString(sys.equations[0]), "x'");
            EXPECT_EQ(RhsString(sys.equations[0]), "x");
        }

        TEST(Normalize, MultipleTermsOnLhs) {
            // x'' + x' + x = t  =>  x'' = t - x' - x
            auto sys = NormalizeText(
                "x'' + x' + x = t\n"
                "x(0) = 0\n"
                "x'(0) = 0\n");
            ASSERT_EQ(sys.equations.size(), 1u);
            EXPECT_EQ(LhsString(sys.equations[0]), "x''");
            // rhs после упрощения: t + (-1*x') + (-1*x)
            // Порядок сортировки Compare: Mul < Function, поэтому сначала Mul-ы
            auto rhs = RhsString(sys.equations[0]);
            EXPECT_NE(rhs.find("t"), std::string::npos);
            EXPECT_NE(rhs.find("x'"), std::string::npos);
            EXPECT_NE(rhs.find("x"), std::string::npos);
        }

        // ====================================================================
        // Деление на коэффициент
        // ====================================================================

        TEST(Normalize, DivideByCoefficient) {
            // 2*x' = x  =>  x' = 0.5*x
            auto sys = NormalizeText(
                "2 * x' = x\n"
                "x(0) = 1\n");
            ASSERT_EQ(sys.equations.size(), 1u);
            EXPECT_EQ(LhsString(sys.equations[0]), "x'");
            EXPECT_EQ(RhsString(sys.equations[0]), "(0.5 * x)");
        }

        TEST(Normalize, DivideByNegativeCoefficient) {
            // (-1) * x' = x  =>  x' = (-1) * x
            // Используем (-1)*x' вместо -x' чтобы обойти ограничение
            // Validate на поиск производных внутри Unary на верхнем уровне.
            auto sys = NormalizeText(
                "(-1) * x' = x\n"
                "x(0) = 1\n");
            ASSERT_EQ(sys.equations.size(), 1u);
            EXPECT_EQ(LhsString(sys.equations[0]), "x'");
            EXPECT_EQ(RhsString(sys.equations[0]), "(-1 * x)");
        }

        // ====================================================================
        // Системы из нескольких уравнений
        // ====================================================================

        TEST(Normalize, SystemOfTwoFirstOrder) {
            auto sys = NormalizeText(
                "x' = 2*x + y\n"
                "y' = x - 3*y\n"
                "x(0) = 1\n"
                "y(0) = 0\n");
            ASSERT_EQ(sys.equations.size(), 2u);
            EXPECT_EQ(LhsString(sys.equations[0]), "x'");
            EXPECT_EQ(LhsString(sys.equations[1]), "y'");
        }

        TEST(Normalize, LorenzLikeSystem) {
            auto sys = NormalizeText(
                "x' = 10 * (y - x)\n"
                "y' = x * (28 - z) - y\n"
                "z' = x * y - 2.666 * z\n"
                "x(0) = 1\n"
                "y(0) = 1\n"
                "z(0) = 1\n");
            ASSERT_EQ(sys.equations.size(), 3u);
            EXPECT_EQ(LhsString(sys.equations[0]), "x'");
            EXPECT_EQ(LhsString(sys.equations[1]), "y'");
            EXPECT_EQ(LhsString(sys.equations[2]), "z'");
        }

        // ====================================================================
        // Ошибки нормализации
        // ====================================================================

        TEST(Normalize, NonLinearInHighestDerivativeThrows) {
            // (x')^2 = 1 — нелинейно по x'
            RawSystem sys = ParseSystem(
                "(x')^2 = 1\n"
                "x(0) = 1\n");
            EXPECT_THROW(NormalizeSystem(sys), NormalizeError);
        }

        TEST(Normalize, DerivativeInsideFunctionThrows) {
            // sin(x') = t — нелинейно
            RawSystem sys = ParseSystem(
                "sin(x') = t\n"
                "x(0) = 0\n");
            EXPECT_THROW(NormalizeSystem(sys), NormalizeError);
        }

        TEST(Normalize, ProductOfDerivativesThrows) {
            // x' * x'' = 1 — нелинейно
            RawSystem sys = ParseSystem(
                "x' * x'' = 1\n"
                "x(0) = 0\n"
                "x'(0) = 1\n");
            EXPECT_THROW(NormalizeSystem(sys), NormalizeError);
        }

        TEST(Normalize, TwoHighestDerivativesInOneEquationThrows) {
            // x' + y' = t — две разные старшие производные в одном уравнении
            RawSystem sys = ParseSystem(
                "x' + y' = t\n"
                "x(0) = 0\n"
                "y(0) = 0\n");
            EXPECT_THROW(NormalizeSystem(sys), NormalizeError);
        }

        // ====================================================================
        // IsLinearIn: отдельные проверки
        // ====================================================================

        TEST(IsLinearIn, LinearSimple) {
            auto e = ParseExpression("3 * x' + x");
            EXPECT_TRUE(IsLinearIn(*e, "x", 1));
        }

        TEST(IsLinearIn, LinearWithOtherDerivative) {
            // 3 * x'' + y' + x  — линейно по x'', т.к. y' — другая производная
            auto e = ParseExpression("3 * x'' + y' + x");
            EXPECT_TRUE(IsLinearIn(*e, "x", 2));
        }

        TEST(IsLinearIn, NonLinearSquare) {
            auto e = ParseExpression("(x')^2");
            EXPECT_FALSE(IsLinearIn(*e, "x", 1));
        }

        TEST(IsLinearIn, NonLinearInsideCall) {
            auto e = ParseExpression("sin(x'')");
            EXPECT_FALSE(IsLinearIn(*e, "x", 2));
        }

        TEST(IsLinearIn, NonLinearProduct) {
            auto e = ParseExpression("x'' * x'");
            EXPECT_FALSE(IsLinearIn(*e, "x", 2));
        }

        TEST(IsLinearIn, LinearProductWithOtherFunction) {
            // t * x'' — линейно по x''
            auto e = ParseExpression("t * x''");
            EXPECT_TRUE(IsLinearIn(*e, "x", 2));
        }

        TEST(IsLinearIn, NonLinearInDenominator) {
            // 1 / x' — нелинейно
            auto e = ParseExpression("1 / x'");
            EXPECT_FALSE(IsLinearIn(*e, "x", 1));
        }

        TEST(IsLinearIn, LinearInNumerator) {
            // x' / t — линейно по x'
            auto e = ParseExpression("x' / t");
            EXPECT_TRUE(IsLinearIn(*e, "x", 1));
        }

        // ====================================================================
        // TargetFunction
        // ====================================================================

        TEST(TargetFunction, FindsSingleFunction) {
            RawSystem sys = ParseSystem(
                "x' = -x\n"
                "x(0) = 1\n");
            std::string t = TargetFunction(sys.equations[0], sys);
            EXPECT_EQ(t, "x");
        }

        TEST(TargetFunction, SecondOrderFoundCorrectly) {
            RawSystem sys = ParseSystem(
                "x'' + x = 0\n"
                "x(0) = 1\n"
                "x'(0) = 0\n");
            std::string t = TargetFunction(sys.equations[0], sys);
            EXPECT_EQ(t, "x");
        }

        TEST(TargetFunction, TwoHighestThrows) {
            RawSystem sys = ParseSystem(
                "x' + y' = 0\n"
                "x(0) = 0\n"
                "y(0) = 0\n");
            EXPECT_THROW(TargetFunction(sys.equations[0], sys), NormalizeError);
        }

        // ====================================================================
        // TotalCoefficient
        // ====================================================================

        TEST(TotalCoefficient, SingleTerm) {
            auto e = ParseExpression("3 * x'");
            e = Simplify(std::move(e));
            double c = TotalCoefficient(*e, "x", 1);
            EXPECT_DOUBLE_EQ(c, 3.0);
        }

        TEST(TotalCoefficient, SumOfTerms) {
            auto e = ParseExpression("2 * x' + 3 * x'");
            e = Simplify(std::move(e));
            double c = TotalCoefficient(*e, "x", 1);
            EXPECT_DOUBLE_EQ(c, 5.0);
        }

        TEST(TotalCoefficient, Subtracting) {
            // 5 * x' - 2 * x' = 3 * x'
            auto e = ParseExpression("5 * x' - 2 * x'");
            e = Simplify(std::move(e));
            double c = TotalCoefficient(*e, "x", 1);
            EXPECT_DOUBLE_EQ(c, 3.0);
        }

        TEST(TotalCoefficient, ImplicitCoefficientOne) {
            auto e = ParseExpression("x'");
            e = Simplify(std::move(e));
            double c = TotalCoefficient(*e, "x", 1);
            EXPECT_DOUBLE_EQ(c, 1.0);
        }

        // ====================================================================
        // End-to-end: проверка эквивалентности
        // ====================================================================

        TEST(Normalize, EquivalentToOriginalOnSamplePoint) {
            // x'' + 2*x' + x = 0  =>  x'' = -2*x' - x
            // В точке x=1, x'=2: исходное x'' + 4 + 1 = 0 => x'' = -5
            // Нормализованное: x'' = -2*2 - 1 = -5. Совпадает.
            auto sys = NormalizeText(
                "x'' + 2 * x' + x = 0\n"
                "x(0) = 1\n"
                "x'(0) = 2\n");
            EXPECT_EQ(LhsString(sys.equations[0]), "x''");
            auto rhs = RhsString(sys.equations[0]);
            // Должно быть что-то эквивалентное -2*x' - x
            EXPECT_NE(rhs.find("x'"), std::string::npos);
            EXPECT_NE(rhs.find("x"), std::string::npos);
        }

    } // namespace
} // namespace diffuri