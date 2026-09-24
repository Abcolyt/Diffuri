
// ============================================================================
// tests/debug_print_system_test.cpp
//
// Тесты для debug/print_system.h
//
// Каждый тест печатает в консоль:
//   1) входную строку системы (то, что подаётся в ParseSystem);
//   2) результат PrintSystem (то, что выдаёт отладочный модуль).
//
// Так визуально видно, что вход и выход согласованы, и легко заметить
// регрессию в форматировании.
// ============================================================================

#include <gtest/gtest.h>

#include <iostream>
#include <sstream>
#include <string>

#include "input/input.h"
#include "input/input_print_debug.h"

namespace diffuri {
    namespace {

        // ----------------------------------------------------------------------------
        // Вспомогательные функции
        // ----------------------------------------------------------------------------

        /// Единообразный вывод теста: заголовок, вход, результат PrintSystem.
        void ShowCase(const std::string& title,
            const std::string& input,
            const std::string& printed) {
            std::cout << "\n"
                << "=======================================================\n"
                << " TEST: " << title << "\n"
                << "=======================================================\n"
                << "[INPUT]\n"
                << input
                << "\n[DEBUG PRINT]\n"
                << printed
                << "-------------------------------------------------------\n";
        }

        /// Перехватывает PrintSystem(sys, os) в строку — чтобы и напечатать её
        /// в консоль, и использовать в EXPECT'ах.
        std::string CapturePrint(const RawSystem& sys) {
            std::ostringstream os;
            PrintSystem(sys, os);
            return os.str();
        }

        // ----------------------------------------------------------------------------
        // Тесты
        // ----------------------------------------------------------------------------

        TEST(DebugPrintSystem, SingleEquationWithIC) {
            const std::string input =
                "x' = -x\n"
                "x(0) = 1\n";

            RawSystem sys = ParseSystem(input);
            const std::string printed = CapturePrint(sys);

            ShowCase("SingleEquationWithIC", input, printed);

            EXPECT_FALSE(printed.empty());
            // Унарный минус в AST — это Binary{Sub, 0, X}, поэтому ToString
            // печатает "(0 - x)", а не "-x". Проверяем именно это.
            EXPECT_NE(printed.find("x' ="), std::string::npos);
            EXPECT_NE(printed.find("0 - x"), std::string::npos);
            EXPECT_NE(printed.find("x(0) = 1"), std::string::npos);
        }

        TEST(DebugPrintSystem, HarmonicOscillatorSystem) {
            const std::string input =
                "x' = -y\n"
                "y' = x\n"
                "x(0) = 1\n"
                "y(0) = 0\n";

            RawSystem sys = ParseSystem(input);
            const std::string printed = CapturePrint(sys);

            ShowCase("HarmonicOscillatorSystem", input, printed);

            EXPECT_FALSE(printed.empty());
            EXPECT_NE(printed.find("x'"), std::string::npos);
            EXPECT_NE(printed.find("y'"), std::string::npos);
            EXPECT_NE(printed.find("x(0)"), std::string::npos);
            EXPECT_NE(printed.find("y(0)"), std::string::npos);
        }

        TEST(DebugPrintSystem, SecondOrderEquation) {
            const std::string input =
                "x'' + x = 0\n"
                "x(0) = 1\n"
                "x'(0) = 0\n";

            RawSystem sys = ParseSystem(input);
            const std::string printed = CapturePrint(sys);

            ShowCase("SecondOrderEquation", input, printed);

            EXPECT_FALSE(printed.empty());
            EXPECT_NE(printed.find("x''"), std::string::npos);
            EXPECT_NE(printed.find("x(0)"), std::string::npos);
            EXPECT_NE(printed.find("x'(0)"), std::string::npos);
        }

        TEST(DebugPrintSystem, WithComments) {
            const std::string input =
                "# Oscillator system\n"
                "x'' + sin(x) = 0    # inline comment\n"
                "x(0) = 0\n"
                "x'(0) = 1\n";

            RawSystem sys = ParseSystem(input);
            const std::string printed = CapturePrint(sys);

            ShowCase("WithComments", input, printed);

            EXPECT_FALSE(printed.empty());
            // Комментарии должны быть отброшены и не попасть в вывод.
            EXPECT_EQ(printed.find('#'), std::string::npos);
            EXPECT_NE(printed.find("sin"), std::string::npos);
        }

        TEST(DebugPrintSystem, NonAutonomousWithT) {
            const std::string input =
                "x' = t * x\n"
                "x(0) = 1\n";

            RawSystem sys = ParseSystem(input);
            const std::string printed = CapturePrint(sys);

            ShowCase("NonAutonomousWithT", input, printed);

            EXPECT_FALSE(printed.empty());
            EXPECT_NE(printed.find("t"), std::string::npos);
        }

        TEST(DebugPrintSystem, ConstantsArePrinted) {
            const std::string input =
                "x' = pi * x\n"
                "x(0) = 1\n";

            ParseOptions opts;
            opts.extra_constants["pi"] = 3.14159265358979323846;

            RawSystem sys = ParseSystem(input, opts);
            const std::string printed = CapturePrint(sys);

            ShowCase("ConstantsArePrinted", input, printed);

            EXPECT_FALSE(printed.empty());
            EXPECT_NE(printed.find("pi"), std::string::npos);
        }

        TEST(DebugPrintSystem, StreamOperatorMatchesPrintSystem) {
            const std::string input =
                "x' = -x\n"
                "x(0) = 1\n";

            RawSystem sys = ParseSystem(input);

            std::ostringstream os;
            os << sys;
            const std::string via_op = os.str();
            const std::string via_fn = CapturePrint(sys);

            ShowCase("StreamOperator (via operator<<)", input, via_op);
            ShowCase("StreamOperator (via PrintSystem)", input, via_fn);

            EXPECT_EQ(via_op, via_fn);
        }

        TEST(DebugPrintSystem, PrintSystemToCout) {
            const std::string input =
                "x' = -x\n"
                "x(0) = 1\n";

            RawSystem sys = ParseSystem(input);

            testing::internal::CaptureStdout();
            PrintSystem(sys);
            const std::string captured = testing::internal::GetCapturedStdout();

            ShowCase("PrintSystemToCout", input, captured);

            EXPECT_FALSE(captured.empty());
            EXPECT_NE(captured.find("x'"), std::string::npos);
        }

        TEST(DebugPrintSystem, RoundTripStable) {
            const std::string input =
                "x'' + x = 0\n"
                "x(0) = 1\n"
                "x'(0) = 0\n";

            RawSystem sys1 = ParseSystem(input);
            const std::string printed1 = CapturePrint(sys1);

            RawSystem sys2 = ParseSystem(printed1);
            const std::string printed2 = CapturePrint(sys2);

            ShowCase("RoundTripStable (input -> print #1)", input, printed1);
            ShowCase("RoundTripStable (print #1 -> print #2)", printed1, printed2);

            EXPECT_EQ(printed1, printed2);
        }

    }  // namespace
}  // namespace diffuri