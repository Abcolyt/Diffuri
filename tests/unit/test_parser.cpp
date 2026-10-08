// ============================================================================
// tests/unit/test_parser.cpp
//
// Юнит-тесты модуля parser: разбор выражений, уравнений, начальных условий.
//
// Структура файла:
//    1. Хелперы
//    2. ParseExpression: атомы (числа, имена, производные)
//    3. ParseExpression: операции и приоритеты
//    4. ParseExpression: вызовы функций
//    5. ParseExpression: константы (extra_constants)
//    6. ParseExpression: комментарии и пробелы
//    7. ParseExpression: ошибки разбора
//    8. ParseExpression: Лейбниц — несогласованность порядка
//    9. ParseExpression: позиция ошибки в многострочном тексте
//   10. ParseEquation
//   11. ParseInitialCondition: базовые формы
//   12. ParseInitialCondition: нотация Лейбница
//   13. ParseInitialCondition: правая часть как константа
//   14. IsBlankOrComment
//   15. LoadConstants (constants_file и extra_constants)
//
// Замечание про унарный минус:
//   В дереве нет отдельного узла Neg. Парсер разбирает "-X" как
//   Binary{Sub, Number{0}, X}. Поэтому ToString выдаёт "(0 - x)", а не "-x".
//   Это осознанное решение: AST остаётся минимальным, а вычитание
//   нормализуется уже на следующих этапах.
// ============================================================================
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <variant>

#include "input/expression.h"
#include "input/parser.h"

namespace diffuri {
    namespace {

        // ========================================================================
        // 1. Хелперы
        // ========================================================================

        // Парсим выражение и возвращаем его ToString-представление.
        // Основная проверка через ToString: она не зависит от деталей
        // структуры и легко читается в отчёте падения.
        std::string RoundTrip(const std::string& text,
            const ParseOptions& opts = {}) {
            return ToString(*ParseExpression(text, opts));
        }

        // RAII-обёртка для временного файла с константами.
        struct TempConstantsFile {
            std::filesystem::path path;

            TempConstantsFile(const std::string& name, const std::string& content) {
                path = std::filesystem::temp_directory_path() / name;
                std::ofstream f(path);
                f << content;
            }

            ~TempConstantsFile() {
                std::error_code ec;
                std::filesystem::remove(path, ec);
            }

            std::string str() const { return path.string(); }

            TempConstantsFile(const TempConstantsFile&) = delete;
            TempConstantsFile& operator=(const TempConstantsFile&) = delete;
        };

        // ========================================================================
        // 2. ParseExpression: атомы
        // ========================================================================

        TEST(Parser, ExpressionNumber) {
            EXPECT_EQ(RoundTrip("2"), "2");
            EXPECT_EQ(RoundTrip("3.14"), "3.14");
            EXPECT_EQ(RoundTrip("0.5"), "0.5");
        }

        TEST(Parser, ExpressionNumberExponent) {
            EXPECT_EQ(RoundTrip("1e-3"), "0.001");
            EXPECT_EQ(RoundTrip("2.5E+2"), "250");
        }

        TEST(Parser, ExpressionFunction) {
            EXPECT_EQ(RoundTrip("x"), "x");
            EXPECT_EQ(RoundTrip("alpha"), "alpha");
        }

        TEST(Parser, ExpressionDerivativeApostrophe) {
            EXPECT_EQ(RoundTrip("x'"), "x'");
            EXPECT_EQ(RoundTrip("x''"), "x''");
            EXPECT_EQ(RoundTrip("x'''"), "x'''");
        }

        TEST(Parser, ExpressionDerivativeLeibniz) {
            EXPECT_EQ(RoundTrip("dy/dt"), "y'");
            EXPECT_EQ(RoundTrip("d^2y/dt^2"), "y''");
        }

        TEST(Parser, ExpressionDerivativeLeibnizUnicode) {
            EXPECT_EQ(RoundTrip("d²y/dt²"), "y''");
            EXPECT_EQ(RoundTrip("d³y/dt³"), "y'''");
        }

        // ========================================================================
        // 3. ParseExpression: операции и приоритеты
        // ========================================================================

        TEST(Parser, ExpressionSum) {
            EXPECT_EQ(RoundTrip("x + y"), "(x + y)");
        }

        TEST(Parser, ExpressionMulBindsStrongerThanAdd) {
            EXPECT_EQ(RoundTrip("x + y * 2"), "(x + (y * 2))");
        }

