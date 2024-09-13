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

# Adjust compilation flags for warnings and errors for libgfxworker
target_platform_compile_options(
    TARGET gfxworker_test_integration
    WINDOWS /W4
            /wd4100 # unreferenced formal parameter
            /wd4201 # nameless struct/union (nonstandard)
            /wd4458 # identifier hides class member
            /we4800 # Implicit conversion from 'type' to bool. Possible information loss
    UNIX $<$<CONFIG:Debug>:-ggdb3> -Wall -Wextra -Wconversion -Wno-unused-parameter
)

if(ENABLE_SDKLIB_WERROR)
    target_platform_compile_options(
        TARGET gfxworker_test_integration
        WINDOWS /WX
        UNIX  $<$<CONFIG:Debug>: -Werror>
        APPLE $<$<CONFIG:Debug>: -Wno-sign-conversion -Wno-overloaded-virtual>
    )
endif()

# prepare test data
# Integration tests require the following files to work
file(GLOB TESTING_AUX_FILES "test_data/*")
add_custom_command(
    TARGET gfxworker_test_integration POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy ${TESTING_AUX_FILES} $<TARGET_FILE_DIR:gfxworker_test_integration>
    COMMENT "Copying test files for gfxworker integration tests."
)
