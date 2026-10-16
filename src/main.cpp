// ============================================================================
// src/main.cpp
//
// CLI-демонстратор пайплайна Diffuri.
//
// Читает систему ОДУ из stdin (до EOF), из файла (--input PATH или
// input=PATH в TUI), либо из группы файлов по маске (--input "dir/*.txt").
// Прогоняет каждую систему через пайплайн (Parse → Validate → Normalize
// → ReduceOrder → Autonomize → Polynomize → Quadratize → Solve) и печатает
// состояние на каждой стадии.
//
// Опционально сохраняет:
//   - полный отчёт в файл (позиционный log_path или --set log=...);
//   - траекторию в CSV (--trajectory PATH);
//   - grid-сводку (--grid "...", по умолчанию grid_summary.csv).
//
// Режимы работы:
//   - batch (по умолчанию): argv + stdin (пайп или файл);
//   - interactive TUI: argc == 1 и stdin — терминал. Значения по умолчанию
//     для TUI читаются из data/settings.txt (LoadSettings);
//   - grid: задан --grid "...". Для каждой комбинации параметров пайплайн
//     прогоняется отдельно. Сводка всегда пишется одной таблицей;
//     траектории (если задан --trajectory) — по файлу на комбинацию.
//
// Файл настроек НЕ читается в batch-режиме: приоритет у argv.
//
// Ошибки этапов не заворачиваются: печатается имя этапа и текст
// исключения. В grid-режиме падение одной комбинации не прерывает
// обработку остальных.
//
// Пример запуска:
//   echo "x'' = -x
//   x(0) = 1
//   x'(0) = 0" | Diffuri
//
//   Diffuri --input data/example_input/lorenz.txt --trajectory out.csv 5.0
//   Diffuri --input "data/example_input/*.txt" --trajectory out.csv 5.0
//   Diffuri --input data/example_input/lorenz.txt ^
//            --grid "t_end=1,2,3 atol=1e-11,1e-12,1e-13,1e-14"
//   Diffuri --input data/example_input/lorenz.txt ^
//            --grid "t_end=1,2,3 atol=1e-11,1e-12,1e-13,1e-14" ^
//            --trajectory out.csv
//
// Структура файла:
//   1. Анонимный namespace: внутренние хелперы.
//   2. Функция main().
// ============================================================================

// --- Стандартная библиотека ---
#include <cstddef>
#include <set>
#include <exception>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

