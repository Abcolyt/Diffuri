
# Diffuri

Учебный проект: решатель систем ОДУ методом рядов Тейлора с адаптивным
шагом. Ключевая идея — любая система приводится к полиномиальному
(квадратичному) виду, после чего решается методом Тейлора.

- **Язык:** C++20
- **Сборка:** CMake ≥ 3.20
- **Тесты:** GoogleTest (FetchContent)

## Пайплайн обработки системы

```
ввод (текст)
  ↓ ParseSystem / Validate
  ↓ NormalizeSystem      — перенос производных в LHS
  ↓ OrderReducer         — понижение порядка
  ↓ Autonomize           — t становится переменной
  ↓ Polynomize           — sin/cos/exp/ln → новые v_i
  ↓ Quadratize           — степени > 2 → новые q_i
  ↓ Solve                — метод рядов Тейлора (адаптивный шаг)
результат (Solution)
```

## Входной формат

```
x'' = -sin(x)
x(0) = 0
x'(0) = 1
```

Поддерживаются: числа (`1e-3`, `2.5`), имена, производные (`y'`, `dy/dt`,
`d²y/dt²`), операции (`+ - * / ^`), вызовы (`sin`, `cos`, `exp`, `ln`),
комментарии (`# …`).

## CLI

```
Diffuri [--trajectory PATH] [--help] [t_end M h_init log_path]
```

Позиционные аргументы (все опциональны):

- `t_end` — конечная точка интегрирования (по умолчанию 1.0)
- `M` — порядок метода (по умолчанию 20)
- `h_init` — начальный шаг (по умолчанию 1e-3)
- `log_path` — путь для полного отчёта

Именованные флаги:

- `--trajectory PATH` — сохранить траекторию в CSV (только исходные функции)
- `--help`, `-h` — справка

Пример (PowerShell):

```powershell
echo "x'' = -x`nx(0) = 1`nx'(0) = 0" | .\Diffuri.exe --trajectory out.csv 6.28 20 1e-3
```

## Структура проекта

```
Diffuri/
├── CMakeLists.txt              ← точка входа: только include модулей
├── CMakePresets.json           ← пресеты VS (x64/x86, debug/release)
├── .gitignore
│
├── Cmake/                      ← модули сборки
│   ├── options.cmake           ← BUILD_TESTS
│   ├── project.cmake           ← C++20, пути DIFFURI_SRC_DIR / DIFFURI_TESTS_DIR
│   ├── warnings.cmake          ← diffuri_enable_warnings
│   ├── sources_core.cmake      ← CORE_SOURCES + MAIN_SOURCE
│   ├── headers.cmake           ← список .h
│   ├── dependencies.cmake      ← GoogleTest через FetchContent
│   ├── targets.cmake           ← diffuri_core + Diffuri
│   └── tests.cmake             ← unit_tests / system_tests + cli_smoke
│
├── src/                        ← библиотека
│   ├── main.cpp                ← CLI-демонстратор
│   ├── input/                  ← парсер, RawSystem, Validate
│   ├── simplify/               ← упрощение выражений
│   ├── normalize/              ← приведение к виду y^(n) = RHS
│   ├── order_reducer/          ← понижение порядка
│   ├── autonomize/             ← устранение явной зависимости от t
│   ├── polynomization/         ← sin/cos/exp/ln → новые переменные
│   ├── quadratize/             ← степени > 2 → новые переменные
│   ├── solver/                 ← метод рядов Тейлора
│   │   ├── solver.*            ← Solve, SolveOptions, Solution
│   │   ├── taylor_spec.*       ← TaylorSpec, BuildTaylorSpec
│   │   ├── taylor_table.*      ← TaylorTable (рекуррентные формулы)
│   │   ├── error_control.*     ← ErrorEstimate (L²-норма §2.1.4)
│   │   ├── step_control.*      ← PickStep (§2.1.2 + §2.1.3 + §2.1.4 + §2.2)
│   │   ├── order_control.*     ← PickOrder (MVP: pass-through)
│   │   └── convergence.*       ← ρ, α, InverseU/V, ComputeTau
│   ├── cli/                    ← разбор argv, SaveTrajectory
│   ├── output/                 ← PrintSystem, FormatSolved, WriteReportToFile
│   └── pipeline/               ← RunPipeline, PipelineTrace
│
├── tests/
│   ├── unit/                   ← юнит-тесты модулей
│   └── systems/                ← тесты систем ОДУ + CLI E2E
│
├── tools/
│   └── gen_reference.py        ← генератор эталонов (scipy, DOP853)
│
└── out/                        ← артефакты сборки (не коммитится)
```

## Как устроена сборка

Корневой `CMakeLists.txt` не содержит логики — только подключает
модули из `Cmake/`:

1. `options.cmake` — `BUILD_TESTS`
2. `project.cmake` — язык и пути
3. `warnings.cmake` — функция предупреждений
4. `sources_core.cmake` + `headers.cmake` — списки файлов
5. `dependencies.cmake` — GoogleTest
6. `targets.cmake` — цели:
   - **`diffuri_core`** — STATIC-библиотека из всех модулей
   - **`Diffuri`** — CLI, линкуется с `diffuri_core`
7. `tests.cmake` — цели `unit_tests`, `system_tests`, тест `cli_smoke`

Код компилируется один раз в `diffuri_core`, приложение и тесты линкуются
с ним.

## Как добавлять новые файлы

| Что добавляешь | Куда прописать |
|---|---|
| Новый модуль `src/<модуль>/<модуль>.cpp` | `Cmake/sources_core.cmake` → `CORE_SOURCES` |
| Его заголовок `.h` | `Cmake/headers.cmake` |
| Юнит-тест | файл в `tests/unit/` + строка в `Cmake/tests.cmake` → `UNIT_TEST_SOURCES` |
| Тест системы ОДУ | файл в `tests/systems/` + строка в `Cmake/tests.cmake` → `SYSTEM_TEST_SOURCES` |

Include-пути (`src/` — PUBLIC у `diffuri_core`), линковка и регистрация в
CTest подтягиваются сами. В коде:

```cpp
#include "solver/solver.h"
```

## Сборка и запуск

В Visual Studio — через пресеты из `CMakePresets.json`. Из консоли:

```bash
# Конфигурация (первый раз скачает GoogleTest — нужен интернет)
cmake -S . -B out/build/x64-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug

# Сборка
cmake --build out/build/x64-debug

# Все тесты
ctest --test-dir out/build/x64-debug --output-on-failure

# Только CLI E2E
ctest --test-dir out/build/x64-debug -R cli_smoke --output-on-failure -V

# Приложение
./out/build/x64-debug/Diffuri.exe
```

## Тесты

- `unit_tests` — юнит-тесты модулей (~970 активных)
- `system_tests` — E2E на наборе систем ОДУ (9 систем, эталоны scipy)
- `cli_smoke` — CTest-тест: запускает `Diffuri` как подпроцесс, проверяет
  exit-код, CSV, отсутствие вспомогательных колонок

Эталоны для `system_tests` генерируются скриптом:

```bash
python tools/gen_reference.py
```

Скрипт требует `numpy` + `scipy`, запускается вручную, результат
(`tests/systems/reference_systems.h`) коммитится.

## Опции сборки

| Опция | По умолчанию | Описание |
|---|---|---|
| `BUILD_TESTS` | `ON` | Собирать тесты (GoogleTest) |

Отключить: `-DBUILD_TESTS=OFF`.
