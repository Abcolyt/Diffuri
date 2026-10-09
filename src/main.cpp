// ============================================================================
// src/main.cpp
//
// CLI-демонстратор пайплайна Diffuri.
//
// Это точка входа (исполняемый файл), не модуль библиотеки.
//
// Зависимости:
//   main -> cli         (ParseCliArgs, PrintUsage, CliOptions, SaveTrajectory)
//   main -> pipeline    (RunPipeline, RunResult, PipelineTrace, Stage, ToString)
//   main -> output      (FormatSolved, WriteReportToFile, ReportOptions)
//   main -> solver      (Solution)
//   main -> (все модули исключений для dynamic_cast)
//
// Отвечает за:
//   - разбор аргументов командной строки;
//   - чтение системы ОДУ из stdin (до EOF);
//   - прогон через пайплайн (Parse → Validate → Normalize → ReduceOrder →
//     Autonomize → Polynomize → Quadratize → Solve);
//   - печать состояния системы на каждой стадии;
//   - опциональную запись отчёта в файл и траектории в CSV.
//
// Что файл НЕ делает:
//   - не реализует этапы пайплайна (это отдельные модули);
//   - не форматирует отчёты (это output);
//   - не тестируется через unit-тесты (тестируется через subprocess в
//     интеграционных тестах).
//
// Пример запуска:
//   echo "x'' = -x
//   x(0) = 1
//   x'(0) = 0" | Diffuri
//
// С параметрами:
//   Diffuri 6.28 20 1e-3 diffuri.log
//   Diffuri --trajectory out.csv 6.28 20 1e-3 diffuri.log
//   argv[1]=t_end   argv[2]=M   argv[3]=h_init   argv[4]=log path
//   --trajectory PATH — где угодно среди аргументов
//
// Структура файла:
//   1. Анонимный namespace: внутренние хелперы (PrintStage, PrintResult,
//      WhichStage, ReadAllStdin).
//   2. Функция main().
// ============================================================================

// --- Стандартная библиотека (по алфавиту) ---
#include <exception>
#include <iostream>
#include <sstream>
#include <string>

// --- Внутренние зависимости (по алфавиту) ---
#include "autonomize/autonomize.h"
#include "cli/cli.h"
#include "input/input.h"
#include "input/parser.h"
#include "normalize/normalize.h"
#include "order_reducer/order_reducer.h"
#include "output/output.h"
#include "pipeline/runner.h"
#include "pipeline/trace.h"
#include "polynomization/polynomization.h"
#include "quadratize/quadratize.h"
#include "solver/solver.h"

// ============================================================================
// 1. АНОНИМНЫЙ NAMESPACE: внутренние хелперы
// ============================================================================
namespace {

