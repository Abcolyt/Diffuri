// ============================================================================
// tests/unit/test_input.cpp
//
// Юнит-тесты модуля input: сборка RawSystem, валидация, запросы.
//
// Структура файла:
//   1. Вспомогательное
//   2. ParseSystem: базовые успешные случаи
//   3. ParseSystem: системы высокого порядка
//   4. ParseSystem: разные t0 в IC
//   5. ParseSystem: форматирование, комментарии, пробелы
//   6. ParseSystem: необычные имена и константы
//   7. ParseSystem: ошибки
//   8. Validate
//   9. DerivativeOrders
//  10. InitialConditionOrders
//  11. CollectT0s
//  12. ToString
//  13. Обёртки ввода: stream / file / stdin
//  14. Крупные системы и порядок в sys.functions
// ============================================================================
#include <gtest/gtest.h>

#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <set>
#include <string>

#include "input/input.h"

namespace diffuri {
    namespace {

        // ========================================================================
        // 1. Вспомогательное
        // ========================================================================

        const char* kSimpleSystem =
            "x' = x + y\n"
            "y' = x*y - 1\n"
            "x(0) = 1\n"
            "y(0) = 0\n";

        // ========================================================================
        // 2. ParseSystem: базовые успешные случаи
        // ========================================================================

        TEST(Input, ParseSystemSimple) {
            auto sys = ParseSystem(kSimpleSystem);
            EXPECT_EQ(sys.independent_variable, "t");
            ASSERT_EQ(sys.equations.size(), 2u);
            ASSERT_EQ(sys.initial_conditions.size(), 2u);
            ASSERT_EQ(sys.functions.size(), 2u);
        }

        TEST(Input, ParseSystemFunctionsCollectedFromDerivatives) {
            auto sys = ParseSystem(kSimpleSystem);
            ASSERT_EQ(sys.functions.size(), 2u);
            EXPECT_EQ(sys.functions[0], "x");
            EXPECT_EQ(sys.functions[1], "y");
        }

        TEST(Input, ParseSystemWithComments) {
            const char* text =
                "# система из двух уравнений\n"
                "x' = x + y   # первое\n"
                "\n"
                "y' = x*y - 1 # второе\n"
                "x(0) = 1\n"
                "y(0) = 0\n";
            auto sys = ParseSystem(text);
            EXPECT_EQ(sys.equations.size(), 2u);
            EXPECT_EQ(sys.initial_conditions.size(), 2u);
        }

        TEST(Input, ParseSystemInitialConditionsBeforeEquations) {
            const char* text =
                "x(0) = 1\n"
                "y(0) = 0\n"
                "x' = x + y\n"
                "y' = x*y - 1\n";
            auto sys = ParseSystem(text);
            EXPECT_EQ(sys.equations.size(), 2u);
            EXPECT_EQ(sys.initial_conditions.size(), 2u);
        }

        TEST(Input, ParseSystemCustomIndependentVariable) {
            ParseOptions opts;
            opts.independent_variable = "s";
            const char* text =
                "x' = x\n"
                "x(0) = 1\n";
            auto sys = ParseSystem(text, opts);
            EXPECT_EQ(sys.independent_variable, "s");
        }

        TEST(Input, ParseSystemFunctionsWithoutDerivativeNotInList) {
            // y не под производной -> в functions попадает только x
            const char* text =
                "x' = y\n"
                "x(0) = 1\n";
            auto sys = ParseSystem(text);
            ASSERT_EQ(sys.functions.size(), 1u);
            EXPECT_EQ(sys.functions[0], "x");
        }

        // ========================================================================
        // 3. ParseSystem: системы высокого порядка
        // ========================================================================

