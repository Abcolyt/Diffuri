# Соглашение об именовании (Diffuri)

## 1. Таблица стилей

| Сущность | Стиль | Пример |
|---|---|---|
| Файлы | `snake_case` | `taylor_table.cpp` |
| Классы / структуры / `enum` | `PascalCase` | `RawSystem`, `StdinMode::UntilEof` |
| Свободные функции и методы | `PascalCase` | `Solve()`, `table.Evaluate()` |
| Публичные поля | `snake_case` | `h_init`, `independent_variable` |
| Приватные поля | `snake_case_` | `text_`, `pos_` |
| Локальные переменные | `snake_case` | `current_h`, `step_count` |
| Константы | `kPascalCase` | `kGraduationRuns` |
| Namespaces | `snake_case` | `diffuri::step_control` |

## 2. Префиксы свободных функций

| Назначение | Префикс | Пример |
|---|---|---|
| Действие (меняет состояние) | Глагол | `Validate()`, `Reset()` |
| Запрос / Вычисление | `Get`, `Calculate`, `Find`, `Collect` | `CollectT0s()`, `CalculateError()` |
| Предикат (`bool`) | `Is`, `Has`, `Can`, `Looks` | `IsBlankOrComment()` |
| Фабрика / Создание | `Make`, `Parse`, `Build` | `ParseSystem()`, `BuildTaylorSpec()` |
| Преобразование | `To`, `From` | `ToString()` |

## 3. Доменные правила и запреты

- **Суффиксы:** Исключения всегда оканчиваются на `Error` (`SolverError`). Структуры настроек — на `Options` (`SolveOptions`).
- **Математика:** Разрешены короткие имена `h`, `M`, `K`, `t0`, `t_end`, `rho`, `tau`. Греческие символы (ρ, τ, α) — только в комментариях.
- **Запрещено:** 
  - Венгерская нотация (`strName`, `iCount`).
  - Префикс `m_` для полей (используем `_` в конце).
  - Суффикс `_utils` для неймспейсов (просто `step_control`, а не `step_control_utils`).
  - Однобуквенные переменные (кроме индексов `i, j, k` и мат. обозначений).