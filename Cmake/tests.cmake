# -------------------------------------------------------------------
# ТЕСТЫ
#
# Требует: targets.cmake (diffuri_core), dependencies.cmake (GoogleTest).
# Новые тестовые файлы добавляй в списки ниже — цели пересоберутся сами.
# -------------------------------------------------------------------

# Юнит-тесты отдельных функций — добавляй новые файлы сюда
set(UNIT_TEST_SOURCES
    ${DIFFURI_TESTS_DIR}/unit/test_expression.cpp
    ${DIFFURI_TESTS_DIR}/unit/test_parser.cpp
    ${DIFFURI_TESTS_DIR}/unit/test_input.cpp
    ${DIFFURI_TESTS_DIR}/systems/test_input_print_debug.cpp

    ${DIFFURI_TESTS_DIR}/unit/test_polynomization.cpp
    ${DIFFURI_TESTS_DIR}/unit/test_solver.cpp
)

# Тесты конкретных систем ОДУ — добавляй новые файлы сюда
set(SYSTEM_TEST_SOURCES
    ${DIFFURI_TESTS_DIR}/systems/test_system_1.cpp
)

# Регистрация целей тестов
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
