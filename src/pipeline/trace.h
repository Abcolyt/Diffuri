// ============================================================================
// src/pipeline/trace.h
//
// Модуль pipeline/trace: обёртка для фиксации состояний системы на каждом
// этапе пайплайна и их отображения (полный/чистый/аннотированный вид).
//
// Зависимости:
//   trace -> input       (RawSystem, Equation, InitialCondition)
//   trace -> expression  (Expr, ExprPtr, фабрики)
//
// Назначение:
//   - дать CLI и логам возможность посмотреть систему «как она есть» на
//     каждом этапе (Capture + At);
//   - отдать «чистый» вид системы без вспомогательных переменных, введённых
//     промежуточными этапами (View);
//   - сформировать человекочитаемый отчёт с аннотациями вспомогательных
//     переменных (Format);
//   - отдать метаданные вспомогательных переменных наружу (Auxiliary) —
//     для модуля output, который строит «чистое» представление решения.
//
// Текущая реализация — snapshot-based (вариант A): система копируется на
// каждом Capture. Публичный API при этом стабилен: переход на reconstruction
// (вариант B) не потребует правок вызывающего кода.
// ============================================================================
#pragma once

#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include "core/expression.h"
#include "input/input.h"

namespace diffuri {

    // ============================================================================
    // СТАДИИ ПАЙПЛАЙНА
    // ============================================================================

    /**
     * @enum Stage
     * @brief Точки пайплайна, между которыми фиксируется состояние системы.
     *
     * Порядок альтернатив — это порядок прохождения пайплайна.
     * Stages() опирается на этот порядок при обходе хранилища.
     */
    enum class Stage {
        Parsed,        ///< После ParseSystem: сырой текст превращён в RawSystem.
        Validated,     ///< После Validate: семантическая согласованность проверена.
        Normalized,    ///< После NormalizeSystem: y^(n) = RHS.
        OrderReduced,  ///< После ReduceOrder: система первого порядка.
        Polynomized,   ///< После Polynomize: система полиномиальна.
        Solved,        ///< После Solver: траектория получена.
    };

    /// Читаемое имя стадии — для заголовков Format и сообщений об ошибках.
    [[nodiscard]] std::string ToString(Stage stage);

    // ============================================================================
    // ХРАНИЛИЩЕ СНИМКОВ
    // ============================================================================

    /**
     * @class PipelineTrace
     * @brief История состояний системы по стадиям пайплайна.
     *
     * Внутри — снимки RawSystem на каждую зафиксированную стадию плюс
     * метаданные вспомогательных переменных, введённых промежуточными
     * этапами (OrderReducer, Polynomize).
     *
     * Move-only: содержит RawSystem с ExprPtr внутри, копирование запрещено.
     */
    class PipelineTrace {
    public:
        /// Зафиксировать текущее состояние системы.
        ///
        /// Первый Capture(Stage::Parsed) запоминает список исходных функций —
        /// он нужен View и Format, чтобы отличать исходные имена от
        /// вспомогательных. Повторные Capture(Parsed) список не перезатирают.
        void Capture(Stage stage, const RawSystem& sys);

        /// Прикрепить метаданные к стадии. Заменяет предыдущие, если были.
        void SetAuxiliary(Stage stage, std::map<std::string, ExprPtr> aux);

        /// Полный снимок системы на стадии. Содержит вспомогательные функции.
        /// @throws std::out_of_range если стадия не зафиксирована.
        [[nodiscard]] const RawSystem& At(Stage stage) const;

        /**
         * @brief Чистый вид: только исходные функции, вспомогательные скрыты.
         *
         * Возвращает систему до появления вспомогательных переменных.
         * Для стадий Parsed/Validated/Normalized это просто снимок
         * соответствующей стадии. Для OrderReduced/Polynomized/Solved —
         * состояние системы до ввода вспомогательных переменных, то есть
         * эквивалент At(Normalized).
         *
         * @throws std::logic_error если Stage::Parsed не был зафиксирован
         *         (нужен список исходных функций), либо если для стадий
         *         OrderReduced/Polynomized/Solved не зафиксирован
         *         Stage::Normalized.
         */
        [[nodiscard]] RawSystem View(Stage stage) const;

        /**
         * @brief Читаемое представление для CLI/логов.
         *
         * Формат:
         *   [<Stage>]
         *   # Source functions: x y                 (только если есть auxiliary)
         *   # Auxiliary: x_1 (from x'), v_1 = sin(x)
         *   <ToString(RawSystem)>
         *
         * Показывает ПОЛНЫЙ вид системы (со вспомогательными), дополненный
         * комментариями о том, откуда взялась каждая вспомогательная
         * переменная. Для Derivative{f, k} в определении пишется
         * «(from f')», для всего остального — «= <ToString>».
         *
         * @throws std::logic_error при тех же условиях, что и View.
         */
        [[nodiscard]] std::string Format(Stage stage) const;

        /**
         * @brief Метаданные вспомогательных переменных для стадии.
         *
         * Возвращает карту {имя_переменной → её_определение}, привязанную
         * к стадии (например, Stage::OrderReduced для x_1 = Derivative{x,1},
         * Stage::Polynomized для v_1 = sin(x)). Если для стадии метаданные
         * не установлены — возвращает пустую карту.
         *
         * Ссылка действительна, пока живёт PipelineTrace и не было вызова
         * SetAuxiliary для той же стадии.
         *
         * Используется модулем output: чтобы отличить «полезные» вспомогательные
         * (x_1 — это производная x, её можно переименовать в x') от
         * «внутренних» (v_1 — служебная переменная Polynomize, её надо скрыть).
         */
        [[nodiscard]] const std::map<std::string, ExprPtr>&
            Auxiliary(Stage stage) const;

        /// Список зафиксированных стадий в порядке прохождения пайплайна.
        [[nodiscard]] std::vector<Stage> Stages() const;

        /// Зафиксирована ли стадия.
        [[nodiscard]] bool Has(Stage stage) const;

    private:
        std::map<Stage, RawSystem>                      snapshots_;
        std::map<Stage, std::map<std::string, ExprPtr>> auxiliary_;
        std::vector<std::string>                        original_functions_;
    };

} // namespace diffuri