// ============================================================================
// src/cli/grid.h
//
// Модуль cli/grid: перебор комбинаций численных параметров.
//
// Зависимости:
//   grid -> cli (CliOptions)
//
// Отвечает за:
//   - разбор спецификации "key=v1,v2,v3 key2=w1,w2" из argv/--grid;
//   - генерацию всех комбинаций (декартово произведение);
//   - применение одной комбинации к копии CliOptions;
//   - генерацию имени траекторного файла для комбинации (значения
//     параметров входят в имя файла через '_');
//   - формирование строк manifest-файла (index → значения параметров);
//   - формирование строк сводного summary-файла с финальными значениями
//     решений по всем комбинациям.
//
// Что модуль НЕ делает:
//   - не запускает пайплайн (это main);
//   - не пишет файлы (это SaveTrajectory / WriteReportToFile / main);
//   - не проверяет физическую допустимость значений (только тип).
//
// Формат спецификации (одна строка, пробелы между параметрами):
//   "t_end=1,2,3 atol=1e-11,1e-12,1e-13,1e-14"
//
// Комбинации перебираются в порядке декартова произведения: параметр,
// указанный первым, меняется медленнее всех, последний — быстрее.
// Для "t_end=1,2,3 M=10,20" порядок:
//   (1,10) (1,20) (2,10) (2,20) (3,10) (3,20)
//
// Режимы вывода в grid-режиме:
//
//   1. Сводка (всегда).
//      Один файл, по строке на комбинацию. Колонки:
//        source,index,<params>,steps,order,t_final,<functions>
//      Позволяет быстро найти лучшую комбинацию (min steps, max точность
//      и т.п.). Путь задаётся --summary или DefaultGridSummaryPath().
//
//   2. Траектории (опционально, только если задан --trajectory PATH).
//      Для каждой комбинации — отдельный CSV. Имя собирается из базового
//      имени, имени входного файла и значений параметров комбинации:
//        out.csv + lorenz.txt + (t_end=1, atol=1e-11)
//          → out_lorenz_1_1e-11.csv
//      Значения разделяются '_'. Формат содержимого — как у обычной
//      траектории: заголовок "t,x,y,z", затем по строке на точку.
//
//   3. Manifest (опционально).
//      Отдельный CSV, где для каждой комбинации написано, какие значения
//      параметров ей соответствуют. Используется, когда хочется
//      сопоставить index из сводки с конкретными значениями. Заголовок
//      и строки — FormatGridManifestHeader / FormatGridCombinationRow.
//
// Имя траекторного файла:
//
//   <base-stem>_<input-stem>_<value-1>_<value-2>_..._<value-N><base-ext>
//
//   base-stem  — из --trajectory, без расширения;
//   input-stem — из --input, без расширения и пути; пусто для stdin;
//   value-i    — значения параметров в порядке их объявления в спеке.
//
//   Примеры:
//     MakeGridOutputPath("out.csv", "lorenz.txt", combo(1, 1e-11))
//       → "out_lorenz_1_1e-11.csv"
//     MakeGridOutputPath("out.csv", "data/x.txt", combo(2, 1e-12))
//       → "out_x_2_1e-12.csv"
//     MakeGridOutputPath("out.csv", "", combo(1))     // stdin
//       → "out_1.csv"
//
//   Если base_path пуст — grid не сохраняет траектории, функция
//   возвращает пустую строку.
//
//   Значения проходят через SanitizeFilenameComponent: символы, которые
//   нельзя использовать в именах файлов (< > : " / \ | ? * и управляющие),
//   заменяются на '_'. Для числовых значений это no-op: "1e-11" остаётся
//   "1e-11", "1.5" остаётся "1.5".
// ============================================================================
#pragma once

// --- Стандартная библиотека ---
#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>

// --- Внутренние зависимости ---
#include "cli/cli.h"

namespace diffuri {

    // ============================================================================
    // 1. ОПЦИИ И КОНФИГУРАЦИЯ
    // ============================================================================
    // (В этом модуле нет структур опций)

    // ============================================================================
    // 2. СТРУКТУРЫ ДАННЫХ
    // ============================================================================

    /**
     * @struct GridParam
     * @brief Один параметр сетки: имя и список значений (в виде строк).
     *
     * Значения хранятся строками, потому что у разных параметров разные
     * типы: t_end — double, M — size_t, enable_order_adaptation — bool.
     * Разбор в нужный тип происходит в ApplyGridCombination по имени ключа.
     */
    struct GridParam {
        std::string              name;   ///< Имя ключа ("t_end", "M", ...).
        std::vector<std::string> values; ///< Список значений.
    };

    /**
     * @struct GridSpec
     * @brief Спецификация сетки: список параметров с их значениями.
     *
     * Пустая spec (нет параметров) означает отсутствие grid-режима.
     */
    struct GridSpec {
        std::vector<GridParam> params;   ///< Параметры в порядке объявления.

        /// Есть ли в спеке хоть один параметр.
        [[nodiscard]] bool empty() const noexcept { return params.empty(); }
    };

