# ===================================================================
# ТЕСТЫ
#
# Списки тестовых файлов лежат в Cmake/tests.cmake — новые тесты
# добавляй туда, здесь только сборка и регистрация целей.
# ===================================================================

include(GoogleTest)

# Юнит-тесты отдельных функций
add_executable(unit_tests ${UNIT_TEST_SOURCES})
target_link_libraries(unit_tests PRIVATE diffuri_core GTest::gtest_main)

# Большой стек: Simplify рекурсивна, а DeepRecursion-тесты строят
# деревья глубиной в тысячи узлов. Дефолтный 1 MB на MSVC-x64
# переполняется (Stack Overflow).
if(MSVC)
    target_link_options(unit_tests PRIVATE /STACK:8388608)        # 8 MB
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_link_options(unit_tests PRIVATE -Wl,-z,stacksize=8388608)
endif()

gtest_discover_tests(unit_tests)

# Тесты конкретных систем ОДУ
add_executable(system_tests ${SYSTEM_TEST_SOURCES})
target_link_libraries(system_tests PRIVATE diffuri_core GTest::gtest_main)
gtest_discover_tests(system_tests)