        TEST(Input, ParseSystemSecondOrderWithEnoughICs) {
            // x'' + x = 0, x(0) = 1, x'(0) = 0
            const char* text =
                "x'' = -x\n"
                "x(0) = 1\n"
                "x'(0) = 0\n";
            auto sys = ParseSystem(text);
            ASSERT_EQ(sys.equations.size(), 1u);
            ASSERT_EQ(sys.initial_conditions.size(), 2u);
            ASSERT_EQ(sys.functions.size(), 1u);
            EXPECT_EQ(sys.functions[0], "x");
        }

        TEST(Input, ParseSystemSecondOrderMissingICThrows) {
            // x'' + x = 0, но только x(0) = 1 — не хватает условия на x'
            const char* text =
                "x'' = -x\n"
                "x(0) = 1\n";
            EXPECT_THROW(ParseSystem(text), InputError);
        }

        TEST(Input, ParseSystemThirdOrder) {
            // x''' = 0, три начальных условия
            const char* text =
                "x''' = 0\n"
                "x(0) = 0\n"
                "x'(0) = 1\n"
                "x''(0) = 2\n";
            auto sys = ParseSystem(text);
            ASSERT_EQ(sys.equations.size(), 1u);
            ASSERT_EQ(sys.initial_conditions.size(), 3u);
            EXPECT_EQ(sys.functions.size(), 1u);
        }

        TEST(Input, ParseSystemExtraInitialConditionsAllowed) {
            // x' = x, но задано 3 ICs — избыток допустим на этом этапе.
            const char* text =
                "x' = x\n"
                "x(0) = 1\n"
                "x'(0) = 0\n"
                "x''(0) = 0\n";
            EXPECT_NO_THROW(ParseSystem(text));
        }

        // ========================================================================
        // 4. ParseSystem: разные t0 в IC
        // ========================================================================

        TEST(Input, ParseSystemDifferentT0ForDifferentFunctions) {
            // Разные функции, разные точки — по решению пользователя допустимо.
            const char* text =
                "x' = x\n"
                "y' = y\n"
                "x(0) = 1\n"
                "y(1.5) = 2\n";
            EXPECT_NO_THROW(ParseSystem(text));
            auto sys = ParseSystem(text);
            auto ts = CollectT0s(sys);
            EXPECT_EQ(ts.size(), 2u);
        }

        // ========================================================================
        // 5. ParseSystem: форматирование, комментарии, пробелы
        // ========================================================================

        TEST(Input, ParseSystemCommentInsideEquation) {
            const char* text =
                "x' = x + y # производная x\n"
                "y' = x - y # производная y\n"
                "x(0) = 1 # начальное x\n"
                "y(0) = 0 # начальное y\n";
            auto sys = ParseSystem(text);
            ASSERT_EQ(sys.equations.size(), 2u);
            ASSERT_EQ(sys.initial_conditions.size(), 2u);
        }

        TEST(Input, ParseSystemBlankLinesEverywhere) {
            const char* text =
                "\n"
                "x' = x\n"
                "\n"
                "x(0) = 1\n"
                "\n";
            auto sys = ParseSystem(text);
            ASSERT_EQ(sys.equations.size(), 1u);
            ASSERT_EQ(sys.initial_conditions.size(), 1u);
        }

        TEST(Input, ParseSystemTabsAndSpaces) {
            const char* text =
                "\tx'\t=\tx\t+\ty\t\n"
                "\ty'\t=\tx\t-\ty\t\n"
                "\tx(0)\t=\t1\t\n"
                "\ty(0)\t=\t0\t\n";
            auto sys = ParseSystem(text);
            ASSERT_EQ(sys.equations.size(), 2u);
            ASSERT_EQ(sys.initial_conditions.size(), 2u);
        }

        // ========================================================================
        // 6. ParseSystem: необычные имена и константы
        // ========================================================================

        TEST(Input, ParseSystemFunctionNamesWithDigits) {
            // x1, x2 — допустимые имена.
            const char* text =
                "x1' = x2\n"
                "x2' = -x1\n"
                "x1(0) = 1\n"
                "x2(0) = 0\n";
            auto sys = ParseSystem(text);
            ASSERT_EQ(sys.functions.size(), 2u);
            EXPECT_EQ(sys.functions[0], "x1");
            EXPECT_EQ(sys.functions[1], "x2");
        }

