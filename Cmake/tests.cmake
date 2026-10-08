# -------------------------------------------------------------------
# ТЕСТЫ
#
# Требует: targets.cmake (diffuri_core), dependencies.cmake (GoogleTest).
# Новые тестовые файлы добавляй в списки ниже — цели пересоберутся сами.
# -------------------------------------------------------------------

# Юнит-тесты отдельных функций — добавляй новые файлы сюда
set(UNIT_TEST_SOURCES
    ${DIFFURI_TESTS_DIR}/systems/test_input_print_debug.cpp
    ${DIFFURI_TESTS_DIR}/unit/test_expression.cpp
    ${DIFFURI_TESTS_DIR}/unit/test_parser.cpp
    ${DIFFURI_TESTS_DIR}/unit/test_input.cpp

    ${DIFFURI_TESTS_DIR}/unit/test_simplify.cpp
    ${DIFFURI_TESTS_DIR}/unit/test_property_simplify.cpp

    ${DIFFURI_TESTS_DIR}/unit/test_normalize.cpp
    ${DIFFURI_TESTS_DIR}/unit/test_semantic_normalize.cpp

    ${DIFFURI_TESTS_DIR}/unit/test_order_reducer.cpp
    ${DIFFURI_TESTS_DIR}/unit/test_pipeline_trace.cpp
    ${DIFFURI_TESTS_DIR}/unit/test_pipeline_runner.cpp    

    ${DIFFURI_TESTS_DIR}/unit/test_function_class.cpp
    ${DIFFURI_TESTS_DIR}/unit/test_library.cpp
    ${DIFFURI_TESTS_DIR}/unit/test_autonomize.cpp
    ${DIFFURI_TESTS_DIR}/unit/test_polynomization.cpp

    ${DIFFURI_TESTS_DIR}/unit/test_quadratize.cpp


    ${DIFFURI_TESTS_DIR}/unit/test_taylor_spec.cpp
    ${DIFFURI_TESTS_DIR}/unit/test_taylor_table.cpp
    ${DIFFURI_TESTS_DIR}/unit/test_convergence.cpp
    ${DIFFURI_TESTS_DIR}/unit/test_error_control.cpp
    ${DIFFURI_TESTS_DIR}/unit/test_step_control.cpp
    ${DIFFURI_TESTS_DIR}/unit/test_order_control.cpp
    ${DIFFURI_TESTS_DIR}/unit/test_solver.cpp

    ${DIFFURI_TESTS_DIR}/unit/test_cli.cpp
)

# Тесты конкретных систем ОДУ — добавляй новые файлы сюда
set(SYSTEM_TEST_SOURCES
    ${DIFFURI_TESTS_DIR}/systems/test_system_1.cpp
    ${DIFFURI_TESTS_DIR}/systems/test_solver_systems.cpp
)

# Регистрация целей тестов
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
