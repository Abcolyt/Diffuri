# -------------------------------------------------------------------
# ЗАВИСИМОСТИ
# -------------------------------------------------------------------

# GoogleTest — нужен только для тестов (подключается при BUILD_TESTS=ON).
if(BUILD_TESTS)
    include(FetchContent)

    # MSVC: использовать тот же рантайм (CRT), что и у остального проекта
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.14.0
    )
    FetchContent_MakeAvailable(googletest)
endif()

# -------------------------------------------------------------------
# Если понадобятся другие библиотеки — подключай их здесь, например:
#
#   include(FetchContent)
#   FetchContent_Declare(
#       <имя>
#       GIT_REPOSITORY https://github.com/<org>/<repo>.git
#       GIT_TAG        <тег>
#   )
#   FetchContent_MakeAvailable(<имя>)
# -------------------------------------------------------------------
