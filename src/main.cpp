// ============================================================================
// src/main.cpp
//
// CLI-демонстратор пайплайна Diffuri.
//
// Читает систему ОДУ из stdin (до EOF), прогоняет через пайплайн
// (Parse → Validate → Normalize → ReduceOrder → Autonomize →
//  Polynomize → Quadratize → Solve), печатает состояние системы
// на каждой стадии: полный вид со вспомогательными переменными
// и чистый вид без них. Для стадии Solved дополнительно печатает
// результат интегрирования: число шагов, порядок, t_final и значения
// функций в финальной точке.
//
// Опционально сохраняет:
//   - полный отчёт в файл (позиционный аргумент log_path);
//   - траекторию в CSV (--trajectory PATH).
//
// Режимы работы:
//   - batch (по умолчанию): argv + stdin (пайп или файл). Текущее поведение.
//   - interactive TUI: argc == 1 и stdin — терминал. Перед запуском
//     показывается экран настроек (PromptSettings), в конце — пауза
//     «Нажмите Enter...», чтобы окно консоли не закрылось мгновенно.
//     Выбор режима — ShouldUseInteractiveMode(argc).
//
// Ошибки этапов не заворачиваются: если пайплайн упал, печатается
// имя этапа и текст исключения.
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
    // а не снимок системы. Полный вид системы после Solved не нужен —
    // он содержит все вспомогательные переменные и не несёт полезной
    // информации для пользователя.
    void PrintStage(const diffuri::PipelineTrace& trace,
        const diffuri::Solution& solution,
        diffuri::Stage stage) {
        std::cout << "========================================================\n";

        if (stage == diffuri::Stage::Solved) {
            // Для Solved печатаем только метаданные траектории.
            // НЕ печатаем trace.Format(stage) — это полный вид системы
            // после квадратизации со всеми вспомогательными переменными.
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
        // Для Solved чистый вид не нужен — метаданные уже напечатаны.
        if (stage == diffuri::Stage::OrderReduced
            || stage == diffuri::Stage::Polynomized
            || stage == diffuri::Stage::Quadratized) {
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

// ============================================================================
// 2. ФУНКЦИЯ main()
// ============================================================================

int main(int argc, char** argv) {
    // --- 1. Разбор аргументов ---
    const auto parsed = diffuri::ParseCliArgs(argc, argv);

    // TUI включается только на «двойной клик»: никаких аргументов + stdin TTY.
    // Если запускают через пайп или передают аргументы — это batch-режим.
    const bool interactive = diffuri::ShouldUseInteractiveMode(argc);

    // Единая точка выхода: в interactive-режиме всегда ждём Enter,
    // чтобы окно консоли не закрылось мгновенно.
    auto finish = [interactive](int code) {
        if (interactive) {
            diffuri::PressEnterToExit(std::cin, std::cout);
        }
        return code;
        };

    if (parsed.status == diffuri::CliParseStatus::Help) {
        diffuri::PrintUsage(std::cout, argv[0]);
        return finish(0);
    }
    if (parsed.status == diffuri::CliParseStatus::Error) {
        std::cerr << parsed.error << "\n";
        return finish(1);
    }

    // --- 2. TUI-экран настроек (только в interactive-режиме) ---
    diffuri::CliOptions cli = parsed.options;
    if (interactive) {
        diffuri::PromptSettings(cli, std::cin, std::cout);
    }

    // --- 3. Один прогон пайплайна: ввод до EOF ---
    std::cout << "\nDiffuri pipeline demo\n";
    std::cout << "Options: t_end=" << cli.solve.t_end
        << "  M=" << cli.solve.M
        << "  h_init=" << cli.solve.h_init
        << "  rtol=" << cli.solve.rtol
        << "  atol=" << cli.solve.atol;
    if (cli.solve.enable_order_adaptation) {
        std::cout << "  order_adapt=on";
    }
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
        return finish(0);
    }

    std::cout << "--- input ---\n" << text << "\n";

    try {
        auto r = diffuri::RunPipeline(text, cli.solve);
        PrintResult(r);

        // --- 4. Траектория в CSV ---
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

        // --- 5. Полный отчёт в файл ---
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

    return finish(0);
}