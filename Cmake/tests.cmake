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
    ${DIFFURI_TESTS_DIR}/systems/test_solver_systems.cpp
    ${DIFFURI_TESTS_DIR}/systems/test_trajectory.cpp 

)

# Регистрация целей тестов
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)

    # --- CLI E2E дымовой тест -----------------------------
    # Запускает собранный Diffuri как подпроцесс через `cmake -P`.
    # Артефакты теста лежат в tests/systems/cli/.
    # add_dependencies не нужен: $<TARGET_FILE:Diffuri> резолвится на
    # этапе генерации, ctest стартует уже после `cmake --build`.
    # OUT_CSV кладётся в верхний build-dir (CMAKE_CURRENT_BINARY_DIR
    # здесь = build-root, т.к. файл include()-ится из корня).
    add_test(
        NAME cli_smoke
        COMMAND ${CMAKE_COMMAND}
            -DEXE=$<TARGET_FILE:Diffuri>
            -DINPUT=${DIFFURI_TESTS_DIR}/systems/cli/cli_input.txt
            -DOUT_CSV=${CMAKE_CURRENT_BINARY_DIR}/cli_smoke_traj.csv
            -DINPUT_AUX=${DIFFURI_TESTS_DIR}/systems/cli/cli_input_aux.txt
            -DOUT_CSV_AUX=${CMAKE_CURRENT_BINARY_DIR}/cli_smoke_traj_aux.csv
            -P ${DIFFURI_TESTS_DIR}/systems/cli/cli_smoke.cmake
    )
endif()