    /**
     * @struct GridCell
     * @brief Одно (имя, значение) из конкретной комбинации.
     */
    struct GridCell {
        std::string name;
        std::string value;
    };

    /**
     * @struct GridCombination
     * @brief Конкретная комбинация значений — по одной ячейке на параметр.
     *
     * Порядок ячеек совпадает с порядком параметров в GridSpec.
     */
    struct GridCombination {
        std::vector<GridCell> cells;
    };

    /**
     * @struct GridSummaryMeta
     * @brief Метаданные одной строки сводного файла.
     *
     * Отделяет «данные о прогоне» (что решали, сколько шагов, где финиш)
     * от «данных решения» (вектор финальных значений). Так модуль grid
     * не зависит от output.h / solver.h — main сам извлекает поля из
     * Solution / SolvedView и передаёт их сюда.
     */
    struct GridSummaryMeta {
        std::string source;         ///< Источник системы ("stdin" или путь к файлу).
        std::size_t index = 0;      ///< Номер комбинации (совпадает с порядком в EnumerateGrid).
        std::size_t steps = 0;      ///< Число шагов интегрирования.
        std::size_t order_used = 0; ///< Фактически использованный порядок метода.
        double      t_final = 0.0;  ///< Финальное время.
    };

    // ============================================================================
    // 3. ИСКЛЮЧЕНИЯ
    // ============================================================================

    /**
     * @class GridError
     * @brief Ошибка разбора спецификации grid или применения комбинации.
     *
     * Типичные причины:
     *   - спецификация не содержит '=' в очередной паре;
     *   - имя параметра пустое;
     *   - список значений пуст (key=);
     *   - значение не разбирается в нужный тип (t_end=abc);
     *   - неизвестное имя параметра (foo=1,2);
     *   - дубликат имени параметра.
     */
    class GridError : public std::runtime_error {
    public:
        explicit GridError(const std::string& what);
    };

    // ============================================================================
    // 4. ПУБЛИЧНЫЙ API (свободные функции)
    // ============================================================================

    // --- 4.1. Разбор спецификации и генерация комбинаций ---

    /**
     * @brief Разобрать спецификацию grid из строки.
     *
     * Формат: одна или несколько пар key=v1,v2,v3, разделённых пробелами.
     * Значения внутри одного параметра разделены запятыми. Пробелы вокруг
     * '=' и ',' игнорируются.
     *
     * Пример: "t_end=1,2,3 atol=1e-11,1e-12,1e-13,1e-14"
     *
     * Пустая строка даёт пустую спецификацию (params пуст).
     * Дубликаты имён параметров НЕ допускаются — бросается GridError.
     *
     * @param text Спецификация.
     * @return     Разобранная спецификация.
     * @throws GridError при синтаксической ошибке.
     */
    [[nodiscard]] GridSpec ParseGridSpec(const std::string& text);

    /**
     * @brief Перебрать все комбинации параметров.
     *
     * Возвращает декартово произведение значений. Порядок соответствует
     * описанию в шапке файла: первый параметр — внешний счётчик, последний —
     * внутренний.
     *
     * Для пустой спецификации возвращает пустой вектор.
     * Общее число комбинаций = произведение размеров списков значений.
     *
     * @param spec Спецификация.
     * @return     Вектор комбинаций.
     */
    [[nodiscard]] std::vector<GridCombination> EnumerateGrid(const GridSpec& spec);

    /**
     * @brief Применить одну комбинацию к CliOptions.
     *
     * Значения разбираются в нужный тип по имени ключа (см. шапку файла).
     * Ячейки применяются в порядке следования; последняя побеждает, если
     * по ошибке указан один и тот же ключ дважды (впрочем, ParseGridSpec
     * такое отсекает).
     *
     * @param opts  Опции; модифицируются по месту.
     * @param combo Комбинация.
     * @throws GridError при неизвестном ключе или неверном значении.
     */
    void ApplyGridCombination(CliOptions& opts, const GridCombination& combo);

    // --- 4.2. Имена траекторных файлов ---

    /**
     * @brief Заменить символы, недопустимые в именах файлов, на '_'.
     *
     * Windows запрещает в именах файлов: < > : " / \ | ? * и управляющие
     * символы (коды 0..31). Пробелы и табы тоже заменяются на '_' — не
     * потому что запрещены, а чтобы имена оставались компактными и
     * предсказуемыми.
     *
     * Для числовых значений это no-op: "1e-11" → "1e-11", "1.5" → "1.5",
     * "-3" → "-3".
     *
     * Примеры:
     *   SanitizeFilenameComponent("1e-11")       → "1e-11"
     *   SanitizeFilenameComponent("data/x.txt")  → "data_x.txt"
     *   SanitizeFilenameComponent("my system")   → "my_system"
     *   SanitizeFilenameComponent("a:b*c?d")     → "a_b_c_d"
     *   SanitizeFilenameComponent("")            → ""
     *
     * @param value Исходное значение.
     * @return      Значение с заменёнными символами.
     */
    [[nodiscard]] std::string SanitizeFilenameComponent(const std::string& value);