    // Печать состояния на стадии: полный вид (Format) + чистый вид (View).
    // Чистый вид печатаем только там, где он отличается от полного —
    // то есть начиная с OrderReduced, где появляются вспомогательные.
    //
    // Для стадии Solved вместо Format(trace) печатаем FormatSolved:
    // это метаданные траектории (steps, order, t_final, финальная точка),
    // а не снимок системы.
    void PrintStage(const diffuri::PipelineTrace& trace,
        const diffuri::Solution& solution,
        diffuri::Stage stage) {
        std::cout << "========================================================\n";

        if (stage == diffuri::Stage::Solved) {
            std::cout << diffuri::FormatSolved(solution, trace);
        }
        else {
            std::cout << trace.Format(stage);
            if (!trace.Format(stage).empty() &&
                trace.Format(stage).back() != '\n') {
                std::cout << "\n";
            }
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

    // Печать результата RunPipeline: список стадий и каждая стадия отдельно.
    void PrintResult(const diffuri::RunResult& r) {
        std::cout << "--- pipeline stages ---\n";
        for (auto s : r.trace.Stages()) {
            std::cout << "  * " << diffuri::ToString(s) << "\n";
        }
        std::cout << "\n";

        for (auto s : r.trace.Stages()) {
            PrintStage(r.trace, r.solution, s);
        }
    }

    // Классификация исключения по этапу пайплайна.
    const char* WhichStage(const std::exception& e) {
        if (dynamic_cast<const diffuri::ParseError*>(&e))        return "Parse";
        if (dynamic_cast<const diffuri::InputError*>(&e))        return "Validate";
        if (dynamic_cast<const diffuri::NormalizeError*>(&e))    return "Normalize";
        if (dynamic_cast<const diffuri::OrderReducerError*>(&e)) return "ReduceOrder";
        if (dynamic_cast<const diffuri::AutonomizeError*>(&e))   return "Autonomize";
        if (dynamic_cast<const diffuri::PolynomizeError*>(&e))   return "Polynomize";
        if (dynamic_cast<const diffuri::QuadratizeError*>(&e))   return "Quadratize";
        if (dynamic_cast<const diffuri::SolverError*>(&e))       return "Solve";
        return "Unknown";
    }

    std::string ReadAllStdin() {
        std::ostringstream oss;
        oss << std::cin.rdbuf();
        return oss.str();
    }

} // namespace

#ifdef _WIN32
#include <windows.h>
#endif

// ============================================================================
// 2. ФУНКЦИЯ main()
// ============================================================================

int main(int argc, char** argv) {
    // --- 1. Разбор аргументов ---
    const auto parsed = diffuri::ParseCliArgs(argc, argv);

    if (parsed.status == diffuri::CliParseStatus::Help) {
        diffuri::PrintUsage(std::cout, argv[0]);
        return 0;
    }
    if (parsed.status == diffuri::CliParseStatus::Error) {
        std::cerr << parsed.error << "\n";
        return 1;
    }

    const diffuri::CliOptions& cli = parsed.options;

    // --- 2. Один прогон пайплайна: ввод до EOF ---
    std::cout << "Diffuri pipeline demo\n";
    std::cout << "Options: t_end=" << cli.solve.t_end
        << "  M=" << cli.solve.M
        << "  h_init=" << cli.solve.h_init;
    if (!cli.log_path.empty())
        std::cout << "  log=" << cli.log_path;
    if (!cli.trajectory_path.empty())
        std::cout << "  trajectory=" << cli.trajectory_path;
    std::cout << "\n";
    std::cout << "Enter ODE system (end with Ctrl+Z then Enter on Windows, "
        << "or Ctrl+D on Unix):\n\n";

    const std::string text = ReadAllStdin();
    if (text.empty()) {
        std::cerr << "input is empty\n";
        return 0;
    }

    std::cout << "--- input ---\n" << text << "\n";

    try {
        auto r = diffuri::RunPipeline(text, cli.solve);
        PrintResult(r);

        // --- 3. Траектория в CSV ---
        // Только исходные функции системы, в порядке ввода.
        // Stage::Parsed — снимок до того, как OrderReducer/Polynomize/
        // Quadratize добавили вспомогательные переменные.
        if (!cli.trajectory_path.empty()) {
            try {
                auto view = r.trace.View(diffuri::Stage::Parsed);
                diffuri::SaveTrajectory(r.solution, view.functions,
                    cli.trajectory_path);
                std::cout << "[trajectory saved to "
                    << cli.trajectory_path << "]\n";
            }
            catch (const std::exception& te) {
                std::cerr << "trajectory write failed: "
                    << te.what() << "\n";
            }
        }

        // --- 4. Полный отчёт в файл ---
        if (!cli.log_path.empty()) {
            try {
                diffuri::ReportOptions ropts;
                ropts.include_clean_views = true;
                diffuri::WriteReportToFile(r, cli.log_path, ropts);
                std::cout << "[log saved to " << cli.log_path << "]\n";
            }
            catch (const std::exception& le) {
                std::cerr << "log write failed: " << le.what() << "\n";
            }
        }

        std::cout << "OK\n";
    }
    catch (const std::exception& e) {
        std::cerr << "\n"
            << "========================================================\n"
            << "ERROR at stage: " << WhichStage(e) << "\n"
            << "  " << e.what() << "\n";
    }

    return 0;
}