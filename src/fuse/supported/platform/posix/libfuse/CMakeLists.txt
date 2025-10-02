# Let CMake know where it can find our FindFUSE module.
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

# Make sure libfuse is present.
find_package(FUSE REQUIRED)

# Make sure 32bit UNIX platforms are building against libfuse3.
if (CMAKE_SIZEOF_VOID_P EQUAL 4 AND FUSE_VERSION_MAJOR LESS 3)
    message(FATAL_ERROR "32bit UNIX platforms must link against libfuse3.")
endif()

# Make sure dependent applications support 64bit inodes.
target_compile_definitions(SDKlib PUBLIC _FILE_OFFSET_BITS=64)

# Make sure the SDKs linked against libfuse.
target_link_libraries(SDKlib PRIVATE FUSE)

# Add version-specific libfuse sources.
add_subdirectory(${FUSE_VERSION_MAJOR})
