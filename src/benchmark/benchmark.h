// ============================================================================
// src/benchmark/benchmark.h
//
// Модуль benchmark: проведение серии запусков (sweep) для оценки сходимости
// метода рядов Тейлора при варьировании одного из параметров (M, rtol, atol).
//
// Зависимости:
//   benchmark -> solver (SolveOptions, Solution, SolverError)
//   benchmark -> pipeline/runner (RunPipeline, RunResult)
//   benchmark -> output (MakeSolvedView)
//
// Отвечает за:
//   - парсинг спецификации sweep-параметров и метрик;
//   - вычисление эталонного решения;
//   - многократный прогон системы с разными параметрами;
//   - вычисление метрик ошибки (final_abs, final_rel, traj_l2);
//   - запись результатов в CSV-формат.
//
// Что модуль НЕ делает:
//   - не взаимодействует с CLI или TUI напрямую;
//   - не строит графики (это задача внешнего Python-скрипта);
//   - не модифирует исходную логику solver или pipeline.
// ============================================================================
#pragma once

// --- Стандартная библиотека ---
#include <cstddef>
#include <string>
#include <vector>

// --- Внутренние зависимости ---
#include "solver/solver.h"

namespace diffuri {

    // ============================================================================
    // 1. ОПЦИИ И КОНФИГУРАЦИЯ
    // ============================================================================

    /**
     * @enum SweepVariable
     * @brief Параметр, который варьируется в ходе sweep-прогона.
     */
    enum class SweepVariable {
        M,    ///< Порядок метода Тейлора.
        Rtol, ///< Относительная точность.
        Atol  ///< Абсолютная точность.
    };

    /**
     * @enum SweepMetric
     * @brief Метрика ошибки для сравнения с эталонным решением.
     */
    enum class SweepMetric {
        FinalAbs, ///< Абсолютная L2-норма разности финальных точек.
        FinalRel, ///< Относительная L2-норма разности финальных точек.
        TrajL2    ///< Интегральная L2-норма разности траекторий.
    };

    /**
     * @struct SweepSpec
     * @brief Спецификация диапазона варьируемого параметра.
     */
    struct SweepSpec {
        SweepVariable variable = SweepVariable::M; ///< Варьируемый параметр.
        std::vector<double> values;                ///< Конкретные значения для прогона.
    };

    // ============================================================================
    // 2. СТРУКТУРЫ ДАННЫХ
    // ============================================================================

    /**
     * @struct SweepPoint
     * @brief Результат одного прогона в рамках sweep.
     */
    struct SweepPoint {
        double value = 0.0;                                  ///< Значение варьируемого параметра.
        double error = std::numeric_limits<double>::quiet_NaN(); ///< Значение метрики ошибки (NaN при сбое).
        std::size_t steps = 0;                               ///< Число шагов интегрирования.
        std::size_t order_used = 0;                          ///< Фактически использованный порядок.
        bool ok = false;                                     ///< Флаг успешности прогона.
        std::string status;                                  ///< Текст ошибки (пусто при успехе).
    };

    /**
     * @struct SweepResult
     * @brief Сводный результат всего sweep-прогона.
     */
    struct SweepResult {
        std::string system_name = "unnamed";       ///< Имя системы (для заголовков CSV).
        SweepVariable variable = SweepVariable::M; ///< Варьируемый параметр.
        SweepMetric metric = SweepMetric::FinalRel;///< Использованная метрика.
        bool reference_ok = false;                 ///< Успешность вычисления эталона.
        std::vector<SweepPoint> points;            ///< Точки данных.
    };

    // ============================================================================
    // 3. ИСКЛЮЧЕНИЯ
    // ============================================================================
    // (Используются стандартные std::runtime_error и SolverError)

    // ============================================================================
    // 4. ПУБЛИЧНЫЙ API (свободные функции)
    // ============================================================================

    /**
     * @brief Разобрать строку спецификации sweep.
     *
     * Формат: var=A:B:N, где N — число точек.
     * Для M точки распределены линейно. Для rtol и atol — логарифмически (равномерно в log10).
     *
     * @param text Строка спецификации (например, "M=5:60:5" или "rtol=1e-4:1e-14:11").
     * @return     Структура SweepSpec.
     * @throws     std::runtime_error при ошибке формата.
     */
    [[nodiscard]] SweepSpec ParseSweepSpec(const std::string& text);

    /**
     * @brief Разобрать строку названия метрики.
     *
     * @param text Строка метрики ("final_abs", "final_rel", "traj_l2").
     * @return     Значение enum SweepMetric.
     * @throws     std::runtime_error при неизвестном названии.
     */
    [[nodiscard]] SweepMetric ParseSweepMetric(const std::string& text);

    /**
     * @brief Выполнить sweep-прогон системы.
     *
     * Сначала вычисляет эталонное решение (M=60, rtol=1e-14, atol=1e-16, h_min=1e-15).
     * Затем проходит по значениям из spec, вычисляя метрику ошибки.
     * Сбои на отдельных точках не прерывают прогон, а помечаются как ok=false.
     *
     * @param system_text Текст системы ОДУ.
     * @param base        Базовые параметры интегрирования (SolveOptions).
     * @param spec        Спецификация варьируемого параметра.
     * @param metric      Метрика для вычисления ошибки.
     * @return            Структура SweepResult.
     */
    [[nodiscard]] SweepResult RunSweep(const std::string& system_text,
        const SolveOptions& base,
        const SweepSpec& spec,
        SweepMetric metric);

    /**
     * @brief Записать результаты sweep-прогона в CSV-файл.
     *
     * @param r    Результаты прогона.
     * @param path Путь к выходному файлу.
     * @throws     std::runtime_error при ошибке открытия или записи файла.
     */
    void WriteSweepCsv(const SweepResult& r, const std::string& path);

} // namespace diffuri