# libgfxwoker
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
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
)

target_link_libraries(libgfxworker
    PUBLIC
    MEGA::SDKlib
)

target_compile_definitions(libgfxworker
    PUBLIC
    UNICODE
    NOMINMAX # TODO Fix locally
)

# gfxworker executable
add_executable(gfxworker
    src/main.cpp
)

target_link_libraries(gfxworker
    PRIVATE
    libgfxworker
)

# test
if(ENABLE_TESTS)
    add_subdirectory(tests/integration)
endif()

# TODO: this is temperary solution to copy gfxworker so we can run jenkins
# and megacli
if(TARGET test_integration)
    add_custom_command(
        TARGET gfxworker POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:gfxworker> $<TARGET_FILE_DIR:test_integration>
        COMMENT "Copying gfxworker for integration tests."
    )
endif()

if(TARGET megacli)
    add_custom_command(
        TARGET gfxworker POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:gfxworker> $<TARGET_FILE_DIR:megacli>
        COMMENT "Copying gfxworker for megacli."
    )
endif()