        TEST(Input, ParseSystemWithConstantsInEquations) {
            // Константа pi из extra_constants.
            ParseOptions opts;
            opts.extra_constants = { {"pi", 3.14159265358979} };
            const char* text =
                "x' = pi * x\n"
                "x(0) = 1\n";
            auto sys = ParseSystem(text, opts);
            ASSERT_EQ(sys.equations.size(), 1u);
            ASSERT_EQ(sys.functions.size(), 1u);
        }

        TEST(Input, ParseSystemCustomIndependentVariableWithLeibniz) {
            // Независимая переменная s, производная через Лейбниц.
            ParseOptions opts;
            opts.independent_variable = "s";
            const char* text =
                "dx/ds = x\n"
                "x(0) = 1\n";
            auto sys = ParseSystem(text, opts);
            EXPECT_EQ(sys.independent_variable, "s");
        }

        // ========================================================================
        // 7. ParseSystem: ошибки
        // ========================================================================

        TEST(Input, ParseSystemEmptyThrows) {
            EXPECT_THROW(ParseSystem(""), InputError);
            EXPECT_THROW(ParseSystem("# только комментарий\n"), InputError);
        }

        TEST(Input, ParseSystemSyntaxErrorPropagates) {
            // Синтаксическая ошибка -> ParseError, не InputError
            EXPECT_THROW(ParseSystem("x' = x +\n"), ParseError);
        }

        TEST(Input, ParseSystemIndependentVariableCollidesWithFunction) {
            // t' = t  -- независимая переменная совпадает с функцией
            const char* text =
                "t' = t\n"
                "t(0) = 1\n";
            EXPECT_THROW(ParseSystem(text), InputError);
        }

        TEST(Input, ParseSystemMissingInitialCondition) {
            // x' и x'' -> нужно минимум 2 начальных условия, дали 1
            const char* text =
                "x'' = -x\n"
                "x(0) = 1\n";
            EXPECT_THROW(ParseSystem(text), InputError);
        }

        TEST(Input, ParseSystemThrowsOnLineWithoutEquals) {
            // Строка без '=' и без производной — это и не уравнение, и не IC.
            const char* text =
                "x' = x\n"
                "это не уравнение и не IC\n"
                "x(0) = 1\n";
            EXPECT_THROW(ParseSystem(text), ParseError);
        }

        TEST(Input, ParseSystemErrorInSecondLineHasCorrectLineNumber) {
            // Многострочный текст: ошибка во второй строке.
            const char* text =
                "x' = x\n"
                "y' = @@\n";
            try {
                ParseSystem(text);
                FAIL() << "expected ParseError";
            }
            catch (const ParseError& e) {
                EXPECT_EQ(e.line(), 2)
                    << "ошибка во второй строке должна иметь line() == 2";
            }
        }

        TEST(Input, ParseSystemEmptyICRhsThrows) {
            const char* text =
                "x' = x\n"
                "x(0) =\n";
            EXPECT_THROW(ParseSystem(text), ParseError);
        }

        // ========================================================================
        // 8. Validate
        // ========================================================================

        TEST(Input, ValidateAcceptsSimpleSystem) {
            auto sys = ParseSystem(kSimpleSystem);
            EXPECT_NO_THROW(Validate(sys));
        }

        TEST(Input, ValidateRejectsEmptyIndependentVariable) {
            auto sys = ParseSystem(kSimpleSystem);
            sys.independent_variable = "";
            EXPECT_THROW(Validate(sys), InputError);
        }

        TEST(Input, ValidateRejectsEmptySystem) {
            RawSystem sys;
            EXPECT_THROW(Validate(sys), InputError);
        }

        TEST(Input, ValidateRejectsInitialConditionForUnknownFunction) {
            // x' = x, x(0) = 1, y(0) = 1 — y нет ни в одном уравнении.
            const char* text =
                "x' = x\n"
                "x(0) = 1\n"
                "y(0) = 1\n";
            EXPECT_THROW(ParseSystem(text), InputError);
        }

