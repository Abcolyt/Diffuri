// ============================================================================
// src/cli/cli.h
//
// Разбор аргументов командной строки Diffuri и утилиты CLI-слоя.
// Вынесено из main.cpp, чтобы тестировать без subprocess.
//
// Зависимости:
//   cli -> solver  (SolveOptions, Solution, SolverError)
//
// Отвечает за:
//   - разбор argv в структуру CliOptions;
//   - печать справки (PrintUsage);
//   - сохранение траектории в CSV (SaveTrajectory).
//
// Что модуль НЕ делает:
//   - не запускает пайплайн (это main.cpp / runner);
//   - не решает систему (это solver);
//   - не читает stdin (это input::ParseSystemFromStdin).
// ============================================================================
#pragma once

// --- Стандартная библиотека ---
#include <iosfwd>
#include <optional>
#include <string>
#include <vector>

// --- Внутренние зависимости ---
#include "solver/solver.h"

namespace diffuri {

    // ============================================================================
    // 1. ОПЦИИ И КОНФИГУРАЦИЯ
    // ============================================================================

    /**
     * @struct CliOptions
     * @brief Результат разбора argv.
     */
    struct CliOptions {
        SolveOptions solve = {};             ///< t_end, M, h_init.
        std::string  log_path = {};          ///< Путь для отчёта (пусто = не писать).
        std::string  trajectory_path = {};   ///< Путь для CSV траектории (пусто = не писать).
    };

    // ============================================================================
    // 2. СТРУКТУРЫ ДАННЫХ
    // ============================================================================

    /**
     * @enum CliParseStatus
     * @brief Что вернул ParseCliArgs.
     */
    enum class CliParseStatus {
        Ok,       ///< Разбор успешен, options валиден.
        Help,     ///< Запрошена справка (--help / -h).
        Error,    ///< Ошибка разбора; текст уже записан в error.
    };

    /**
     * @struct CliParseResult
     * @brief Результат разбора argv с кодом статуса и сообщением об ошибке.
     */
    struct CliParseResult {
        CliParseStatus status = CliParseStatus::Ok; ///< Статус разбора.
        CliOptions     options = {};                ///< Разобранные опции.
        std::string    error = {};                  ///< Сообщение об ошибке (status == Error).
    };

    // ============================================================================
    // 3. ИСКЛЮЧЕНИЯ
    // ============================================================================
    // (В этом модуле используется SolverError из solver.h)

    // ============================================================================
    // 4. ПУБЛИЧНЫЙ API (свободные функции)
    // ============================================================================

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
     * @param argc Число аргументов (как в main).
     * @param argv Массив аргументов (как в main).
     * @return     Структура CliParseResult со статусом и результатом.
     */
    [[nodiscard]] CliParseResult ParseCliArgs(int argc, char** argv);

    /**
     * @brief Напечатать справку в os.
     *
     * @param os        Выходной поток.
     * @param prog_name Имя программы (argv[0]).
     */
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
     * @param sol                Результат интегрирования.
     * @param visible_functions  Имена функций для записи.
     * @param path               Путь к CSV-файлу.
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