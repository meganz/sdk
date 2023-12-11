
# Look for the libraries needed for both integration and unit tests.
if(VCPKG_ROOT)
    find_package(GTest CONFIG REQUIRED)
else()
    pkg_check_modules(gtest REQUIRED IMPORTED_TARGET gtest)
    pkg_check_modules(gmock REQUIRED IMPORTED_TARGET gmock)
endif()

add_subdirectory(integration)
add_subdirectory(unit)
