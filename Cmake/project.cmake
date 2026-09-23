# ===================================================================
# ПРОЕКТ: имя, политики, стандарт языка, пути
# ===================================================================

project(Diffuri LANGUAGES CXX)

# Включение горячей перезагрузки для компиляторов MSVC, если поддерживается.
if(POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Пути проекта (используются всеми остальными модулями)
set(DIFFURI_SRC_DIR   ${CMAKE_CURRENT_SOURCE_DIR}/src)
set(DIFFURI_TESTS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/tests)
