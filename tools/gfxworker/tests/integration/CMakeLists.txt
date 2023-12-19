add_executable(gfxworker_test_integration
               ${GFXWORKER_DIR}/tests/integration/executable_dir.h
               ${GFXWORKER_DIR}/tests/integration/executable_dir.cpp
               ${GFXWORKER_DIR}/tests/integration/main.cpp
               ${GFXWORKER_DIR}/tests/integration/server_client_test.cpp
)

target_link_libraries(gfxworker_test_integration PRIVATE gtest libgfxworker)

# prepare test data
add_custom_command(TARGET gfxworker_test_integration POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy ${GFXWORKER_DIR}/tests/integration/test_data/logo.png ${CMAKE_BINARY_DIR}
)
