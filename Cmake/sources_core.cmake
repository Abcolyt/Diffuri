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

    ${DIFFURI_SRC_DIR}/output/output.cpp
    ${DIFFURI_SRC_DIR}/polynomization/polynomization.cpp
    ${DIFFURI_SRC_DIR}/solver/solver.cpp
)
