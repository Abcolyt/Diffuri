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
