# ===================================================================
# Diffuri — корневой файл сборки.
#
# Никакой логики здесь нет: вся сборка разложена по модулям
# в Cmake/*.cmake, здесь задан только порядок их подключения
# (порядок важен: модули используют переменные и цели друг друга).
# ===================================================================
cmake_minimum_required(VERSION 3.20) # обязательна в самом начале

include(Cmake/project.cmake)      # project(), стандарт C++, пути проекта
include(Cmake/options.cmake)      # опции сборки (BUILD_TESTS и др.)
include(Cmake/sources_core.cmake) # списки исходников основного кода
include(Cmake/headers.cmake)      # заголовки (для IDE)
include(Cmake/dependencies.cmake) # зависимости (GoogleTest)
include(Cmake/targets.cmake)      # цели: diffuri_core, Diffuri
include(Cmake/tests.cmake)        # тесты: unit_tests, system_tests