        TEST(Input, ValidateAcceptsManyFunctionsWithoutDerivativeInRHS) {
            // y и z упомянуты, но не под производной — это параметры.
            // Список functions содержит только x.
            const char* text =
                "x' = y + z\n"
                "x(0) = 1\n";
            auto sys = ParseSystem(text);
            ASSERT_EQ(sys.functions.size(), 1u);
            EXPECT_EQ(sys.functions[0], "x");
        }

        // ========================================================================
        // 9. DerivativeOrders
        // ========================================================================

        TEST(Input, DerivativeOrdersSimple) {
            const char* text =
                "x'' = -x\n"
                "y'  = x*y\n"
                "x(0) = 1\n"
                "x'(0) = 0\n"
                "y(0) = 1\n";
            auto sys = ParseSystem(text);
            auto orders = DerivativeOrders(sys);
            ASSERT_EQ(orders.size(), 2u);
            EXPECT_EQ(orders.at("x"), 2);
            EXPECT_EQ(orders.at("y"), 1);
        }

        TEST(Input, DerivativeOrdersMissingFunctionNotInMap) {
            // y только упомянут, но не под производной
            const char* text =
                "x' = y\n"
                "x(0) = 1\n";
            auto sys = ParseSystem(text);
            auto orders = DerivativeOrders(sys);
            EXPECT_EQ(orders.count("y"), 0u);
            EXPECT_EQ(orders.at("x"), 1);
        }

        TEST(Input, DerivativeOrdersTakesMaxPerFunction) {
            // x' встречается и как x', и как x'' — берётся 2.
            const char* text =
                "x'' = -x\n"
                "x(0) = 1\n"
                "x'(0) = 0\n";
            auto sys = ParseSystem(text);
            auto orders = DerivativeOrders(sys);
            EXPECT_EQ(orders.at("x"), 2);
        }

        TEST(Input, DerivativeOrdersInRhsOnly) {
            // Производная только в правой части: x' = y'.
            const char* text =
                "x' = y'\n"
                "y' = 0\n"
                "x(0) = 0\n"
                "y(0) = 0\n";
            auto sys = ParseSystem(text);
            auto orders = DerivativeOrders(sys);
            EXPECT_EQ(orders.at("x"), 1);
            EXPECT_EQ(orders.at("y"), 1);
        }

        TEST(Input, DerivativeOrdersInsideCall) {
            // Производная внутри sin(...) в правой части.
            const char* text =
                "x' = sin(y')\n"
                "y' = 0\n"
                "x(0) = 0\n"
                "y(0) = 0\n";
            auto sys = ParseSystem(text);
            auto orders = DerivativeOrders(sys);
            EXPECT_EQ(orders.at("x"), 1);
            EXPECT_EQ(orders.at("y"), 1);
        }

        // ========================================================================
        // 10. InitialConditionOrders
        // ========================================================================

        TEST(Input, InitialConditionOrdersSimple) {
            const char* text =
                "x'' = -x\n"
                "y'  = x*y\n"
                "x(0) = 1\n"
                "x'(0) = 0\n"
                "y(0) = 1\n";
            auto sys = ParseSystem(text);
            auto orders = InitialConditionOrders(sys);
            EXPECT_EQ(orders.at("x"), 1);
            EXPECT_EQ(orders.at("y"), 0);
        }

        TEST(Input, InitialConditionOrdersMissingFunctionNotInMap) {
            const char* text =
                "x' = x\n"
                "x(0) = 1\n";
            auto sys = ParseSystem(text);
            auto orders = InitialConditionOrders(sys);
            EXPECT_EQ(orders.count("y"), 0u);
        }

