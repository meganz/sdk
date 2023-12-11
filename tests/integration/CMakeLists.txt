add_executable(test_integration)

target_sources(test_integration
    PRIVATE
    SdkTest_test.h
    stdfs.h
    test.h

    main.cpp
    SdkTest_test.cpp
    Sync_test.cpp
)

# Link with SDKlib
target_link_libraries(test_integration PRIVATE MEGA::SDKlib)

# Link with GoogleTest framework libraries
if(VCPKG_ROOT)
    target_link_libraries(test_integration PRIVATE GTest::gmock GTest::gtest)
else()
    target_link_libraries(test_integration PRIVATE PkgConfig::gmock PkgConfig::gtest)
endif()

## Adjust compilation flags for warnigns and errors ##
if(WIN32)

    if(ENABLE_SDKLIB_WERROR)
        target_compile_options(test_integration PRIVATE /WX)
    endif()

else()

    target_compile_options(test_integration
        PRIVATE
        $<$<CONFIG:Debug>:-ggdb3>
        -Wall         -Wextra
        -Wconversion  -Wno-unused-parameter
    )

    if(ENABLE_SDKLIB_WERROR AND CMAKE_BUILD_TYPE STREQUAL "Debug")
        target_compile_options(test_integration PRIVATE -Werror)
        if(APPLE)
            target_compile_options(test_integration
                PRIVATE
                -Wno-sign-conversion                -Wno-overloaded-virtual
                -Wno-inconsistent-missing-override  -Wno-string-conversion
                -Wno-unused-lambda-capture
            )
        endif()
    endif()

endif()

# Integration tests require the following files to work
file(GLOB TESTING_AUX_FILES "test-data/*")
add_custom_command(
    TARGET test_integration POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy ${TESTING_AUX_FILES} $<TARGET_FILE_DIR:test_integration>
    COMMENT "Copying test files for integration tests."
)
