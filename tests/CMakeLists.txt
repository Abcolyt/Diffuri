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
gtest_discover_tests(unit_tests)

# Тесты конкретных систем ОДУ
add_executable(system_tests ${SYSTEM_TEST_SOURCES})
target_link_libraries(system_tests PRIVATE diffuri_core GTest::gtest_main)
gtest_discover_tests(system_tests)
