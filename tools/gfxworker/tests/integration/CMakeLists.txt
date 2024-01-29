add_executable(gfxworker_test_integration
    executable_dir.h
    executable_dir.cpp
    main.cpp
    server_client_test.cpp
)

target_link_libraries(gfxworker_test_integration
    PRIVATE
    libgfxworker
    MEGA::SDKlib
)

# Look for the libraries needed for both integration and unit tests.
if(VCPKG_ROOT)
    find_package(GTest CONFIG REQUIRED)
    target_link_libraries(gfxworker_test_integration PRIVATE GTest::gtest)
else()
    pkg_check_modules(gtest REQUIRED IMPORTED_TARGET gtest)
    target_link_libraries(gfxworker_test_integration PRIVATE PkgConfig::gtest)
endif()

# prepare test data
# Integration tests require the following files to work
file(GLOB TESTING_AUX_FILES "test_data/*")
add_custom_command(
    TARGET gfxworker_test_integration POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy ${TESTING_AUX_FILES} $<TARGET_FILE_DIR:gfxworker_test_integration>
    COMMENT "Copying test files for gfxworker integration tests."
)