    /**
     * @brief Суффикс имени файла для комбинации: "_v1_v2_..._vN".
     *
     * Каждое значение предваряется '_'. Пустая комбинация даёт пустую
     * строку (в grid-режиме такого не бывает — spec без параметров
     * означает отсутствие grid).
     *
     * Значения проходят через SanitizeFilenameComponent.
     *
     * Примеры:
     *   combo(1, 1e-11) → "_1_1e-11"
     *   combo(true)     → "_true"
     *   combo(-1)       → "_-1"
     *
     * @param combo Комбинация.
     * @return      Строка вида "_1_1e-11".
     */
    [[nodiscard]] std::string MakeGridSuffix(const GridCombination& combo);

    /**
     * @brief Сгенерировать путь траекторного файла для комбинации.
     *
     * Собирает имя из четырёх частей:
     *   <base-stem>_<input-stem>_<value-1>_..._<value-N><base-ext>
     *
     * Расширение берётся от base_path. Если input_path пуст (stdin),
     * компонента имени файла опускается. Если base_path пуст — grid не
     * сохраняет траектории, возвращается пустая строка.
     *
     * Примеры:
     *   MakeGridOutputPath("out.csv", "lorenz.txt", combo(1, 1e-11))
     *     → "out_lorenz_1_1e-11.csv"
     *   MakeGridOutputPath("out.csv", "", combo(1))
     *     → "out_1.csv"
     *   MakeGridOutputPath("results/out.log", "vdp.txt", combo(2, 1e-12))
     *     → "results/out_vdp_2_1e-12.log"
     *   MakeGridOutputPath("", "lorenz.txt", combo(1))
     *     → ""
     *
     * @param base_path  Базовый путь (--trajectory).
     * @param input_path Путь входного файла (для компоненты имени).
     * @param combo      Комбинация.
     * @return           Сформированный путь.
     */
    [[nodiscard]] std::string MakeGridOutputPath(
        const std::string& base_path,
        const std::string& input_path,
        const GridCombination& combo);

    // --- 4.3. Manifest ---

    /**
     * @brief Заголовок manifest-файла по спецификации.
     *
     * Формат: "index,param1,param2,..." без завершающего '\n'.
     * Для пустой спецификации возвращает "index".
     *
     * @param spec Спецификация.
     * @return     Строка заголовка.
     */
    [[nodiscard]] std::string FormatGridManifestHeader(const GridSpec& spec);

    /**
     * @brief Одна строка manifest-файла: index + значения всех параметров.
     *
     * Формат: "index,v1,v2,..." без завершающего '\n'. Значения идут в том же
     * порядке, что параметры в исходной спеке.
     *
     * @param combo Комбинация.
     * @param index Номер комбинации.
     * @return      Строка CSV.
     */
    [[nodiscard]] std::string FormatGridCombinationRow(
        const GridCombination& combo,
        std::size_t index);

    // --- 4.4. Сводный файл (summary) ---

    /**
     * @brief Путь по умолчанию для сводного файла.
     *
     * Возвращает "grid_summary.csv" — относительно CWD, как и все остальные
     * выходные файлы проекта.
     *
     * @return "grid_summary.csv".
     */
    [[nodiscard]] std::string DefaultGridSummaryPath();

    /**
     * @brief Заголовок сводного файла.
     *
     * Колонки:
     *   source,index,<param_1>,...,<param_k>,steps,order,t_final,<f_1>,...,<f_m>
     *
     * source    — имя входного файла или "stdin";
     * index     — номер комбинации (порядок в EnumerateGrid);
     * <param_i> — имена параметров сетки в порядке, заданном пользователем;
     * steps     — число шагов интегрирования;
     * order     — фактически использованный порядок метода;
     * t_final   — финальное время;
     * <f_j>     — имена «видимых» функций (без служебных q_*, v_*; см.
     *             MakeSolvedView в output.h).
     *
     * @param spec           Спецификация сетки.
     * @param function_names Имена отображаемых функций решения.
     * @return               Строка заголовка без завершающего '\n'.
     */
    [[nodiscard]] std::string FormatGridSummaryHeader(
        const GridSpec& spec,
        const std::vector<std::string>& function_names);

    /**
     * @brief Одна строка сводного файла: метаданные + вектор финальных
     *        значений.
     *
     * Формат значений — 17 значащих цифр (max_digits10 для double),
     * тот же, что в SaveTrajectory. Разделитель — ','.
     *
     * @param combo  Комбинация параметров (значения идут в том же порядке,
     *               что параметры в спеке).
     * @param meta   Метаданные прогона (source, index, steps, order, t_final).
     * @param values Финальные значения отображаемых функций.
     * @return       Строка CSV без завершающего '\n'.
     */
    [[nodiscard]] std::string FormatGridSummaryRow(
        const GridCombination& combo,
        const GridSummaryMeta& meta,
        const std::vector<double>& values);

} // namespace diffuri