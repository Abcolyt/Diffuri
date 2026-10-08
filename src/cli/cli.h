// ============================================================================
// src/cli/cli.h
//
// Разбор аргументов командной строки Diffuri и утилиты CLI-слоя.
// Вынесено из main.cpp, чтобы тестировать без subprocess.
//
// Зависимости:
//   cli -> solver  (SolveOptions, Solution)
// ============================================================================
#pragma once

#include <iosfwd>
#include <optional>
#include <string>
#include <vector>

#include "solver/solver.h"

namespace diffuri {

    /// Результат разбора argv.
    struct CliOptions {
        SolveOptions solve;             ///< t_end, M, h_init
        std::string  log_path;          ///< путь для отчёта (пусто = не писать)
        std::string  trajectory_path;   ///< путь для CSV траектории (пусто = не писать)
    };

    /// Что вернул ParseCliArgs.
    enum class CliParseStatus {
        Ok,       ///< разбор успешен, options валиден
        Help,     ///< запрошена справка (--help / -h)
        Error,    ///< ошибка разбора; текст уже записан в err
    };

    struct CliParseResult {
        CliParseStatus status = CliParseStatus::Ok;
        CliOptions     options;
        std::string    error;   ///< непусто только при status == Error
    };

    /**
     * @brief Разобрать argv.
     *
     * Поддерживает:
     *   --trajectory PATH    сохранить траекторию в CSV
     *   --help, -h           справка
     *   <t_end> <M> <h_init> <log_path>   позиционно (обратная совместимость)
     *
     * Порядок: именованные флаги могут стоять в любом месте; позиционные
     * заполняются в порядке появления.
     *
     * @param argc, argv  как в main().
     */
    [[nodiscard]] CliParseResult ParseCliArgs(int argc, char** argv);

    /// Напечатать справку в os.
    void PrintUsage(std::ostream& os, const std::string& prog_name);

    /**
     * @brief Записать траекторию в CSV.
     *
     * Колонки: t, затем visible_functions в переданном порядке.
     * visible_functions — имена функций, которые должны быть записаны;
     * должны быть подмножеством sol.functions. Вспомогательные
     * переменные (q_i, v_i), не попавшие в visible_functions, в CSV
     * не пишутся.
     *
     * Точность — 17 значащих цифр (round-trip для double).
     * Разделитель — ',', конец строки — '\n'.
     *
     * @throws SolverError если sol.points пуст; если файл не открылся;
     *         если запись провалилась; если какое-то имя из
     *         visible_functions не найдено в sol.functions
     *         (текст: "SaveTrajectory: function '<name>' not found
     *         in solution").
     */
    void SaveTrajectory(const Solution& sol,
        const std::vector<std::string>& visible_functions,
        const std::string& path);

} // namespace diffuri