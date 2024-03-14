# Common interface target for test tools
add_library(test_tools STATIC)
add_library(MEGA::test_tools ALIAS test_tools)

target_sources(test_tools
    PRIVATE
    gtest_common.h
    sdk_test_utils.h
    stdfs.h

    gtest_common.cpp
    sdk_test_utils.cpp
)

target_compile_definitions(test_tools PRIVATE NOMINMAX)

target_link_libraries(test_tools PRIVATE MEGA::SDKlib)

target_include_directories(test_tools
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Common interface target for configurations and libraries for the SDK and MEGAchat tests.
add_library(test_common INTERFACE)
add_library(MEGA::test_common ALIAS test_common)

# Look for the libraries needed for both unit tests and integration tests for the SDK and MEGAchat.
if(VCPKG_ROOT)
    find_package(GTest CONFIG REQUIRED)
    target_link_libraries(test_common INTERFACE GTest::gmock GTest::gtest)
else()
    pkg_check_modules(gtest REQUIRED IMPORTED_TARGET gtest)
    pkg_check_modules(gmock REQUIRED IMPORTED_TARGET gmock)
    target_link_libraries(test_common INTERFACE PkgConfig::gmock PkgConfig::gtest)
endif()

if(WIN32)
    target_compile_definitions(test_common
        INTERFACE
            NOMINMAX # TODO Fix locally
    )
endif()

if(ENABLE_SDKLIB_TESTS) # This file is also loaded for MEGAchat tests.
    add_subdirectory(integration)
    add_subdirectory(unit)
endif()