// --- Внутренние зависимости (по алфавиту) ---
#include "autonomize/autonomize.h"
#include "benchmark/benchmark.h"
#include "cli/cli.h"
#include "cli/glob.h"
#include "cli/grid.h"
#include "cli/settings.h"
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

    // ------------------------------------------------------------------------
    // Печать стадий пайплайна (как в обычном режиме)
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

    // ------------------------------------------------------------------------
    // Чтение входных данных
    // ------------------------------------------------------------------------

    std::string ReadAllStdin() {
        std::ostringstream oss;
        oss << std::cin.rdbuf();
        return oss.str();
    }

    bool ReadWholeFile(const std::string& path, std::string& out) {
        std::ifstream f(path);
        if (!f) return false;
        std::ostringstream oss;
        oss << f.rdbuf();
        out = oss.str();
        return true;
    }

    bool ReadFromStdin(const std::string& input_path) {
        return input_path.empty() || input_path == "-";
    }

    // ------------------------------------------------------------------------
    // Источники входных систем: (label, text). label == "stdin" для stdin,
    // иначе путь к файлу.
    // ------------------------------------------------------------------------

    struct InputSource {
        std::string label;
        std::string text;
    };

    std::vector<InputSource> CollectSources(const diffuri::CliOptions& cli) {
        std::vector<InputSource> sources;

        if (ReadFromStdin(cli.input_path)) {
            std::cout << "Enter ODE system (end with Ctrl+Z then Enter on "
                "Windows, or Ctrl+D on Unix):\n\n";
            InputSource s;
            s.label = "stdin";
            s.text = ReadAllStdin();
            sources.push_back(std::move(s));
            return sources;
        }

        if (diffuri::HasGlobPattern(cli.input_path)) {
            const auto files = diffuri::ExpandGlob(cli.input_path);
            if (files.empty()) {
                std::cerr << "no files match pattern '"
                    << cli.input_path << "'\n";
                return {};
            }
            std::cout << "Matched " << files.size() << " file(s):\n";
            for (const auto& f : files) {
                std::cout << "  " << f << "\n";
            }
            std::cout << "\n";

            for (const auto& f : files) {
                std::string text;
                if (!ReadWholeFile(f, text)) {
                    std::cerr << "cannot open input file '" << f
                        << "' — skipped\n";
                    continue;
                }
                InputSource s;
                s.label = f;
                s.text = std::move(text);
                sources.push_back(std::move(s));
            }
            return sources;
        }

        std::string text;
        if (!ReadWholeFile(cli.input_path, text)) {
            std::cerr << "cannot open input file '" << cli.input_path << "'\n";
            return {};
        }
        InputSource s;
        s.label = cli.input_path;
        s.text = std::move(text);
        sources.push_back(std::move(s));
        return sources;
    }

    // ------------------------------------------------------------------------
    // Обычный режим (не grid): прогон одной системы с печатью стадий.
    // ------------------------------------------------------------------------

    bool RunOneSystem(const std::string& text,
        const std::string& source_label,
        const diffuri::CliOptions& cli,
        const std::string& trajectory_path,
        const std::string& log_path) {
        std::cout << "--- input (" << source_label << ") ---\n";
        std::cout << text << "\n";

        if (text.empty()) {
            std::cerr << "input is empty: " << source_label << "\n";
            return false;
        }

        try {
            auto r = diffuri::RunPipeline(text, cli.solve);
            PrintResult(r);

            if (!trajectory_path.empty()) {
                try {
                    auto view = r.trace.View(diffuri::Stage::Parsed);
                    diffuri::SaveTrajectory(r.solution, view.functions,
                        trajectory_path);
                    std::cout << "[trajectory saved to "
                        << trajectory_path << "]\n";
                }
                catch (const std::exception& te) {
                    std::cerr << "trajectory write failed: "
                        << te.what() << "\n";
                }
            }

            if (!log_path.empty()) {
                try {
                    diffuri::ReportOptions ropts;
                    ropts.include_clean_views = true;
                    diffuri::WriteReportToFile(r, log_path, ropts);
                    std::cout << "[log saved to " << log_path << "]\n";
                }
                catch (const std::exception& le) {
                    std::cerr << "log write failed: " << le.what() << "\n";
                }
            }

            std::cout << "OK\n";
            return true;
        }
        catch (const std::exception& e) {
            std::cerr << "\n"
                << "========================================================\n"
                << "ERROR at stage: " << WhichStage(e) << "\n"
                << "  " << e.what() << "\n";
            return false;
        }
    }

    // ------------------------------------------------------------------------
    // Grid-режим: прогон одной комбинации
    // ------------------------------------------------------------------------

    struct GridRunResult {
        bool                     ok = false;
        std::size_t              steps = 0;
        std::size_t              order_used = 0;
        double                   t_final = 0.0;
        std::vector<std::string> function_names;
        std::vector<double>      function_values;
    };

    GridRunResult RunGridOne(const diffuri::CliOptions& cli,
        const std::string& text,
        const std::string& trajectory_path,
        const std::string& log_path) {
        GridRunResult result;
        try {
            auto r = diffuri::RunPipeline(text, cli.solve);

            if (!trajectory_path.empty()) {
                try {
                    auto view = r.trace.View(diffuri::Stage::Parsed);
                    diffuri::SaveTrajectory(r.solution, view.functions,
                        trajectory_path);
                    std::cout << "[traj:" << trajectory_path << "] ";
                }
                catch (const std::exception& te) {
                    std::cerr << "[traj-error:" << te.what() << "] ";
                }
            }

            if (!log_path.empty()) {
                try {
                    diffuri::ReportOptions ropts;
                    ropts.include_clean_views = true;
                    diffuri::WriteReportToFile(r, log_path, ropts);
                }
                catch (const std::exception& le) {
                    std::cerr << "[log-error:" << le.what() << "] ";
                }
            }

            const auto view = diffuri::MakeSolvedView(r.solution, r.trace);
            result.steps = view.steps;
            result.order_used = view.order_used;
            result.t_final = view.t_final;
            result.function_names = view.names;
            result.function_values = view.values;
            result.ok = true;
        }
        catch (const std::exception& e) {
            std::cerr << "[" << WhichStage(e) << ": " << e.what() << "] ";
        }
        return result;
    }

} // namespace

