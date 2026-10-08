# ===================================================================
# tests/systems/cli/cli_smoke.cmake
#
# Дымовой E2E-тест CLI (ТЗ №3.1). Запускается через `cmake -P`.
# Запускает собранный бинарник Diffuri как подпроцесс, подаёт систему
# на stdin, передаёт --trajectory PATH, проверяет exit-код и файл.
#
# Ожидает в -D:
#   EXE        — путь к бинарнику Diffuri
#   INPUT      — путь к cli_input.txt
#   OUT_CSV    — путь, куда бинарник должен записать траекторию
#   INPUT_AUX     — путь к cli_input_aux.txt   (опционально)
#   OUT_CSV_AUX   — путь для CSV второго прогона (опционально)
# ===================================================================

if(NOT EXISTS "${EXE}")
    message(FATAL_ERROR "Diffuri executable not found: ${EXE}")
endif()
if(NOT EXISTS "${INPUT}")
    message(FATAL_ERROR "CLI input not found: ${INPUT}")
endif()

file(REMOVE "${OUT_CSV}")

execute_process(
    COMMAND "${EXE}" --trajectory "${OUT_CSV}" 1.0 20 1e-3
    INPUT_FILE "${INPUT}"
    RESULT_VARIABLE rc
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE  stderr
    TIMEOUT 30
)

if(NOT rc EQUAL 0)
    message(FATAL_ERROR
        "CLI exit code ${rc}\n--- stdout ---\n${stdout}\n--- stderr ---\n${stderr}")
endif()

if(NOT EXISTS "${OUT_CSV}")
    message(FATAL_ERROR
        "Trajectory file was not created: ${OUT_CSV}\n--- stderr ---\n${stderr}")
endif()

file(READ "${OUT_CSV}" csv_content)
if(csv_content STREQUAL "")
    message(FATAL_ERROR "Trajectory file is empty: ${OUT_CSV}")
endif()

# Первая строка — заголовок CSV: должен начинаться с 't,'.
string(FIND "${csv_content}" "t," header_pos)
if(NOT header_pos EQUAL 0)
    message(FATAL_ERROR
        "Unexpected CSV header (expected to start with 't,'):\n${csv_content}")
endif()

# ===================================================================
# Второй прогон: система с вспомогательными переменными Quadratize.
# Проверяем, что служебные q_ не попадают в заголовок CSV.
# Параметры -DINPUT_AUX / -DOUT_CSV_AUX передаются из tests.cmake.
# Если они не заданы — секция просто не выполняется.
# ===================================================================
if(DEFINED INPUT_AUX AND DEFINED OUT_CSV_AUX)
    if(NOT EXISTS "${INPUT_AUX}")
        message(FATAL_ERROR "CLI aux input not found: ${INPUT_AUX}")
    endif()

    file(REMOVE "${OUT_CSV_AUX}")

    execute_process(
        COMMAND "${EXE}" --trajectory "${OUT_CSV_AUX}" 0.3 20 1e-3
        INPUT_FILE "${INPUT_AUX}"
        RESULT_VARIABLE rc_aux
        OUTPUT_VARIABLE stdout_aux
        ERROR_VARIABLE  stderr_aux
        TIMEOUT 30
    )

    if(NOT rc_aux EQUAL 0)
        message(FATAL_ERROR
            "CLI (aux) exit code ${rc_aux}\n--- stdout ---\n${stdout_aux}\n--- stderr ---\n${stderr_aux}")
    endif()

    if(NOT EXISTS "${OUT_CSV_AUX}")
        message(FATAL_ERROR
            "Trajectory file (aux) was not created: ${OUT_CSV_AUX}\n--- stderr ---\n${stderr_aux}")
    endif()

    file(READ "${OUT_CSV_AUX}" csv_aux)
    if(csv_aux STREQUAL "")
        message(FATAL_ERROR "Trajectory file (aux) is empty: ${OUT_CSV_AUX}")
    endif()

    # Первая строка — заголовок. CMake-регекс [^\n] на Windows ловит
    # и '\r' тоже, поэтому после MATCH применяем STRIP: он срежет
    # ведущие/хвостовые пробельные символы, включая CR/LF.
    string(REGEX MATCH "^[^\n]*" header_aux_raw "${csv_aux}")
    string(STRIP "${header_aux_raw}" header_aux)

    # Ожидаем ровно "t,x" — исходная функция одна, вспомогательных быть не должно.
    if(NOT header_aux STREQUAL "t,x")
        message(FATAL_ERROR
            "Unexpected CSV header for aux system (expected 't,x'): '${header_aux}'")
    endif()

    # Дополнительный барьер: 'q_' не должно быть нигде в файле.
    string(FIND "${csv_aux}" "q_" q_pos)
    if(NOT q_pos EQUAL -1)
        message(FATAL_ERROR
            "Auxiliary column 'q_' leaked into CSV:\n${csv_aux}")
    endif()
endif()