        TEST(Input, InitialConditionOrdersTakesMaxOrder) {
            const char* text =
                "x'' = -x\n"
                "x(0) = 1\n"
                "x'(0) = 0\n";
            auto sys = ParseSystem(text);
            auto orders = InitialConditionOrders(sys);
            EXPECT_EQ(orders.at("x"), 1);
        }

        TEST(Input, InitialConditionOrdersMultipleFunctions) {
            const char* text =
                "x'' = -x\n"
                "y' = y\n"
                "x(0) = 1\n"
                "x'(0) = 0\n"
                "y(0) = 1\n";
            auto sys = ParseSystem(text);
            auto orders = InitialConditionOrders(sys);
            EXPECT_EQ(orders.size(), 2u);
            EXPECT_EQ(orders.at("x"), 1);
            EXPECT_EQ(orders.at("y"), 0);
        }

        // ========================================================================
        // 11. CollectT0s
        // ========================================================================

        TEST(Input, CollectT0sSinglePoint) {
            auto sys = ParseSystem(kSimpleSystem);
            auto ts = CollectT0s(sys);
            ASSERT_EQ(ts.size(), 1u);
            EXPECT_EQ(*ts.begin(), 0.0);
        }

        TEST(Input, CollectT0sMultiplePoints) {
            const char* text =
                "x' = x\n"
                "y' = y\n"
                "x(0) = 1\n"
                "y(1.5) = 2\n";
            auto sys = ParseSystem(text);
            auto ts = CollectT0s(sys);
            ASSERT_EQ(ts.size(), 2u);
            EXPECT_EQ(ts.count(0.0), 1u);
            EXPECT_EQ(ts.count(1.5), 1u);
        }

        TEST(Input, CollectT0sEmptyWhenNoICs) {
            // Несмотря на то, что система из одного уравнения без IC не
            // пройдёт Validate, вызов CollectT0s на "сырой" структуре
            // должен работать.
            RawSystem sys;
            sys.equations.push_back(
                Equation{ MakeFunction("x"), MakeFunction("x") });
            auto ts = CollectT0s(sys);
            EXPECT_TRUE(ts.empty());
        }

        TEST(Input, CollectT0sDeduplicates) {
            const char* text =
                "x'' = -x\n"
                "x(0) = 1\n"
                "x'(0) = 0\n";
            auto sys = ParseSystem(text);
            auto ts = CollectT0s(sys);
            ASSERT_EQ(ts.size(), 1u);
            EXPECT_EQ(*ts.begin(), 0.0);
        }

        TEST(Input, CollectT0sThreePoints) {
            const char* text =
                "x' = x\n"
                "y' = y\n"
                "z' = z\n"
                "x(0) = 1\n"
                "y(1.5) = 1\n"
                "z(-2.25) = 1\n";
            auto sys = ParseSystem(text);
            auto ts = CollectT0s(sys);
            ASSERT_EQ(ts.size(), 3u);
            EXPECT_EQ(ts.count(0.0), 1u);
            EXPECT_EQ(ts.count(1.5), 1u);
            EXPECT_EQ(ts.count(-2.25), 1u);
        }

        // ========================================================================
        // 12. ToString
        // ========================================================================

        TEST(Input, ToStringNotEmptyForSimpleSystem) {
            auto sys = ParseSystem(kSimpleSystem);
            auto s = ToString(sys);
            EXPECT_FALSE(s.empty());
        }

        TEST(Input, ToStringRoundTrip) {
            auto sys1 = ParseSystem(kSimpleSystem);
            auto s = ToString(sys1);
            auto sys2 = ParseSystem(s);
            EXPECT_EQ(sys1.equations.size(), sys2.equations.size());
            EXPECT_EQ(sys1.initial_conditions.size(),
                sys2.initial_conditions.size());
            EXPECT_EQ(sys1.functions, sys2.functions);
            EXPECT_EQ(sys1.independent_variable, sys2.independent_variable);
        }

