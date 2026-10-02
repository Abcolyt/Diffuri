# -------------------------------------------------------------------
# ЗАГОЛОВОЧНЫЕ ФАЙЛЫ (нужны, чтобы .h были видны в IDE)
#
# Новые .h добавляй сюда по мере написания модулей.
# -------------------------------------------------------------------

set(HEADERS
    ${DIFFURI_SRC_DIR}/input/expression.h
    ${DIFFURI_SRC_DIR}/input/parser.h
    ${DIFFURI_SRC_DIR}/input/input.h
    ${DIFFURI_SRC_DIR}/input/input_print_debug.h

    ${DIFFURI_SRC_DIR}/simplify/simplify.h
    ${DIFFURI_SRC_DIR}/normalize/normalize.h
    
    ${DIFFURI_SRC_DIR}/order_reducer/order_reducer.h
    ${DIFFURI_SRC_DIR}/pipeline/trace.h
    ${DIFFURI_SRC_DIR}/pipeline/runner.h

    ${DIFFURI_SRC_DIR}/polynomization/function_class.h
    ${DIFFURI_SRC_DIR}/polynomization/library.h
    ${DIFFURI_SRC_DIR}/polynomization/polynomization.h

    ${DIFFURI_SRC_DIR}/solver/solver.h

    ${DIFFURI_SRC_DIR}/output/output.h

)