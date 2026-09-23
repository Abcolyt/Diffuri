# ===================================================================
# Diffuri — корневой файл сборки.
#
# Никакой логики здесь нет: вся сборка разложена по модулям
# в Cmake/*.cmake, здесь задан только порядок их подключения
# (порядок важен: модули используют переменные и цели друг друга).
# ===================================================================
cmake_minimum_required(VERSION 3.20) # обязательна в самом начале

# Политики, которые использует VS-овский SegmentHeap.cmake
# (подключается через CMAKE_PROJECT_TOP_LEVEL_INCLUDES в пресете).
# Должны быть выставлены до project() — иначе CMake загрузит
# SegmentHeap.cmake раньше, чем политики применятся.
if(POLICY CMP0057)
  cmake_policy(SET CMP0057 NEW)
endif()

# project() обязан быть прямо здесь, в верхнем файле — CMake это требует.
project(Diffuri LANGUAGES CXX)

include(Cmake/options.cmake)      # опции сборки (BUILD_TESTS и др.)
include(Cmake/project.cmake)      # стандарт C++, пути проекта, MSVC-политики
include(Cmake/warnings.cmake)     # функция включения предупреждений компилятора
include(Cmake/sources_core.cmake) # списки исходников основного кода
include(Cmake/headers.cmake)      # заголовки (для IDE)
include(Cmake/dependencies.cmake) # зависимости (GoogleTest)
include(Cmake/targets.cmake)      # цели: diffuri_core, Diffuri
include(Cmake/tests.cmake)        # тесты: unit_tests, system_tests