        TEST(Input, ToStringSingleEquation) {
            const char* text =
                "x' = y\n"
                "x(0) = 1\n"
                "y(0) = 0\n"
                "y' = -x\n";
            auto sys = ParseSystem(text);
            auto s = ToString(sys);
            // Формат: сначала все уравнения, потом все ICs.
            EXPECT_NE(s.find("x' = y"), std::string::npos);
            EXPECT_NE(s.find("y' = (-x)"), std::string::npos);
            EXPECT_NE(s.find("x(0) = 1"), std::string::npos);
            EXPECT_NE(s.find("y(0) = 0"), std::string::npos);
        }

        TEST(Input, ToStringInitialConditionWithDerivative) {
            const char* text =
                "x'' = -x\n"
                "x(0) = 1\n"
                "x'(0) = 0\n";
            auto sys = ParseSystem(text);
            auto s = ToString(sys);
            EXPECT_NE(s.find("x(0) = 1"), std::string::npos);
            EXPECT_NE(s.find("x'(0) = 0"), std::string::npos);
        }

        TEST(Input, ToStringNonZeroT0) {
            const char* text =
                "x' = x\n"
                "x(1.5) = 2.5\n";
            auto sys = ParseSystem(text);
            auto s = ToString(sys);
            EXPECT_NE(s.find("x(1.5) = 2.5"), std::string::npos);
        }

        TEST(Input, ToStringRoundTripComplexSystem) {
            const char* text =
                "x'' = -x + sin(t)\n"
                "y' = x * y - 1\n"
                "x(0) = 1\n"
                "x'(0) = 0\n"
                "y(0.5) = 2\n";
            auto sys1 = ParseSystem(text);
            auto s = ToString(sys1);
            auto sys2 = ParseSystem(s);

            EXPECT_EQ(sys1.functions, sys2.functions);
            EXPECT_EQ(sys1.equations.size(), sys2.equations.size());
            EXPECT_EQ(sys1.initial_conditions.size(),
                sys2.initial_conditions.size());
            EXPECT_EQ(sys1.independent_variable, sys2.independent_variable);

            for (std::size_t i = 0; i < sys1.initial_conditions.size(); ++i) {
                EXPECT_EQ(sys1.initial_conditions[i].function_name,
                    sys2.initial_conditions[i].function_name);
                EXPECT_EQ(sys1.initial_conditions[i].order,
                    sys2.initial_conditions[i].order);
                EXPECT_DOUBLE_EQ(sys1.initial_conditions[i].t0,
                    sys2.initial_conditions[i].t0);
                EXPECT_DOUBLE_EQ(sys1.initial_conditions[i].value,
                    sys2.initial_conditions[i].value);
            }
        }

        // ========================================================================
        // 13. Обёртки ввода: stream / file / stdin
        // ========================================================================

        TEST(Input, ParseSystemFromFileMissingThrows) {
            EXPECT_THROW(
                ParseSystemFromFile("/nonexistent/path/to/system.txt"),
                InputError);
        }

        TEST(Input, ParseSystemFromStreamMatchesString) {
            std::istringstream is(kSimpleSystem);
            auto sys_stream = ParseSystem(is);
            auto sys_string = ParseSystem(kSimpleSystem);
            EXPECT_EQ(ToString(sys_stream), ToString(sys_string));
        }

        TEST(Input, ParseSystemFromFileMatchesString) {
            const std::string path = "test_system_temp.txt";
            {
                std::ofstream f(path);
                f << kSimpleSystem;
            }
            auto sys_file = ParseSystemFromFile(path);
            auto sys_string = ParseSystem(kSimpleSystem);
            EXPECT_EQ(ToString(sys_file), ToString(sys_string));
            std::remove(path.c_str());
        }

        TEST(Input, ParseSystemFromStdinUntilEof) {
            std::istringstream iss("x' = x\n\nx(0) = 1\n");
            auto old_rdbuf = std::cin.rdbuf(iss.rdbuf());
            auto sys = ParseSystemFromStdin(StdinMode::UntilEof);
            std::cin.rdbuf(old_rdbuf);

            EXPECT_EQ(sys.equations.size(), 1u);
            EXPECT_EQ(sys.initial_conditions.size(), 1u);
        }