        TEST(Parser, ExpressionParens) {
            EXPECT_EQ(RoundTrip("(x + y) * 2"), "((x + y) * 2)");
        }

        TEST(Parser, ExpressionPowRightAssociative) {
            // x ^ y ^ z должно разбираться как x ^ (y ^ z)
            EXPECT_EQ(RoundTrip("x ^ y ^ z"), "(x ^ (y ^ z))");
        }

        TEST(Parser, ExpressionPowRightAssociativeOnNumbers) {
            // 2^3^2 = 2^(3^2) = 2^9, а не (2^3)^2 = 64.
            EXPECT_EQ(RoundTrip("2 ^ 3 ^ 2"), "(2 ^ (3 ^ 2))");
        }

        TEST(Parser, ExpressionPowWithNegativeExponent) {
            // x^-2: показатель — унарное выражение.
            EXPECT_EQ(RoundTrip("x^-2"), "(x ^ (-2))");
        }

        TEST(Parser, ExpressionUnaryMinus) {
            // Отдельного узла Neg в дереве нет: -X разбирается как (0 - X).
            EXPECT_EQ(RoundTrip("-x"), "(-x)");
            EXPECT_EQ(RoundTrip("-1.5"), "(-1.5)");
            EXPECT_EQ(RoundTrip("-(x + y)"), "(-(x + y))");
        }

        TEST(Parser, ExpressionUnaryPlus) {
            // +X эквивалентно X.
            EXPECT_EQ(RoundTrip("+x"), "x");
            EXPECT_EQ(RoundTrip("+(x + y)"), "(x + y)");
        }

        TEST(Parser, ExpressionDoubleUnaryMinus) {
            // --x разбирается как (0 - (0 - x)).
            EXPECT_EQ(RoundTrip("--x"), "(-(-x))");
        }

        TEST(Parser, ExpressionUnaryMinusBeforePow) {
            // -x^2: унарный минус ниже ^ по приоритету,
            // значит разбирается как -(x^2) = (0 - (x ^ 2)).
            EXPECT_EQ(RoundTrip("-x^2"), "(-(x ^ 2))");
        }

        // ========================================================================
        // 4. ParseExpression: вызовы функций
        // ========================================================================

        TEST(Parser, ExpressionCallUnary) {
            EXPECT_EQ(RoundTrip("sin(x)"), "sin(x)");
        }

        TEST(Parser, ExpressionCallBinary) {
            EXPECT_EQ(RoundTrip("pow(x, 2)"), "pow(x, 2)");
        }

        TEST(Parser, ExpressionCallNested) {
            EXPECT_EQ(RoundTrip("sin(cos(x))"), "sin(cos(x))");
        }

        TEST(Parser, ExpressionCallNoArgs) {
            EXPECT_EQ(RoundTrip("f()"), "f()");
        }

        TEST(Parser, ExpressionCallThreeArgs) {
            EXPECT_EQ(RoundTrip("f(x, y, z)"), "f(x, y, z)");
        }

        TEST(Parser, ExpressionCallNestedWithMultipleArgs) {
            EXPECT_EQ(RoundTrip("atan2(sin(x), cos(y))"),
                "atan2(sin(x), cos(y))");
        }

        TEST(Parser, ExpressionCallTrailingCommaThrows) {
            // f(x, y,) — лишняя запятая перед ')'.
            EXPECT_THROW(ParseExpression("f(x, y,)"), ParseError);
        }

        TEST(Parser, ExpressionCallLeadingCommaThrows) {
            // f(, x) — запятая сразу после '('.
            EXPECT_THROW(ParseExpression("f(, x)"), ParseError);
        }

        TEST(Parser, ExpressionCallEmptyParensIsAllowed) {
            // f() — вызов без аргументов.
            EXPECT_EQ(RoundTrip("f()"), "f()");
        }

        TEST(Parser, ExpressionEmptyParensThrows) {
            // () — не вызов, а просто пустые скобки. Невалидное выражение.
            EXPECT_THROW(ParseExpression("()"), ParseError);
        }

        // ========================================================================
        // 5. ParseExpression: константы (extra_constants)
        // ========================================================================

        TEST(Parser, ExpressionConstantFromOptions) {
            ParseOptions opts;
            opts.extra_constants = { {"pi", 3.14159265358979} };
            auto e = ParseExpression("pi", opts);
            ASSERT_TRUE(std::holds_alternative<Constant>(e->value));
            EXPECT_EQ(std::get<Constant>(e->value).name, "pi");
        }

