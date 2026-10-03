// ============================================================================
// src/main.cpp
//
// CLI-демонстратор пайплайна Diffuri.
//
// Читает систему ОДУ из stdin (до EOF), прогоняет через пайплайн
// (Parse → Validate → Normalize → OrderReducer), печатает состояние
// системы на каждой стадии: полный вид со вспомогательными переменными
// и чистый вид без них.
//
// Ошибки этапов не заворачиваются: если пайплайн упал, печатается
// имя этапа и текст исключения.
//
// Пример запуска:
//   echo "x'' = -x
//   x(0) = 1
//   x'(0) = 0" | Diffuri
// ============================================================================
#include <exception>
#include <iostream>
#include <sstream>
#include <string>

#include "input/input.h"
#include "input/parser.h"
#include "normalize/normalize.h"
#include "order_reducer/order_reducer.h"
#include "pipeline/runner.h"
#include "pipeline/trace.h"

namespace {

    // ------------------------------------------------------------------------
    // Печать состояния на стадии: полный вид (Format) + чистый вид (View).
    // Чистый вид печатаем только там, где он отличается от полного —
    // то есть начиная с OrderReduced, где появляются вспомогательные.
    // ------------------------------------------------------------------------
    void PrintStage(const diffuri::PipelineTrace& trace,
        diffuri::Stage stage) {
        std::cout << "========================================================\n";
        std::cout << trace.Format(stage);
        if (!trace.Format(stage).empty() &&
            trace.Format(stage).back() != '\n') {
            std::cout << "\n";
        }

        // Для стадий, где есть скрытые вспомогательные переменные,
        // дополнительно показываем «чистый» вид.
        if (stage == diffuri::Stage::OrderReduced
            || stage == diffuri::Stage::Polynomized
            || stage == diffuri::Stage::Solved) {
            try {
                auto view = trace.View(stage);
                std::cout << "--------------------------------------------------------\n";
                std::cout << "[" << diffuri::ToString(stage) << ", clean]\n";
                std::cout << diffuri::ToString(view);
                if (!diffuri::ToString(view).empty() &&
                    diffuri::ToString(view).back() != '\n') {
                    std::cout << "\n";
                }
            }
            catch (const std::logic_error& e) {
                std::cout << "clean view unavailable: " << e.what() << "\n";
            }
        }
        std::cout << "\n";
    }

    // ------------------------------------------------------------------------
    // Печать результата RunPipeline: список стадий и каждая стадия отдельно.
    // ------------------------------------------------------------------------
    void PrintResult(const diffuri::RunResult& r) {
        std::cout << "--- pipeline stages ---\n";
        for (auto s : r.trace.Stages()) {
            std::cout << "  * " << diffuri::ToString(s) << "\n";
        }
        std::cout << "\n";

        for (auto s : r.trace.Stages()) {
            PrintStage(r.trace, s);
        }
    }

    // ------------------------------------------------------------------------
    // Классификация исключения по этапу пайплайна.
    // ------------------------------------------------------------------------
    const char* WhichStage(const std::exception& e) {
        if (dynamic_cast<const diffuri::ParseError*>(&e))       return "Parse";
        if (dynamic_cast<const diffuri::InputError*>(&e))       return "Validate";
        if (dynamic_cast<const diffuri::NormalizeError*>(&e))   return "Normalize";
        if (dynamic_cast<const diffuri::OrderReducerError*>(&e))return "OrderReducer";
        return "Unknown";
    }

    std::string ReadAllStdin() {
        std::ostringstream oss;
        oss << std::cin.rdbuf();
        return oss.str();
    }

} // namespace

#include <windows.h>

int main() {
    //// Устанавливаем кодовую страницу вывода в UTF-8
    //SetConsoleOutputCP(65001);
    //// Также рекомендуется установить кодовую страницу ввода
    //SetConsoleCP(65001);

    while (true)
    {

        std::cout << "Diffuri pipeline demo\n";
        std::cout << "Enter ODE system (end with Ctrl+Z then Enter on Windows, "
            "or Ctrl+D on Unix):\n\n";

        const std::string text = ReadAllStdin();
        if (text.empty()) {
            std::cerr << "input is empty\n";
            return 1;
        }

        std::cout << "--- input ---\n" << text << "\n";

        try {
            auto r = diffuri::RunPipeline(text);
            PrintResult(r);
            std::cout << "OK\n";
        }
        catch (const std::exception& e) {
            std::cerr << "\n"
                << "========================================================\n"
                << "ERROR at stage: " << WhichStage(e) << "\n"
                << "  " << e.what() << "\n";
        }
    }
    return 0;
}