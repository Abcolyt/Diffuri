#include "cli/cli.h"

#include <cstddef>
#include <fstream>
#include <iomanip>
#include <ostream>
#include <stdexcept>

namespace diffuri {

    namespace {
        // Позиция, которую сейчас ожидаем.
        enum class Pos { TEnd, M, HInit, LogPath, Done };
    }

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
            "(Parse -> Validate -> Normalize -> OrderReducer -> Autonomize\n"
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

    void SaveTrajectory(const Solution& sol, const std::string& path) {
        std::ofstream f(path);
        if (!f) {
            throw SolverError("SaveTrajectory: cannot open '" + path + "'");
        }

        f << std::setprecision(17);

        f << 't';
        for (const auto& name : sol.functions) f << ',' << name;
        f << '\n';

        for (const auto& pt : sol.points) {
            f << pt.t;
            for (double v : pt.x) f << ',' << v;
            f << '\n';
        }

        if (!f) {
            throw SolverError("SaveTrajectory: write failed for '" + path + "'");
        }
    }

} // namespace diffuri