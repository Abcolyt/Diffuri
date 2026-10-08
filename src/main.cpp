// ============================================================================
// src/main.cpp
//
// CLI-демонстратор пайплайна Diffuri.
//
// Читает систему ОДУ из stdin (до EOF), прогоняет через пайплайн
// (Parse → Validate → Normalize → OrderReducer → Autonomize →
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
// ============================================================================
#include <exception>
#include <iostream>
#include <sstream>
#include <string>

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

namespace {

    // ------------------------------------------------------------------------
    // Печать состояния на стадии: полный вид (Format) + чистый вид (View).
    // Чистый вид печатаем только там, где он отличается от полного —
    // то есть начиная с OrderReduced, где появляются вспомогательные.
    //
    // Для стадии Solved вместо Format(trace) печатаем FormatSolved:
    // это метаданные траектории (steps, order, t_final, финальная точка),
    // а не снимок системы.
    // ------------------------------------------------------------------------
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
            PrintStage(r.trace, r.solution, s);
        }
    }

    // ------------------------------------------------------------------------
    // Классификация исключения по этапу пайплайна.
    // ------------------------------------------------------------------------
    const char* WhichStage(const std::exception& e) {
        if (dynamic_cast<const diffuri::ParseError*>(&e))        return "Parse";
        if (dynamic_cast<const diffuri::InputError*>(&e))        return "Validate";
        if (dynamic_cast<const diffuri::NormalizeError*>(&e))    return "Normalize";
        if (dynamic_cast<const diffuri::OrderReducerError*>(&e)) return "OrderReducer";
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

#include <windows.h>

int main(int argc, char** argv) {
    //// Устанавливаем кодовую страницу вывода в UTF-8
    //SetConsoleOutputCP(65001);
    //// Также рекомендуется установить кодовую страницу ввода
    //SetConsoleCP(65001);

    // ------------------------------------------------------------------------
    // 1. Разбор аргументов.
    // ------------------------------------------------------------------------
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

    // ------------------------------------------------------------------------
    // 2. Цикл обработки (в текущем виде — один ввод до EOF, см. комментарий
    //    в конце файла).
    // ------------------------------------------------------------------------
    while (true)
    {
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
            "or Ctrl+D on Unix):\n\n";

        const std::string text = ReadAllStdin();
        if (text.empty()) {
            std::cerr << "input is empty\n";
            return 0;
        }

        std::cout << "--- input ---\n" << text << "\n";

        try {
            auto r = diffuri::RunPipeline(text, cli.solve);
            PrintResult(r);

            // Траектория в CSV.
            if (!cli.trajectory_path.empty()) {
                try {
                    diffuri::SaveTrajectory(r.solution, cli.trajectory_path);
                    std::cout << "[trajectory saved to "
                        << cli.trajectory_path << "]\n";
                }
                catch (const std::exception& te) {
                    std::cerr << "trajectory write failed: "
                        << te.what() << "\n";
                }
            }

            // Полный отчёт в файл.
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
    }
    return 0;
}