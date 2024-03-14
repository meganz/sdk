add_library(libgfxworker
    STATIC
    src/logger.h
    src/server_win32.h
    src/processor.h
    src/thread_pool.h
    src/logger.cpp
    src/server_win32.cpp
    src/processor.cpp
    src/thread_pool.cpp
)

target_include_directories(libgfxworker
    PUBLIC
    src
)

target_link_libraries(libgfxworker
    PUBLIC
    MEGA::SDKlib
)

target_compile_definitions(libgfxworker
    PUBLIC
    UNICODE
    NOMINMAX
)

add_executable(gfxworker
    src/main.cpp
)

target_link_libraries(gfxworker
    PRIVATE
    libgfxworker
)

if(ENABLE_SDKLIB_TESTS)
    add_subdirectory(tests/integration)
endif()