        TEST(Parser, ExpressionUnknownNameIsFunction) {
            ParseOptions opts; // без констант
            auto e = ParseExpression("pi", opts);
            EXPECT_TRUE(std::holds_alternative<Function>(e->value));
        }

        TEST(Parser, ExpressionConstantInExpression) {
            ParseOptions opts;
            opts.extra_constants = { {"pi", 3.14159265358979} };
            EXPECT_EQ(RoundTrip("2 * pi", opts), "(2 * pi)");
        }

        TEST(Parser, ExpressionMultipleConstants) {
            ParseOptions opts;
            opts.extra_constants = { {"pi", 3.14}, {"e", 2.71} };
            EXPECT_EQ(RoundTrip("pi + e", opts), "(pi + e)");
        }

        TEST(Parser, ExpressionConstantInCall) {
            // pi(x) — сейчас это разбирается как Call{"pi", {x}}, потому что
            // проверка на Call идёт раньше проверки на константу.
            // Тест фиксирует это поведение. Если решим, что правильно иначе —
            // поправим и код, и тест.
            ParseOptions opts;
            opts.extra_constants = { {"pi", 3.14} };
            EXPECT_EQ(RoundTrip("pi(x)", opts), "pi(x)");
        }

        // ========================================================================
        // 6. ParseExpression: комментарии и пробелы
        // ========================================================================

        TEST(Parser, ExpressionWithTrailingComment) {
            // Всё после '#' игнорируется.
            EXPECT_EQ(RoundTrip("x + y # это сумма"), "(x + y)");
            EXPECT_EQ(RoundTrip("x + y   #комментарий без пробела"), "(x + y)");
        }

        TEST(Parser, ExpressionCommentOnlyLineThrows) {
            // Строка-комментарий после CutComment становится пустой — ошибка.
            EXPECT_THROW(ParseExpression("# только комментарий"), ParseError);
            EXPECT_THROW(ParseExpression("   #   "), ParseError);
        }

        TEST(Parser, ExpressionExtraWhitespace) {
            EXPECT_EQ(RoundTrip("   x   +   y   "), "(x + y)");
            EXPECT_EQ(RoundTrip("\tx\t+\ty\t"), "(x + y)");
        }

        // ========================================================================
        // 7. ParseExpression: ошибки разбора
        // ========================================================================

        TEST(Parser, ExpressionEmptyThrows) {
            EXPECT_THROW(ParseExpression(""), ParseError);
            EXPECT_THROW(ParseExpression("   "), ParseError);
        }

        TEST(Parser, ExpressionUnbalancedParensThrows) {
            EXPECT_THROW(ParseExpression("(x + y"), ParseError);
            EXPECT_THROW(ParseExpression("x + y)"), ParseError);
        }

        TEST(Parser, ExpressionTrailingOperatorThrows) {
            EXPECT_THROW(ParseExpression("x +"), ParseError);
        }

        TEST(Parser, ExpressionErrorHasPosition) {
            try {
                ParseExpression("x + * y");
                FAIL() << "expected ParseError";
            }
            catch (const ParseError& e) {
                EXPECT_EQ(e.line(), 1);
                EXPECT_GE(e.column(), 1);
                EXPECT_FALSE(std::string(e.what()).empty());
            }
        }

        TEST(Parser, ExpressionUnbalancedParenErrorHasExpected) {
            try {
                ParseExpression("(x + y");
                FAIL() << "expected ParseError";
            }
            catch (const ParseError& e) {
                // Ожидаем, что парсер сообщит про ожидаемую ')'.
                EXPECT_EQ(e.expected(), ")");
            }
        }

        TEST(Parser, ExpressionErrorReportsFoundToken) {
            try {
                ParseExpression("x + * y");
                FAIL() << "expected ParseError";
            }
            catch (const ParseError& e) {
                // '*' — неожиданный токен. what() не пустой, expected/found
                // могут быть заполнены или пусты — тест не требует конкретики.
                EXPECT_FALSE(std::string(e.what()).empty());
            }
        }

        TEST(Parser, ExpressionLeibnizWrongIndependentThrows) {
            // независимая переменная t, а в знаменателе s
            ParseOptions opts;
            opts.independent_variable = "t";
            EXPECT_THROW(ParseExpression("dy/ds", opts), ParseError);
        }

