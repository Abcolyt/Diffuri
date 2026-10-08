# -------------------------------------------------------------------
# ИСХОДНИКИ ОСНОВНОГО ПРИЛОЖЕНИЯ
#
# Новые .cpp-модули добавляй в CORE_SOURCES.
# main.cpp — отдельно (MAIN_SOURCE).
# -------------------------------------------------------------------

set(MAIN_SOURCE
    ${DIFFURI_SRC_DIR}/main.cpp
)

set(CORE_SOURCES
    ${DIFFURI_SRC_DIR}/input/expression.cpp
    ${DIFFURI_SRC_DIR}/input/parser.cpp
    ${DIFFURI_SRC_DIR}/input/input.cpp
    ${DIFFURI_SRC_DIR}/input/input_print_debug.cpp

    ${DIFFURI_SRC_DIR}/simplify/simplify.cpp
    ${DIFFURI_SRC_DIR}/normalize/normalize.cpp

    ${DIFFURI_SRC_DIR}/order_reducer/order_reducer.cpp
    ${DIFFURI_SRC_DIR}/pipeline/trace.cpp
    ${DIFFURI_SRC_DIR}/pipeline/runner.cpp

    ${DIFFURI_SRC_DIR}/polynomization/library.cpp
    ${DIFFURI_SRC_DIR}/polynomization/polynomization.cpp
    ${DIFFURI_SRC_DIR}/autonomize/autonomize.cpp
    ${DIFFURI_SRC_DIR}/quadratize/quadratize.cpp

    ${DIFFURI_SRC_DIR}/solver/solver.cpp
    ${DIFFURI_SRC_DIR}/solver/taylor_spec.cpp
    ${DIFFURI_SRC_DIR}/solver/taylor_table.cpp
    ${DIFFURI_SRC_DIR}/solver/convergence.cpp
    ${DIFFURI_SRC_DIR}/solver/error_control.cpp
    ${DIFFURI_SRC_DIR}/solver/step_control.cpp
    ${DIFFURI_SRC_DIR}/solver/order_control.cpp


    ${DIFFURI_SRC_DIR}/output/output.cpp

    ${DIFFURI_SRC_DIR}/cli/cli.cpp
)
