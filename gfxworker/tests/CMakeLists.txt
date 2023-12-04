add_executable(gfxworker_test_unit
               ${GFXWORKER_DIR}/tests/test_unit_main.cpp
               ${GFXWORKER_DIR}/tests/server_client_test.cpp
               ${GFXWORKER_DIR}/tests/commands_test.cpp
               ${GFXWORKER_DIR}/tests/misc_test.cpp
)

target_link_libraries(gfxworker_test_unit PRIVATE gmock gtest libgfxworker)