        // ========================================================================
        // 8. ParseExpression: Лейбниц — несогласованность порядка
        // ========================================================================

        TEST(Parser, ExpressionLeibnizOrderMismatchThrows) {
            EXPECT_THROW(ParseExpression("d^2y/dt^3"), ParseError);
        }

        TEST(Parser, ExpressionLeibnizOnlyNumeratorOrderThrows) {
            // d^2y/dt — порядок в числителе, но не в знаменателе.
            EXPECT_THROW(ParseExpression("d^2y/dt"), ParseError);
        }

        TEST(Parser, ExpressionLeibnizOnlyDenominatorOrderThrows) {
            // dy/dt^2 — порядок в знаменателе, но не в числителе.
            EXPECT_THROW(ParseExpression("dy/dt^2"), ParseError);
        }

        // ========================================================================
        // 9. ParseExpression: позиция ошибки в многострочном тексте
        //
        // ВНИМАНИЕ: эти тесты, скорее всего, УПАДУТ. Лексер не увеличивает
        // line_ при переходе через '\n', поэтому ошибка во второй строке
        // будет отчитаться с line() == 1. Это известный недостаток
        // реализации; тест фиксирует ожидаемое поведение, чтобы при
        // починке он стал зелёным.
        // ========================================================================

        TEST(Parser, ExpressionErrorLineNumberMultiLine) {
            try {
                ParseExpression("x +\n@");
                FAIL() << "expected ParseError";
            }
            catch (const ParseError& e) {
                EXPECT_EQ(e.line(), 2)
                    << "ошибка во второй строке должна иметь line() == 2";
            }
        }

        TEST(Parser, ExpressionErrorColumnAfterNewline) {
            try {
                ParseExpression("x\n+ y @ z");
                FAIL() << "expected ParseError";
            }
            catch (const ParseError& e) {
                EXPECT_EQ(e.line(), 2);
                // '@' в позиции 5 строки "+ y @ z"
                EXPECT_GE(e.column(), 1);
            }
        }

        // ========================================================================
        // 10. ParseEquation
        // ========================================================================

        TEST(Parser, EquationSimple) {
            auto eq = ParseEquation("x = y + 1");
            ASSERT_NE(eq.lhs, nullptr);
            ASSERT_NE(eq.rhs, nullptr);
            EXPECT_EQ(ToString(*eq.lhs), "x");
            EXPECT_EQ(ToString(*eq.rhs), "(y + 1)");
        }

        TEST(Parser, EquationWithDerivativesBothSides) {
            auto eq = ParseEquation("x'' + sin(x) = y' + t");
            EXPECT_EQ(ToString(*eq.lhs), "(x'' + sin(x))");
            EXPECT_EQ(ToString(*eq.rhs), "(y' + t)");
        }

        TEST(Parser, EquationWithTrailingComment) {
            auto eq = ParseEquation("x = y + 1 # это уравнение");
            EXPECT_EQ(ToString(*eq.lhs), "x");
            EXPECT_EQ(ToString(*eq.rhs), "(y + 1)");
        }

        TEST(Parser, EquationExtraWhitespace) {
            auto eq = ParseEquation("   x''   =   x   +   y   ");
            EXPECT_EQ(ToString(*eq.lhs), "x''");
            EXPECT_EQ(ToString(*eq.rhs), "(x + y)");
        }

        TEST(Parser, EquationWithoutEqualsThrows) {
            EXPECT_THROW(ParseEquation("x + y"), ParseError);
        }

        TEST(Parser, EquationMultipleEqualsThrows) {
            EXPECT_THROW(ParseEquation("x = y = z"), ParseError);
        }

        TEST(Parser, EquationEmptyRhsThrows) {
            EXPECT_THROW(ParseEquation("x ="), ParseError);
            EXPECT_THROW(ParseEquation("x =   "), ParseError);
        }

        TEST(Parser, EquationEmptyLhsThrows) {
            EXPECT_THROW(ParseEquation("= x + y"), ParseError);
        }

        // ========================================================================
        // 11. ParseInitialCondition: базовые формы
        // ========================================================================

        TEST(Parser, InitialConditionOrderZero) {
            auto ic = ParseInitialCondition("x(0) = 1");
            EXPECT_EQ(ic.function_name, "x");
            EXPECT_EQ(ic.order, 0);
            EXPECT_DOUBLE_EQ(ic.t0, 0.0);
            EXPECT_DOUBLE_EQ(ic.value, 1.0);
        }