// ============================================================================
// 2. ФУНКЦИЯ main()
// ============================================================================

int main(int argc, char** argv) {
#ifdef _WIN32
    // Windows-консоль по умолчанию в CP866/CP1251, а весь текст проекта
    // (включая вывод Format и русские комментарии в data/*.txt) — UTF-8.
    // Без этих вызовов кириллица в stdout превращается в mojibake.
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    // --- 1. Разбор аргументов ---
    const auto parsed = diffuri::ParseCliArgs(argc, argv);
    const bool interactive = diffuri::ShouldUseInteractiveMode(argc);

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
        const std::string settings_path = diffuri::DefaultSettingsPath();
        const auto load = diffuri::LoadSettings(settings_path, cli);
        if (load.created_default) {
            std::cout << "[settings] создан файл по умолчанию: "
                << load.path << "\n";
        }
        if (!load.warning.empty()) {
            std::cerr << "[settings] предупреждения:\n" << load.warning;
        }
        diffuri::PromptSettings(cli, std::cin, std::cout);
    }

    // --- 3. Баннер и опции ---
    const bool read_from_stdin = ReadFromStdin(cli.input_path);
    const bool grid_mode = !cli.grid_spec.empty();

    std::cout << "\nDiffuri pipeline demo";
    if (grid_mode) std::cout << " (grid mode)";
    std::cout << "\n";

    std::set<std::string> overridden;
    if (grid_mode) {
        try {
            const auto spec = diffuri::ParseGridSpec(cli.grid_spec);
            for (const auto& p : spec.params) overridden.insert(p.name);
        }
        catch (const std::exception&) {
        }
    }
    auto keep = [&](const char* key) {
        return overridden.find(key) == overridden.end();
        };

    std::cout << (grid_mode ? "Base options: " : "Options: ");
    bool first_opt = true;
    auto sep = [&]() {
        if (!first_opt) std::cout << "  ";
        first_opt = false;
        };

    if (keep("t_end")) { sep(); std::cout << "t_end=" << cli.solve.t_end; }
    if (keep("M")) { sep(); std::cout << "M=" << cli.solve.M; }
    if (keep("h_init")) { sep(); std::cout << "h_init=" << cli.solve.h_init; }
    if (keep("rtol")) { sep(); std::cout << "rtol=" << cli.solve.rtol; }
    if (keep("atol")) { sep(); std::cout << "atol=" << cli.solve.atol; }
    if (cli.solve.enable_order_adaptation && keep("enable_order_adaptation")) {
        sep(); std::cout << "order_adapt=on";
    }
    if (!cli.log_path.empty()) { sep(); std::cout << "log=" << cli.log_path; }
    if (!cli.trajectory_path.empty()) {
        sep(); std::cout << "trajectory=" << cli.trajectory_path;
    }
    sep();
    std::cout << "input="
        << (read_from_stdin ? std::string("stdin") : cli.input_path);
    std::cout << "\n";

    if (grid_mode) {
        std::cout << "Grid: " << cli.grid_spec << "\n";
        std::cout << "Grid overrides: ";
        if (overridden.empty()) {
            std::cout << "(none)";
        }
        else {
            bool first_over = true;
            for (const auto& k : overridden) {
                if (!first_over) std::cout << ", ";
                std::cout << k;
                first_over = false;
            }
        }
        std::cout << "\n";
    }   

    // --- 4. Сбор входных систем ---
    const auto sources = CollectSources(cli);
    if (sources.empty()) {
        std::cerr << "no input sources\n";
        return finish(1);
    }

    // --- 5. Grid-режим ---
    if (grid_mode) {
        // Разбор спецификации.
        diffuri::GridSpec spec;
        std::vector<diffuri::GridCombination> combos;
        try {
            spec = diffuri::ParseGridSpec(cli.grid_spec);
            combos = diffuri::EnumerateGrid(spec);
        }
        catch (const diffuri::GridError& e) {
            std::cerr << "grid spec error: " << e.what() << "\n";
            return finish(1);
        }
        if (combos.empty()) {
            std::cerr << "grid: no combinations to run\n";
            return finish(1);
        }

        // Путь к сводному файлу.
        const std::string summary_path = cli.summary_path.empty()
            ? diffuri::DefaultGridSummaryPath()
            : cli.summary_path;

        std::ofstream summary(summary_path);
        if (!summary) {
            std::cerr << "cannot open summary file '" << summary_path
                << "' for writing\n";
            return finish(1);
        }

        // Опциональный manifest: пишется, если пользователь задал
        // --trajectory (split-режим) — чтобы сопоставить имена файлов
        // с параметрами.
        const bool want_traj = !cli.trajectory_path.empty();

        const std::size_t total_runs = sources.size() * combos.size();
        std::size_t run_index = 0;
        std::size_t ok_count = 0;
        std::size_t fail_count = 0;
        bool header_written = false;
        std::vector<std::string> header_function_names;

        std::cout << "\nGrid: " << combos.size() << " combination(s) x "
            << sources.size() << " source(s) = " << total_runs
            << " run(s)\n\n";

        for (const auto& source : sources) {
            for (std::size_t k = 0; k < combos.size(); ++k) {
                ++run_index;
                const auto& combo = combos[k];

                std::cout << "[grid " << run_index << "/" << total_runs
                    << "] ";
                for (const auto& cell : combo.cells) {
                    std::cout << cell.name << "=" << cell.value << " ";
                }
                std::cout << "... " << std::flush;

                diffuri::CliOptions cli_copy = cli;
                try {
                    diffuri::ApplyGridCombination(cli_copy, combo);
                }
                catch (const diffuri::GridError& e) {
                    std::cout << "FAILED (apply: " << e.what() << ")\n";
                    ++fail_count;
                    continue;
                }

                // Путь к траектории для этой комбинации.
                std::string traj_path;
                if (want_traj) {
                    const std::string input_stem =
                        (source.label == "stdin") ? "" : source.label;
                    traj_path = diffuri::MakeGridOutputPath(
                        cli.trajectory_path, input_stem, combo);
                }

                // Путь к логу: та же схема, если задан log.
                std::string log_path;
                if (!cli.log_path.empty()) {
                    const std::string input_stem =
                        (source.label == "stdin") ? "" : source.label;
                    log_path = diffuri::MakeGridOutputPath(
                        cli.log_path, input_stem, combo);
                }

                const auto run = RunGridOne(
                    cli_copy, source.text, traj_path, log_path);

                if (!run.ok) {
                    std::cout << "FAILED\n";
                    ++fail_count;
                    continue;
                }

                ++ok_count;
                std::cout << "ok (steps=" << run.steps
                    << ", t_final=" << run.t_final << ")\n";

                // Заголовок сводки — один раз, по первому успешному
                // прогону.
                if (!header_written) {
                    header_function_names = run.function_names;
                    summary << diffuri::FormatGridSummaryHeader(
                        spec, run.function_names) << "\n";
                    header_written = true;
                }
                else if (run.function_names != header_function_names) {
                    std::cerr
                        << "[grid warning] function set differs from the "
                        << "header; row may be misaligned (source='"
                        << source.label << "', index=" << k << ")\n";
                }

                diffuri::GridSummaryMeta meta;
                meta.source = source.label;
                meta.index = k;
                meta.steps = run.steps;
                meta.order_used = run.order_used;
                meta.t_final = run.t_final;

                summary << diffuri::FormatGridSummaryRow(
                    combo, meta, run.function_values) << "\n";
            }
        }

        summary.close();

        std::cout << "\n=== Grid summary: " << ok_count << " ok, "
            << fail_count << " failed (of " << total_runs << ") ===\n";
        std::cout << "[summary saved to " << summary_path << "]\n";

        if (want_traj) {
            std::cout << "[trajectory files: "
                << cli.trajectory_path << "-based names with '"
                << "_<value>..." << "' suffix]\n";
        }

        return finish(fail_count == 0 ? 0 : 1);
    }
    // --- 5b. Sweep-режим: варьирование одного параметра ---
    //
    // Запускается, только если --grid не задан: grid-блок выше уже
    // вернул управление. Sweep строит эталонное решение
    // (M=60, rtol=1e-14, atol=1e-16), затем для каждого значения
    // из spec.values прогоняет систему и сравнивает с эталоном.
    // Результат — CSV, который читает tools/plot_sweep.py.
    if (!cli.sweep_spec.empty()) {
        diffuri::SweepSpec spec;
        diffuri::SweepMetric metric;
        try {
            spec = diffuri::ParseSweepSpec(cli.sweep_spec);
            metric = diffuri::ParseSweepMetric(cli.sweep_metric);
        }
        catch (const std::exception& e) {
            std::cerr << "sweep spec error: " << e.what() << "\n";
            return finish(1);
        }

        std::cout << "\n=== Sweep mode ===\n";
        std::cout << "  variable: " << cli.sweep_spec << "\n";
        std::cout << "  metric:   " << cli.sweep_metric << "\n";
        std::cout << "  output:   " << cli.sweep_out_path << "\n";
        std::cout << "  values:   ";
        for (double v : spec.values) std::cout << v << " ";
        std::cout << "\n\n";

        std::size_t total_runs = 0;
        std::size_t total_ok = 0;

        for (const auto& source : sources) {
            std::cout << "Running sweep on: " << source.label << "\n";

            const auto result = diffuri::RunSweep(
                source.text, cli.solve, spec, metric);

            for (const auto& pt : result.points) {
                ++total_runs;
                if (pt.ok) ++total_ok;
            }

            std::string out_path = cli.sweep_out_path;
            if (sources.size() > 1) {
                out_path = diffuri::MakeOutputPathWithSuffix(
                    cli.sweep_out_path, source.label);
            }

            try {
                diffuri::WriteSweepCsv(result, out_path);
                std::cout << "[sweep saved to " << out_path << "]\n";
            }
            catch (const std::exception& e) {
                std::cerr << "sweep write failed: " << e.what() << "\n";
            }
        }

        std::cout << "\n=== Sweep summary: " << total_ok << " ok of "
            << total_runs << " run(s) ===\n";
        return finish(0);
    }
    // --- 6. Обычный режим ---
    std::size_t ok_count = 0;
    std::size_t fail_count = 0;

    for (const auto& source : sources) {
        if (sources.size() > 1) {
            std::cout
                << "========================================================\n"
                << "### Input: " << source.label << "\n"
                << "========================================================\n";
        }

        // Путь к траектории: при одном источнике с glob путь получает
        // суффикс с именем файла (как раньше). При stdin суффикс не нужен.
        std::string traj_path = cli.trajectory_path;
        std::string log_path = cli.log_path;
        if (sources.size() > 1) {
            traj_path = diffuri::MakeOutputPathWithSuffix(
                cli.trajectory_path, source.label);
            log_path = diffuri::MakeOutputPathWithSuffix(
                cli.log_path, source.label);
        }

        if (RunOneSystem(source.text, source.label, cli,
            traj_path, log_path)) {
            ++ok_count;
        }
        else {
            ++fail_count;
        }
    }

    if (sources.size() > 1) {
        std::cout << "\n=== Summary: " << ok_count << " ok, "
            << fail_count << " failed (of " << sources.size()
            << " files) ===\n";
    }

    return finish(fail_count == 0 ? 0 : 1);
}