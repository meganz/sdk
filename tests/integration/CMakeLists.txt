add_executable(test_integration)

target_sources(test_integration
    PRIVATE
    SdkTest_test.h
    stdfs.h
    test.h
    sdk_test_utils.h
    ../gtest_common.h

    main.cpp
    SdkTest_test.cpp
    Sync_test.cpp
    sdk_test_utils.cpp
    ../gtest_common.cpp
)

target_include_directories(test_integration
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../
)

# Link with SDKlib
target_link_libraries(test_integration PRIVATE MEGA::SDKlib)

# Link with the common interface library for the tests.
target_link_libraries(test_integration PRIVATE test_common)

# Adjust compilation flags for warnings and errors
target_platform_compile_options(
    TARGET test_integration
    UNIX $<$<CONFIG:Debug>:-ggdb3> -Wall -Wextra -Wconversion -Wno-unused-parameter
)

if(ENABLE_SDKLIB_WERROR)
    target_platform_compile_options(
        TARGET test_integration
        WINDOWS /WX
        UNIX  $<$<CONFIG:Debug>: -Werror>
        APPLE $<$<CONFIG:Debug>: -Wno-sign-conversion -Wno-overloaded-virtual -Wno-inconsistent-missing-override
                                 -Wno-string-conversion -Wno-unused-lambda-capture>
    )
endif()

# Integration tests require the following files to work
file(GLOB TESTING_AUX_FILES "test-data/*")
add_custom_command(
    TARGET test_integration POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy ${TESTING_AUX_FILES} $<TARGET_FILE_DIR:test_integration>
    COMMENT "Copying test files for integration tests."
)

if(WIN32)
    add_custom_command(
        TARGET test_integration POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:gfxworker> $<TARGET_FILE_DIR:test_integration>
        COMMENT "Copying gfxworker for integration tests."
    )
endif()