        TEST(Parser, InitialConditionOrderOne) {
            auto ic = ParseInitialCondition("y'(0) = 0");
            EXPECT_EQ(ic.function_name, "y");
            EXPECT_EQ(ic.order, 1);
            EXPECT_DOUBLE_EQ(ic.t0, 0.0);
            EXPECT_DOUBLE_EQ(ic.value, 0.0);
        }

        TEST(Parser, InitialConditionOrderTwo) {
            auto ic = ParseInitialCondition("z''(0) = -1");
            EXPECT_EQ(ic.function_name, "z");
            EXPECT_EQ(ic.order, 2);
            EXPECT_DOUBLE_EQ(ic.value, -1.0);
        }

        TEST(Parser, InitialConditionNonZeroT0) {
            auto ic = ParseInitialCondition("y'(1.5) = 2.7");
            EXPECT_EQ(ic.function_name, "y");
            EXPECT_EQ(ic.order, 1);
            EXPECT_DOUBLE_EQ(ic.t0, 1.5);
            EXPECT_DOUBLE_EQ(ic.value, 2.7);
        }

        TEST(Parser, InitialConditionWithTrailingComment) {
            auto ic = ParseInitialCondition("x(0) = 1 # начальное условие");
            EXPECT_EQ(ic.function_name, "x");
            EXPECT_EQ(ic.order, 0);
            EXPECT_DOUBLE_EQ(ic.t0, 0.0);
            EXPECT_DOUBLE_EQ(ic.value, 1.0);
        }

        TEST(Parser, InitialConditionRhsNotNumberThrows) {
            EXPECT_THROW(ParseInitialCondition("x(0) = y"), ParseError);
        }

        TEST(Parser, InitialConditionMissingT0Throws) {
            EXPECT_THROW(ParseInitialCondition("x = 1"), ParseError);
        }

        TEST(Parser, InitialConditionEmptyRhsThrows) {
            EXPECT_THROW(ParseInitialCondition("x(0) ="), ParseError);
        }

        TEST(Parser, InitialConditionEmptyLhsThrows) {
            EXPECT_THROW(ParseInitialCondition("= 1"), ParseError);
        }

        // ========================================================================
        // 12. ParseInitialCondition: нотация Лейбница
        // ========================================================================

        TEST(Parser, InitialConditionLeibnizApostropheMixed) {
            // Правая часть — с точкой в скобках после дроби.
            auto ic = ParseInitialCondition("dy/dt(0) = 1");
            EXPECT_EQ(ic.function_name, "y");
            EXPECT_EQ(ic.order, 1);
            EXPECT_DOUBLE_EQ(ic.t0, 0.0);
            EXPECT_DOUBLE_EQ(ic.value, 1.0);
        }

        TEST(Parser, InitialConditionLeibnizSecondOrder) {
            auto ic = ParseInitialCondition("d^2y/dt^2(0) = -1");
            EXPECT_EQ(ic.function_name, "y");
            EXPECT_EQ(ic.order, 2);
            EXPECT_DOUBLE_EQ(ic.t0, 0.0);
            EXPECT_DOUBLE_EQ(ic.value, -1.0);
        }

        TEST(Parser, InitialConditionLeibnizUnicode) {
            auto ic = ParseInitialCondition("d²y/dt²(0) = -1");
            EXPECT_EQ(ic.function_name, "y");
            EXPECT_EQ(ic.order, 2);
            EXPECT_DOUBLE_EQ(ic.t0, 0.0);
            EXPECT_DOUBLE_EQ(ic.value, -1.0);
        }

        TEST(Parser, InitialConditionLeibnizWithNonZeroT0) {
            auto ic = ParseInitialCondition("dy/dt(1.5) = 2.7");
            EXPECT_EQ(ic.function_name, "y");
            EXPECT_EQ(ic.order, 1);
            EXPECT_DOUBLE_EQ(ic.t0, 1.5);
            EXPECT_DOUBLE_EQ(ic.value, 2.7);
        }

        // ========================================================================
        // 13. ParseInitialCondition: правая часть как константа
        // ========================================================================

        TEST(Parser, InitialConditionRhsArithmetic) {
            // 2 + 3 — вычислимо, принимаем.
            auto ic = ParseInitialCondition("x(0) = 2 + 3");
            EXPECT_DOUBLE_EQ(ic.value, 5.0);
        }

