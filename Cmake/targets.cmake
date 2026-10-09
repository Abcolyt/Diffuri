# ===================================================================
# ЦЕЛИ СБОРКИ
#
# Требует: sources_core.cmake (CORE_SOURCES, MAIN_SOURCE),
#          headers.cmake (HEADERS), warnings.cmake.
# ===================================================================

# Ядро (всё, кроме main.cpp) — чтобы код был доступен и приложению, и тестам.
add_library(diffuri_core STATIC ${CORE_SOURCES} ${HEADERS})
target_include_directories(diffuri_core PUBLIC ${DIFFURI_SRC_DIR})
diffuri_enable_warnings(diffuri_core)

# Основное приложение
add_executable(Diffuri ${MAIN_SOURCE})
target_link_libraries(Diffuri PRIVATE diffuri_core)
diffuri_enable_warnings(Diffuri)
 
# ============================================================================
# Копирование data/constants.txt рядом с экзешником
#
# Источник правды — ${CMAKE_SOURCE_DIR}/data/constants.txt.
# При каждой сборке файл копируется рядом с бинарником, но только если его
# содержимое отличается от копии (команда `copy_if_different`). Это значит:
#   - правки исходника в репозитории автоматически попадают к экзешнику;
#   - правки КОПИИ рядом с экзешником будут перезатёрты при следующей
#     сборке (так задумано: источник правды — файл в репозитории).
#
# Парсер ищет файл относительно рабочей директории (CWD). Если запускать
# экзешник из папки сборки (например, из отладчика или двойным кликом),
# файл найдётся как out/build/x64-debug/data/constants.txt.
# ============================================================================
add_custom_command(TARGET Diffuri POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/data/constants.txt"
        "$<TARGET_FILE_DIR:Diffuri>/data/constants.txt"
    COMMENT "Copying data/constants.txt next to Diffuri executable"
    VERBATIM
)



# ============================================================================
# Установка финального продукта (дистрибутив)
#
# Команда для запуска:
#   cmake --install out/build/x64-release --prefix ./dist
#
# Результат — чистая папка только с нужными файлами:
#   dist/
#   ├── Diffuri.exe
#   └── data/
#       └── constants.txt
#
# Без тестов, промежуточных .obj/.pdb/.ilk, кэша CMake и _deps.
# ============================================================================

install(TARGETS Diffuri
    RUNTIME DESTINATION .
)

install(FILES ${CMAKE_SOURCE_DIR}/data/constants.txt
    DESTINATION data
)