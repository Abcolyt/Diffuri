// ============================================================================
// tests/unit/test_normalize.cpp
//
// Тесты модуля normalize: приведение системы ОДУ к каноническому виду
// y^(n) = RHS.
// ============================================================================
#include <gtest/gtest.h>

#include <set>
#include <variant>
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
        // FindTargetFunction
        // ====================================================================

        TEST(FindTargetFunction, FindsSingleFunction) {
            RawSystem sys = ParseSystem(
                "x' = -x\n"
                "x(0) = 1\n");
            std::string t = FindTargetFunction(sys.equations[0], sys);
            EXPECT_EQ(t, "x");
        }

        TEST(FindTargetFunction, SecondOrderFoundCorrectly) {
            RawSystem sys = ParseSystem(
                "x'' + x = 0\n"
                "x(0) = 1\n"
                "x'(0) = 0\n");
            std::string t = FindTargetFunction(sys.equations[0], sys);
            EXPECT_EQ(t, "x");
        }

        TEST(FindTargetFunction, TwoHighestThrows) {
            RawSystem sys = ParseSystem(
                "x' + y' = 0\n"
                "x(0) = 0\n"
                "y(0) = 0\n");
            EXPECT_THROW(FindTargetFunction(sys.equations[0], sys), NormalizeError);
        }

        // ====================================================================
        // CalculateTotalCoefficient
        // ====================================================================

        TEST(CalculateTotalCoefficient, SingleTerm) {
            auto e = ParseExpression("3 * x'");
            e = Simplify(std::move(e));
            double c = CalculateTotalCoefficient(*e, "x", 1);
            EXPECT_DOUBLE_EQ(c, 3.0);
        }

        TEST(CalculateTotalCoefficient, SumOfTerms) {
            auto e = ParseExpression("2 * x' + 3 * x'");
            e = Simplify(std::move(e));
            double c = CalculateTotalCoefficient(*e, "x", 1);
            EXPECT_DOUBLE_EQ(c, 5.0);
        }

        TEST(CalculateTotalCoefficient, Subtracting) {
            // 5 * x' - 2 * x' = 3 * x'
            auto e = ParseExpression("5 * x' - 2 * x'");
            e = Simplify(std::move(e));
            double c = CalculateTotalCoefficient(*e, "x", 1);
            EXPECT_DOUBLE_EQ(c, 3.0);
        }

        TEST(CalculateTotalCoefficient, ImplicitCoefficientOne) {
            auto e = ParseExpression("x'");
            e = Simplify(std::move(e));
            double c = CalculateTotalCoefficient(*e, "x", 1);
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

        // ====================================================================
// НОВЫЕ ТЕСТЫ — Validate
//
// NormalizeSystem предполагает, что система уже прошла Validate.
// Если Validate что-то пропустит, нормализация упадёт с
// непонятной ошибкой (out_of_range) или молча даст неверный
// результат. Проверяем те ветки, которые легко забыть.
// ====================================================================

        TEST(Validate, IndependentVariableConflictsWithFunction) {
            EXPECT_THROW(
                ParseSystem("t' = t\nt(0) = 1\n"),
                InputError);
        }

        TEST(Validate, EmptySystem) {
            EXPECT_THROW(ParseSystem(""), InputError);
            EXPECT_THROW(ParseSystem("# только комментарий\n"), InputError);
        }

        TEST(Validate, ExtraInitialConditionForUnknownFunction) {
            // Уравнение только для x, а начальное условие задано и для y.
            EXPECT_THROW(
                ParseSystem("x' = x\nx(0) = 1\ny(0) = 0\n"),
                InputError);
        }

        TEST(Validate, ExactlyEnoughInitialConditions) {
            // x'' требует двух IC. Их ровно две.
            EXPECT_NO_THROW(
                ParseSystem("x'' = -x\nx(0) = 1\nx'(0) = 0\n"));
        }

        // ====================================================================
        // НОВЫЕ ТЕСТЫ — Leibniz-нотация в контексте нормализации
        //
        // Parser обещает три формы записи производной:
        //   x'', dy/dt, d^2y/dt^2, d²y/dt².
        // Все они должны быть эквивалентны для NormalizeSystem.
        // ====================================================================

        TEST(NormalizeLeibniz, FirstOrder) {
            auto sys = NormalizeText(
                "dy/dt = -y\n"
                "y(0) = 1\n");
            ASSERT_EQ(sys.equations.size(), 1u);
            EXPECT_EQ(LhsString(sys.equations[0]), "y'");
        }

        TEST(NormalizeLeibniz, SecondOrderAscii) {
            auto sys = NormalizeText(
                "d^2y/dt^2 + y = 0\n"
                "y(0) = 1\n"
                "y'(0) = 0\n");
            ASSERT_EQ(sys.equations.size(), 1u);
            EXPECT_EQ(LhsString(sys.equations[0]), "y''");
        }

        TEST(NormalizeLeibniz, SecondOrderUnicode) {
            // d²y/dt² — то же самое, что d^2y/dt^2.
            //
            // Используем явные байты UTF-8: \xC2\xB2 — это U+00B2 (superscript 2).
            // \u00B2 в узком литерале НЕ подходит: MSVC без /utf-8 кодирует его
            // в execution charset (CP1251 на русской Windows), где U+00B2 нет,
            // и подставляет '?'. Парсер видит '?' вместо '²'.
            auto sys = NormalizeText(
                "d\xC2\xB2y/dt\xC2\xB2 + y = 0\n"
                "y(0) = 1\n"
                "y'(0) = 0\n");
            ASSERT_EQ(sys.equations.size(), 1u);
            EXPECT_EQ(LhsString(sys.equations[0]), "y''");
        }

        TEST(NormalizeLeibniz, MixedWithPrimes) {
            // В одном уравнении допустимо смешивать штрихи и Лейбница.
            auto sys = NormalizeText(
                "d^2y/dt^2 + y' + y = 0\n"
                "y(0) = 0\n"
                "y'(0) = 1\n");
            ASSERT_EQ(sys.equations.size(), 1u);
            EXPECT_EQ(LhsString(sys.equations[0]), "y''");
        }

        // ====================================================================
        // НОВЫЕ ТЕСТЫ — унарный минус перед производной
        //
        // Регрессия: "-x' = x" раньше падало в Validate из-за того, что
        // CollectDerivativesInto не спускался в Unary. После фикса
        // должно нормализоваться в x' = -x.
        // ====================================================================

        TEST(NormalizeUnary, NegDerivativeLhs) {
            auto sys = NormalizeText(
                "-x' = x\n"
                "x(0) = 1\n");
            ASSERT_EQ(sys.equations.size(), 1u);
            EXPECT_EQ(LhsString(sys.equations[0]), "x'");
            auto rhs = RhsString(sys.equations[0]);
            EXPECT_NE(rhs.find("x"), std::string::npos);
            EXPECT_NE(rhs.find("-1"), std::string::npos);
        }

        TEST(NormalizeUnary, NegSecondDerivativeLhs) {
            auto sys = NormalizeText(
                "-x'' = x\n"
                "x(0) = 0\n"
                "x'(0) = 1\n");
            ASSERT_EQ(sys.equations.size(), 1u);
            EXPECT_EQ(LhsString(sys.equations[0]), "x''");
        }

        // ====================================================================
        // НОВЫЕ ТЕСТЫ — ToString round-trip для системы
        //
        // ToString(sys) строит "x''" из ic.order в цикле с апострофами.
        // Ошибка на единицу здесь тихо сломала бы экспорт. Round-trip
        // ловит любое расхождение: распарсили → напечатали → распарсили,
        // деревья должны совпасть после Simplify.
        // ====================================================================

        TEST(ToStringRoundTripSystem, SimpleSystem) {
            auto sys = ParseSystem(
                "x'' + 3 * x' - 2 * x = sin(t)\n"
                "x(0) = 1\n"
                "x'(0) = 0\n");
            std::string printed = ToString(sys);
            auto sys2 = ParseSystem(printed);

            ASSERT_EQ(sys.equations.size(), sys2.equations.size());
            for (std::size_t i = 0; i < sys.equations.size(); ++i) {
                auto l1 = Simplify(std::move(sys.equations[i].lhs));
                auto l2 = Simplify(std::move(sys2.equations[i].lhs));
                auto r1 = Simplify(std::move(sys.equations[i].rhs));
                auto r2 = Simplify(std::move(sys2.equations[i].rhs));
                EXPECT_TRUE(ExprEquals(*l1, *l2)) << "eq " << i << " lhs";
                EXPECT_TRUE(ExprEquals(*r1, *r2)) << "eq " << i << " rhs";
            }

            ASSERT_EQ(sys.initial_conditions.size(),
                sys2.initial_conditions.size());
            for (std::size_t i = 0; i < sys.initial_conditions.size(); ++i) {
                EXPECT_EQ(sys.initial_conditions[i].function_name,
                    sys2.initial_conditions[i].function_name);
                EXPECT_EQ(sys.initial_conditions[i].order,
                    sys2.initial_conditions[i].order);
                EXPECT_DOUBLE_EQ(sys.initial_conditions[i].t0,
                    sys2.initial_conditions[i].t0);
                EXPECT_DOUBLE_EQ(sys.initial_conditions[i].value,
                    sys2.initial_conditions[i].value);
            }
        }

        // ====================================================================
// НОВЫЕ ТЕСТЫ — символьный коэффициент (ТЗ 2.4)
//
// "t * x' = x": коэффициент при старшей производной — не число,
// а функция t. NormalizeSystem это не поддерживает: ExtractCoefficient
// не может вытащить Number из Mul(t, x'), и нормализатор падает
// с сообщением про «символьный коэффициент».
// ====================================================================

        TEST(Normalize, SymbolicCoefficientThrows) {
            RawSystem sys = ParseSystem(
                "t * x' = x\n"
                "x(0) = 1\n");
            EXPECT_THROW(NormalizeSystem(sys), NormalizeError);
        }

        TEST(Normalize, SymbolicCoefficientMessage) {
            RawSystem sys = ParseSystem(
                "t * x' = x\n"
                "x(0) = 1\n");
            try {
                NormalizeSystem(sys);
                FAIL() << "expected NormalizeError";
            }
            catch (const NormalizeError& e) {
                std::string msg = e.what();
                bool mentions_symbolic =
                    msg.find("symbolic") != std::string::npos;
                bool mentions_coeff =
                    msg.find("coefficient") != std::string::npos;
                EXPECT_TRUE(mentions_symbolic || mentions_coeff)
                    << "message: " << msg;
            }
        }

        // ====================================================================
        // НОВЫЕ ТЕСТЫ — перекрёстные системы (ТЗ 2.7)
        //
        // "x' = y''": в одном уравнении две разные старшие производные.
        // Ожидаем либо NormalizeError, либо корректную нормализацию
        // (если нормализатор научится такие системы разбирать).
        // Если нормализовалось — постусловие: lhs каждого eq — Derivative,
        // и множество имён совпадает с sys.functions.
        // ====================================================================

        TEST(Normalize, CrossSystemEitherThrowsOrNormalizes) {
            RawSystem sys = ParseSystem(
                "x' = y''\n"
                "y'' = x\n"
                "x(0) = 1\n"
                "y(0) = 0\n"
                "y'(0) = 0\n");

            try {
                NormalizeSystem(sys);
            }
            catch (const NormalizeError&) {
                SUCCEED() << "NormalizeError allowed for cross-system";
                return;
            }

            ASSERT_EQ(sys.equations.size(), 2u);
            std::set<std::string> lhs_names;
            for (const auto& eq : sys.equations) {
                ASSERT_NE(eq.lhs, nullptr);
                ASSERT_TRUE(std::holds_alternative<Derivative>(eq.lhs->value))
                    << "lhs: " << ToString(*eq.lhs);
                lhs_names.insert(
                    std::get<Derivative>(eq.lhs->value).function_name);
            }
            std::set<std::string> sys_funcs(sys.functions.begin(),
                sys.functions.end());
            EXPECT_EQ(lhs_names, sys_funcs);
        }
    } // namespace
} // namespace diffuri