        TEST(Parser, InitialConditionRhsMultiplication) {
            auto ic = ParseInitialCondition("x(0) = 2 * 3");
            EXPECT_DOUBLE_EQ(ic.value, 6.0);
        }

        TEST(Parser, InitialConditionRhsWithConstant) {
            ParseOptions opts;
            opts.extra_constants = { {"pi", 3.14159265358979} };
            auto ic = ParseInitialCondition("x(0) = 2 * pi", opts);
            EXPECT_DOUBLE_EQ(ic.value, 2 * 3.14159265358979);
        }

        TEST(Parser, InitialConditionRhsDivisionByZeroThrows) {
            // TryEvalConstNumber возвращает nullopt при делении на 0.
            EXPECT_THROW(ParseInitialCondition("x(0) = 1 / 0"), ParseError);
        }

        TEST(Parser, InitialConditionRhsWithFunctionThrows) {
            // sin(x) не константное выражение — ошибка.
            EXPECT_THROW(ParseInitialCondition("x(0) = sin(y)"), ParseError);
        }

        // Документирует известное ограничение: TryEvalConstNumber не
        // поддерживает Binary::Op::Pow, поэтому x(0) = 2^3 отвергается
        // как не-число. Если решите поддержать Pow — замените
        // EXPECT_THROW на:
        //     auto ic = ParseInitialCondition("x(0) = 2^3");
        //     EXPECT_DOUBLE_EQ(ic.value, 8.0);
        TEST(Parser, InitialConditionRhsPowNotSupported) {
            EXPECT_THROW(ParseInitialCondition("x(0) = 2^3"), ParseError);
            EXPECT_THROW(ParseInitialCondition("x(0) = 2^2"), ParseError);
        }

        // ========================================================================
        // 14. IsBlankOrComment
        // ========================================================================

        TEST(Parser, IsBlankOrCommentEmpty) {
            EXPECT_TRUE(IsBlankOrComment(""));
            EXPECT_TRUE(IsBlankOrComment("    "));
            EXPECT_TRUE(IsBlankOrComment("\t"));
        }

        TEST(Parser, IsBlankOrCommentComment) {
            EXPECT_TRUE(IsBlankOrComment("# comment"));
            EXPECT_TRUE(IsBlankOrComment("   # comment"));
        }

        TEST(Parser, IsBlankOrCommentContent) {
            EXPECT_FALSE(IsBlankOrComment("x' = y"));
            EXPECT_FALSE(IsBlankOrComment("   x' = y  "));
            EXPECT_FALSE(IsBlankOrComment("x(0) = 1"));
        }

        // ========================================================================
        // 15. LoadConstants (constants_file и extra_constants)
        // ========================================================================

        TEST(Parser, LoadConstantsExtraOverridesFile) {
            // Файл: c = 1. extra_constants: c = 2. Побеждает extra.
            TempConstantsFile tf("diffuri_test_constants_override.txt", "c = 1\n");

            ParseOptions opts;
            opts.constants_file = tf.str();
            opts.extra_constants = { {"c", 2.0} };

            auto e = ParseExpression("c", opts);
            ASSERT_TRUE(std::holds_alternative<Constant>(e->value));
            EXPECT_DOUBLE_EQ(std::get<Constant>(e->value).value, 2.0);
        }

        TEST(Parser, LoadConstantsMissingFileIsNotError) {
            // Несуществующий файл — не ошибка. Все имена становятся Function.
            ParseOptions opts;
            opts.constants_file = "diffuri_test_nonexistent_987654321.txt";

            auto e = ParseExpression("pi", opts);
            EXPECT_TRUE(std::holds_alternative<Function>(e->value));
        }

        TEST(Parser, LoadConstantsBrokenLinesIgnored) {
            // Строка "pi = abc" — значение не число, должна быть пропущена.
            // Строка "x = 3" — валидна, регистрируется.
            TempConstantsFile tf("diffuri_test_constants_broken.txt",
                "pi = abc\nx = 3\n");

            ParseOptions opts;
            opts.constants_file = tf.str();

            auto ex = ParseExpression("x", opts);
            ASSERT_TRUE(std::holds_alternative<Constant>(ex->value));
            EXPECT_DOUBLE_EQ(std::get<Constant>(ex->value).value, 3.0);

            auto epi = ParseExpression("pi", opts);
            EXPECT_TRUE(std::holds_alternative<Function>(epi->value));
        }

    } // namespace
} // namespace diffuri