        TEST(Input, ParseSystemFromStdinUntilBlankLine) {
            std::istringstream iss(
                "x' = x\nx(0) = 1\n\ny' = y\ny(0) = 0\n");
            auto old_rdbuf = std::cin.rdbuf(iss.rdbuf());
            auto sys = ParseSystemFromStdin(StdinMode::UntilBlankLine);
            std::cin.rdbuf(old_rdbuf);

            EXPECT_EQ(sys.equations.size(), 1u);
            EXPECT_EQ(sys.initial_conditions.size(), 1u);
        }

        TEST(Input, ParseSystemFromStdinEmptyUntilEofThrows) {
            std::istringstream iss("");
            auto old_rdbuf = std::cin.rdbuf(iss.rdbuf());
            EXPECT_THROW(ParseSystemFromStdin(StdinMode::UntilEof),
                InputError);
            std::cin.rdbuf(old_rdbuf);
        }

        TEST(Input, ParseSystemCoreReadsUntilEof) {
            // Ядро читает поток до самого EOF. В текстовом формате нет
            // разделителя между «разными системами», поэтому все строки
            // сливаются в одну RawSystem.
            std::istringstream iss(
                "x' = x\nx(0) = 1\ny' = y\ny(0) = 0\n");
            auto sys = ParseSystem(iss);

            // Ожидаем, что прочитались все 4 строки
            EXPECT_EQ(sys.equations.size(), 2u);
            EXPECT_EQ(sys.initial_conditions.size(), 2u);

            // Главное: поток должен быть вычитан до конца (EOF)
            EXPECT_TRUE(iss.eof());
        }

        // ========================================================================
        // 14. Крупные системы и порядок в sys.functions
        // ========================================================================

        TEST(Input, ParseSystemLorenzLike) {
            // Уравнения Лоренца — три уравнения, три ICs.
            const char* text =
                "x' = -10*x + 10*y\n"
                "y' = -x*z + 28*x - y\n"
                "z' = x*y - 8*z/3\n"
                "x(0) = -13.76\n"
                "y(0) = -19.58\n"
                "z(0) = 27\n";
            auto sys = ParseSystem(text);
            ASSERT_EQ(sys.equations.size(), 3u);
            ASSERT_EQ(sys.initial_conditions.size(), 3u);
            ASSERT_EQ(sys.functions.size(), 3u);
            EXPECT_EQ(sys.functions[0], "x");
            EXPECT_EQ(sys.functions[1], "y");
            EXPECT_EQ(sys.functions[2], "z");
        }

        TEST(Input, ParseSystemManyEquations) {
            // Система из 10 уравнений — проверяем, что нет квадратичных
            // проблем с размером.
            std::string text;
            for (int i = 0; i < 10; ++i) {
                text += "x" + std::to_string(i) + "' = x"
                    + std::to_string(i) + "\n";
            }
            for (int i = 0; i < 10; ++i) {
                text += "x" + std::to_string(i) + "(0) = 0\n";
            }
            auto sys = ParseSystem(text);
            EXPECT_EQ(sys.equations.size(), 10u);
            EXPECT_EQ(sys.initial_conditions.size(), 10u);
            EXPECT_EQ(sys.functions.size(), 10u);
        }

        TEST(Input, FunctionsOrderIsFirstAppearance) {
            // Порядок в functions — по первому появлению под производной при
            // обходе уравнений сверху вниз (lhs, потом rhs).
            const char* text =
                "b' = a' \n"
                "a' = 0\n"
                "a(0) = 0\n"
                "b(0) = 0\n";
            auto sys = ParseSystem(text);
            ASSERT_EQ(sys.functions.size(), 2u);
            EXPECT_EQ(sys.functions[0], "b");
            EXPECT_EQ(sys.functions[1], "a");
        }

    } // namespace
} // namespace diffuri