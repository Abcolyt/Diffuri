# Diffuri

Учебный проект (автоматизация решения дифференциальных уравнений):
система ОДУ → полиномиальная форма → решение (идея рядов Тейлора).

- **Язык:** C++20
- **Сборка:** CMake ≥ 3.20
- **Тесты:** GoogleTest (подключается автоматически через FetchContent)

## Структура проекта

```
Diffuri/
├── CMakeLists.txt              ← точка входа сборки: только include модулей
├── CMakePresets.json           ← пресеты Visual Studio (x64/x86, debug/release)
├── .gitignore                  ← игнорирует out/, .vs/
│
├── Cmake/                      ← модули сборки (вся логика здесь)
│   ├── options.cmake           ← опции: BUILD_TESTS=ON
│   ├── project.cmake           ← project(), C++20, пути DIFFURI_SRC_DIR / DIFFURI_TESTS_DIR
│   ├── warnings.cmake          ← функция diffuri_enable_warnings(/W4, -Wall...)
│   ├── sources_core.cmake      ← списки .cpp: CORE_SOURCES + MAIN_SOURCE
│   ├── headers.cmake           ← список .h (для видимости в IDE)
│   ├── dependencies.cmake      ← GoogleTest через FetchContent
│   ├── targets.cmake           ← цели: diffuri_core (STATIC) + Diffuri (exe)
│   └── tests.cmake             ← списки тестовых файлов + add_subdirectory(tests)
│
├── src/                        ← основной код
│   ├── main.cpp                ← точка входа приложения
│   ├── input/                  ← чтение системы ОДУ и начальных условий
│   ├── output/                 ← вывод решения
│   ├── polynomization/         ← сведение системы к полиномиальной форме
│   └── solver/                 ← решение (идея рядов Тейлора)
│       каждая папка: module.h + module.cpp
│
├── tests/                      ← тесты (два исполняемых файла)
│   ├── CMakeLists.txt          ← цели unit_tests и system_tests + gtest_discover_tests
│   ├── unit/                   ← юнит-тесты функций
│   └── systems/                ← тесты конкретных систем ОДУ
│
└── example_realization/        ← справочник (в сборку НЕ входит)
    └── input/ output/ polynomization/ solver/ global_test/
```

## Как устроена сборка

Корневой `CMakeLists.txt` не содержит логики — только подключает модули
из `Cmake/` в заданном порядке:

1. `options.cmake` — определяет `BUILD_TESTS`
2. `project.cmake` — настройки языка и пути к `src/`, `tests/`
3. `warnings.cmake` — объявляет функцию включения предупреждений
4. `sources_core.cmake` + `headers.cmake` — наполняют списки файлов
5. `dependencies.cmake` — качает GoogleTest (если тесты включены)
6. `targets.cmake` — создаёт цели:
   - **`diffuri_core`** — STATIC-библиотека из всех модулей (всё, кроме main.cpp)
   - **`Diffuri`** — исполняемый файл, линкуется с `diffuri_core`
7. `tests.cmake` — если `BUILD_TESTS=ON`, добавляет `tests/`, где создаются:
   - **`unit_tests`** — юнит-тесты отдельных функций
   - **`system_tests`** — тесты конкретных систем ОДУ

Ключевая идея: код модулей компилируется **один раз** в `diffuri_core`,
а приложение и тесты просто линкуются с ним — дублирования компиляции нет.

## Как добавлять новые файлы

| Что добавляешь | Куда прописать |
|---|---|
| Новый модуль `src/<модуль>/<модуль>.cpp` | `Cmake/sources_core.cmake` → `CORE_SOURCES` |
| Его заголовок `.h` | `Cmake/headers.cmake` |
| Юнит-тест | файл в `tests/unit/` + строка в `Cmake/tests.cmake` → `UNIT_TEST_SOURCES` |
| Тест системы ОДУ | файл в `tests/systems/` + строка в `Cmake/tests.cmake` → `SYSTEM_TEST_SOURCES` |

Больше ничего править не нужно: include-пути (`src/` — PUBLIC у
`diffuri_core`), линковка и регистрация в CTest подтягиваются сами.
В коде заголовки подключаются от корня `src/`:

```cpp
#include "polynomization/polynomization.h"
```

## Сборка и запуск

В Visual Studio проект собирается как обычно (пресеты уже настроены в
`CMakePresets.json`). Из консоли (Developer Command Prompt / PowerShell
или после запуска `vcvars64.bat`):

```bash
# Конфигурация (первый раз скачает GoogleTest — нужен интернет)
cmake -S . -B out/build/x64-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug

# Сборка
cmake --build out/build/x64-debug

# Запуск тестов
ctest --test-dir out/build/x64-debug --output-on-failure

# Запуск приложения
./out/build/x64-debug/Diffuri.exe
```

Тесты также доступны в Visual Studio через Test Explorer.

## Опции сборки

| Опция | По умолчанию | Описание |
|---|---|---|
| `BUILD_TESTS` | `ON` | Собирать `unit_tests` и `system_tests` (GoogleTest) |

Отключить тесты: `-DBUILD_TESTS=OFF` — тогда GoogleTest не скачивается
и не собирается вовсе.
