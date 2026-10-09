# -------------------------------------------------------------------
# ЗАГОЛОВОЧНЫЕ ФАЙЛЫ (нужны, чтобы .h были видны в IDE)
#
# Новые .h добавляй сюда по мере написания модулей.
# -------------------------------------------------------------------

set(HEADERS
    ${DIFFURI_SRC_DIR}/core/expression.h
    ${DIFFURI_SRC_DIR}/input/parser.h
    ${DIFFURI_SRC_DIR}/input/input.h
    ${DIFFURI_SRC_DIR}/input/input_print_debug.h

    ${DIFFURI_SRC_DIR}/simplify/simplify.h
    ${DIFFURI_SRC_DIR}/normalize/normalize.h
    
    ${DIFFURI_SRC_DIR}/pipeline/trace.h
    ${DIFFURI_SRC_DIR}/pipeline/runner.h

    ${DIFFURI_SRC_DIR}/polynomization/function_class.h
    ${DIFFURI_SRC_DIR}/polynomization/library.h
    
    ${DIFFURI_SRC_DIR}/order_reducer/order_reducer.h
    ${DIFFURI_SRC_DIR}/autonomize/autonomize.h
    ${DIFFURI_SRC_DIR}/polynomization/polynomization.h

    ${DIFFURI_SRC_DIR}/quadratize/quadratize.h

    ${DIFFURI_SRC_DIR}/solver/solver.h
    ${DIFFURI_SRC_DIR}/solver/taylor_spec.h
    ${DIFFURI_SRC_DIR}/solver/taylor_table.h
    ${DIFFURI_SRC_DIR}/solver/convergence.h
    ${DIFFURI_SRC_DIR}/solver/error_control.h
    ${DIFFURI_SRC_DIR}/solver/step_control.h
    ${DIFFURI_SRC_DIR}/solver/order_control.h

    ${DIFFURI_SRC_DIR}/output/output.h

    ${DIFFURI_SRC_DIR}/cli/cli.h
)