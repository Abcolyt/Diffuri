// ============================================================================
// src/cli/cli.cpp
//
// Реализация разбора аргументов командной строки Diffuri и утилит CLI-слоя.
//
// Структура файла:
//   1. Анонимный namespace: локальные утилиты (enum Pos).
//   2. Реализация исключений (отсутствуют).
//   3. Реализация публичных функций (ParseCliArgs, PrintUsage, SaveTrajectory).
// ============================================================================
#include "cli/cli.h"

// --- Стандартная библиотека (по алфавиту) ---
#include <algorithm>
#include <cstddef>
#include <fstream>
#include <iomanip>
#include <ostream>
#include <stdexcept>
#include <vector>

namespace diffuri {

    // ============================================================================
    // 1. АНОНИМНЫЙ NAMESPACE: локальные утилиты
    // ============================================================================
    namespace {
        // Позиция, которую сейчас ожидаем в позиционных аргументах.
        enum class Pos { TEnd, M, HInit, LogPath, Done };
    } // namespace

    // ============================================================================
    // 2. РЕАЛИЗАЦИЯ ИСКЛЮЧЕНИЙ
    // ============================================================================
    // (В этом модуле исключений нет, используется SolverError из solver.cpp)

    // ============================================================================
    // 3. РЕАЛИЗАЦИЯ ПУБЛИЧНЫХ ФУНКЦИЙ
    // ============================================================================

    CliParseResult ParseCliArgs(int argc, char** argv) {
        CliParseResult out;
        Pos pos = Pos::TEnd;

        for (int i = 1; i < argc; ++i) {
            const std::string a = argv[i];

            if (a == "--help" || a == "-h") {
                out.status = CliParseStatus::Help;
                return out;
            }

            if (a == "--trajectory") {
                if (i + 1 >= argc) {
                    out.status = CliParseStatus::Error;
                    out.error = "--trajectory requires a path";
                    return out;
                }
                out.options.trajectory_path = argv[++i];
                continue;
            }

            if (!a.empty() && a[0] == '-') {
                out.status = CliParseStatus::Error;
                out.error = "unknown option: " + a;
                return out;
            }

            try {
                switch (pos) {
                case Pos::TEnd:
                    out.options.solve.t_end = std::stod(a);
                    pos = Pos::M;
                    break;
                case Pos::M:
                    out.options.solve.M =
                        static_cast<std::size_t>(std::stoul(a));
                    pos = Pos::HInit;
                    break;
                case Pos::HInit:
                    out.options.solve.h_init = std::stod(a);
                    pos = Pos::LogPath;
                    break;
                case Pos::LogPath:
                    out.options.log_path = a;
                    pos = Pos::Done;
                    break;
                case Pos::Done:
                    out.status = CliParseStatus::Error;
                    out.error = "too many positional arguments";
                    return out;
                }
            }
            catch (const std::exception& e) {
                out.status = CliParseStatus::Error;
                out.error = "bad argument '" + a + "': " + e.what();
                return out;
            }
        }
        return out;  // status == Ok
    }

    void PrintUsage(std::ostream& os, const std::string& prog) {
        os <<
            "Usage: " << prog << " [options] [t_end] [M] [h_init] [log_path]\n"
            "\n"
            "Reads an ODE system from stdin until EOF and runs the pipeline\n"
            "(Parse -> Validate -> Normalize -> ReduceOrder -> Autonomize\n"
            " -> Polynomize -> Quadratize -> Solve).\n"
            "\n"
            "Options:\n"
            "  --trajectory PATH   save full trajectory to CSV\n"
            "  --help, -h          this message\n"
            "\n"
            "Positional:\n"
            "  t_end     final time (default 1)\n"
            "  M         Taylor order (default 20)\n"
            "  h_init    initial step (default 1e-3)\n"
            "  log_path  optional path for the full pipeline report\n";
    }

    void SaveTrajectory(const Solution& sol,
        const std::vector<std::string>& visible_functions,
        const std::string& path) {
        if (sol.points.empty()) {
            throw SolverError("SaveTrajectory: solution has no points");
        }

        // --- 1. Построение индексов для visible_functions ---
        // Линейный поиск по sol.functions: размеры маленькие,
        // отдельная карта не нужна.
        std::vector<std::size_t> indices;
        indices.reserve(visible_functions.size());
        for (const auto& name : visible_functions) {
            auto it = std::find(sol.functions.begin(),
                sol.functions.end(),
                name);
            if (it == sol.functions.end()) {
                throw SolverError("SaveTrajectory: function '" + name +
                    "' not found in solution");
            }
            indices.push_back(static_cast<std::size_t>(
                std::distance(sol.functions.begin(), it)));
        }

        // --- 2. Открытие файла ---
        std::ofstream f(path);
        if (!f) {
            throw SolverError("SaveTrajectory: cannot open '" + path + "'");
        }

        // --- 3. Запись содержимого ---
        f << std::setprecision(17);

        // Заголовок CSV.
        f << 't';
        for (const auto& name : visible_functions) f << ',' << name;
        f << '\n';

        // Строки данных.
        for (const auto& pt : sol.points) {
            f << pt.t;
            for (std::size_t i : indices) {
                if (i >= pt.x.size()) {
                    throw SolverError(
                        "SaveTrajectory: solution point has too few "
                        "components");
                }
                f << ',' << pt.x[i];
            }
            f << '\n';
        }

        // --- 4. Проверка успешности записи ---
        if (!f) {
            throw SolverError("SaveTrajectory: write failed for '" +
                path + "'");
        }
    }

} // namespace diffuri