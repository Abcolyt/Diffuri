# ===================================================================
# ЦЕЛИ СБОРКИ
#
# Требует: sources_core.cmake (CORE_SOURCES, MAIN_SOURCE),
#          headers.cmake (HEADERS).
# ===================================================================

# Ядро (всё, кроме main.cpp) — чтобы код был доступен и приложению, и тестам.
add_library(diffuri_core OBJECT ${CORE_SOURCES} ${HEADERS})
target_include_directories(diffuri_core PUBLIC ${DIFFURI_SRC_DIR})

# Основное приложение
add_executable(Diffuri ${MAIN_SOURCE})
target_link_libraries(Diffuri PRIVATE diffuri_core)
