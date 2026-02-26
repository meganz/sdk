set(VCPKG_POLICY_EMPTY_PACKAGE enabled)

if(VCPKG_OSX_SYSROOT STREQUAL "iphonesimulator")
    set(target_environment "iPhoneSimulator")
else()
    set(target_environment "iPhoneOS")
endif()

message(NOTICE "Creating .pc file for ${PORT}, pointing to the zlib provided by the system in the ${target_environment} sysroot/SDK")

configure_file("${CMAKE_CURRENT_LIST_DIR}/zlib.pc.in" "${CURRENT_PACKAGES_DIR}/debug/lib/pkgconfig/zlib.pc" @ONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/zlib.pc.in" "${CURRENT_PACKAGES_DIR}/lib/pkgconfig/zlib.pc" @ONLY)
