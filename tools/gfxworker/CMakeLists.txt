add_library(libgfxworker STATIC)

target_sources(libgfxworker
    PRIVATE
    src/logger.h
    src/processor.h
    src/server.h
    src/thread_pool.h
    src/logger.cpp
    src/processor.cpp
    src/thread_pool.cpp
)

target_sources_conditional(libgfxworker
    FLAG WIN32
    PRIVATE
    src/win32/server.h
    src/win32/server.cpp
)

target_sources_conditional(libgfxworker
    FLAG UNIX
    PRIVATE
    src/posix/server.h
    src/posix/server.cpp
)

target_include_directories(libgfxworker
    PUBLIC
    src
)

target_link_libraries(libgfxworker
    PUBLIC
    MEGA::SDKlib
)

if (WIN32)
    target_compile_definitions(libgfxworker
        PUBLIC
        UNICODE
    )
endif()

# Adjust compilation flags for warnings and errors for libgfxworker
target_platform_compile_options(
    TARGET libgfxworker
)

if(ENABLE_SDKLIB_WERROR)
    target_platform_compile_options(
        TARGET libgfxworker
    )
endif()

add_executable(gfxworker
    src/main.cpp
)

target_link_libraries(gfxworker
    PRIVATE
    libgfxworker
)

# Adjust compilation flags for warnings and errors for gfxworker
target_platform_compile_options(
    TARGET gfxworker
)

if(ENABLE_SDKLIB_WERROR)
    target_platform_compile_options(
        TARGET gfxworker
    )
endif()

if(ENABLE_SDKLIB_TESTS)
    add_subdirectory(tests/integration)
endif()
