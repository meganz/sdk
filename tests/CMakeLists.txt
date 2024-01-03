
## Comon interface target for the unit and integration tests
add_library(test_common INTERFACE)

# Look for the libraries needed for both integration and unit tests.
if(VCPKG_ROOT)
    find_package(GTest CONFIG REQUIRED)
    target_link_libraries(test_common INTERFACE GTest::gmock GTest::gtest)
else()
    pkg_check_modules(gtest REQUIRED IMPORTED_TARGET gtest)
    pkg_check_modules(gmock REQUIRED IMPORTED_TARGET gmock)
    target_link_libraries(test_common INTERFACE PkgConfig::gmock PkgConfig::gtest)
endif()

if (WIN32)
    target_compile_definitions(test_common
        INTERFACE
            NOMINMAX # TODO Fix locally
    )
endif()

add_subdirectory(integration)
add_subdirectory(unit)
