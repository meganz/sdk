# Convenience.
set(FUSE_POSIX_SRC ${CMAKE_CURRENT_LIST_DIR})
set(FUSE_POSIX_INC ${FUSE_POSIX_SRC}/mega/fuse/platform)

# Let CMake know where it can find our FindFUSE module.
list(APPEND CMAKE_MODULE_PATH ${FUSE_POSIX_SRC}/cmake)

# Make sure libfuse is present.
find_package(FUSE REQUIRED)

# Make sure 32bit UNIX platforms are building against libfuse3.
if (CMAKE_SIZEOF_VOID_P EQUAL 4 AND FUSE_VERSION_MAJOR LESS 3)
    message(FATAL_ERROR "32bit UNIX platforms must link against libfuse3.")
endif()

# Convenience.
set(CAPABILITY_HEADER_PATH "${PROJECT_BINARY_DIR}/mega/fuse/platform")
set(CAPABILITY_HEADER_FILE "${CAPABILITY_HEADER_PATH}/capability_flags.i")

# Do we need to generate a list of known capability flags?
if (NOT EXISTS "${CAPABILITY_HEADER_FILE}")
    # Convenience.
    set(CAPABILITY_PATTERN "^#define (FUSE_CAP_[^ \t]+).*$")

    # Extract capability flag definitions.
    file(STRINGS "${FUSE_INCLUDE_DIR}/fuse_common.h"
                 CAPABILITY_DEFINITIONS
                 REGEX "${CAPABILITY_PATTERN}")

    # Create mega/fuse/platform if necessary.
    file(MAKE_DIRECTORY "${CAPABILITY_HEADER_PATH}")

    # Write capability flag entries to a header.
    foreach (CAPABILITY_DEFINITION ${CAPABILITY_DEFINITIONS})
        # Generate an entry for this capability flag.
        string(REGEX REPLACE "${CAPABILITY_PATTERN}"
                             "ENTRY(\\1)\n"
                             CAPABILITY_ENTRY
                             "${CAPABILITY_DEFINITION}")

        # Write the entry to our header.
        file(APPEND "${CAPABILITY_HEADER_FILE}" "${CAPABILITY_ENTRY}")
    endforeach()

    # Cleanup.
    unset(CAPABILITY_ENTRY)
    unset(CAPABILITY_DEFINITION)
    unset(CAPABILITY_DEFINITIONS)
    unset(CAPABILITY_PATTERN)
endif()

# Cleanup.
unset(CAPABILITY_HEADER_FILE)
unset(CAPABILITY_HEADER_PATH)

# Convenience.
set(FUSE_LIBFUSE_SRC ${FUSE_POSIX_SRC}/libfuse/${FUSE_VERSION_MAJOR})
set(FUSE_LIBFUSE_INC ${FUSE_LIBFUSE_SRC}/mega/fuse/platform)

# Make sure the SDKs linked against libfuse.
target_link_libraries(${SDK_TARGET} PRIVATE FUSE)

# Let the SDK know where it can find our headers.
target_include_directories(${SDK_TARGET} PRIVATE
                                         ${FUSE_LIBFUSE_SRC}
                                         ${FUSE_POSIX_SRC}
)

# Apple and 32bit UNIX systems have a time_t distinct time m_time_t.
if (APPLE OR CMAKE_SIZEOF_VOID_P EQUAL 4)
    target_compile_definitions(${SDK_TARGET} PUBLIC HAS_DISTINCT_TIME_T)
endif()

# Make sure dependent applications support 64bit inodes.
target_compile_definitions(${SDK_TARGET} PUBLIC _FILE_OFFSET_BITS=64)

# Check if the target supports the O_PATH flag.
try_compile(HAS_OPEN_PATH
            "${CMAKE_BINARY_DIR}"
            "${FUSE_POSIX_SRC}/cmake/checks/has_open_path.cpp")

if (HAS_OPEN_PATH)
    target_compile_definitions(${SDK_TARGET} PUBLIC HAS_OPEN_PATH)
endif()

# Check if the target supports the RENAME_(EXCHANGE|NOREPLACE) flags.
try_compile(HAS_RENAME_FLAGS
            "${CMAKE_BINARY_DIR}"
            "${FUSE_POSIX_SRC}/cmake/checks/has_rename_flags.cpp")

if (HAS_RENAME_FLAGS)
    target_compile_definitions(${SDK_TARGET} PUBLIC HAS_RENAME_FLAGS)
endif()

# Add sources required by libfuse backend.
target_sources(${SDK_TARGET} PRIVATE
                             ${FUSE_POSIX_INC}/constants.h
                             ${FUSE_POSIX_INC}/date_time.h
                             ${FUSE_POSIX_INC}/directory_context.h
                             ${FUSE_POSIX_INC}/file_descriptor.h
                             ${FUSE_POSIX_INC}/file_descriptor_forward.h
                             ${FUSE_POSIX_INC}/inode_invalidator.h
                             ${FUSE_POSIX_INC}/mount.h
                             ${FUSE_POSIX_INC}/mount_db.h
                             ${FUSE_POSIX_INC}/path_adapter.h
                             ${FUSE_POSIX_INC}/process.h
                             ${FUSE_POSIX_INC}/process_forward.h
                             ${FUSE_POSIX_INC}/request.h
                             ${FUSE_POSIX_INC}/request_forward.h
                             ${FUSE_POSIX_INC}/session_base.h
                             ${FUSE_POSIX_INC}/session_forward.h
                             ${FUSE_POSIX_INC}/signal.h
                             ${FUSE_POSIX_INC}/utility.h
                             ${FUSE_POSIX_SRC}/constants.cpp
                             ${FUSE_POSIX_SRC}/directory_context.cpp
                             ${FUSE_POSIX_SRC}/file_descriptor.cpp
                             ${FUSE_POSIX_SRC}/inode_invalidator.cpp
                             ${FUSE_POSIX_SRC}/mount.cpp
                             ${FUSE_POSIX_SRC}/mount_db.cpp
                             ${FUSE_POSIX_SRC}/process.cpp
                             ${FUSE_POSIX_SRC}/request.cpp
                             ${FUSE_POSIX_SRC}/service.cpp
                             ${FUSE_POSIX_SRC}/session_base.cpp
                             ${FUSE_POSIX_SRC}/signal.cpp
                             ${FUSE_POSIX_SRC}/unmounter.cpp
                             ${FUSE_POSIX_SRC}/utility.cpp
)

# Add version-specific libfuse sources.
target_sources(${SDK_TARGET} PRIVATE
                             ${FUSE_LIBFUSE_INC}/library.h
                             ${FUSE_LIBFUSE_INC}/session.h
                             ${FUSE_LIBFUSE_SRC}/session.cpp
                             ${FUSE_LIBFUSE_SRC}/session_base.cpp
)

# What kind of UNIX are we building for?
string(TOLOWER ${CMAKE_SYSTEM_NAME} FUSE_UNIX_TYPE)

# Where can we find UNIX-specific sources?
set(FUSE_UNIX_SRC ${FUSE_POSIX_SRC}/${FUSE_UNIX_TYPE})
set(FUSE_UNIX_INC ${FUSE_UNIX_SRC}/mega/fuse/platform)

# Add UNIX-specific sources.
target_sources(${SDK_TARGET} PRIVATE
                             ${FUSE_UNIX_INC}/platform.h
                             ${FUSE_UNIX_SRC}/utility.cpp
)

target_include_directories(${SDK_TARGET} PRIVATE ${FUSE_UNIX_SRC})

# Are we building integration tests?
if (NOT TARGET test_integration)
    return()
endif()

set(FUSE_POSIX_TESTING_SRC ${FUSE_POSIX_SRC}/testing)
set(FUSE_POSIX_TESTING_INC ${FUSE_POSIX_INC}/testing)

set(FUSE_UNIX_TESTING_INC ${FUSE_UNIX_INC}/testing)
set(FUSE_UNIX_TESTING_SRC ${FUSE_UNIX_SRC}/testing)

target_include_directories(test_integration PRIVATE
                                            ${FUSE_POSIX_SRC}
                                            ${FUSE_UNIX_SRC}
)

target_sources(test_integration PRIVATE
                                ${FUSE_POSIX_TESTING_INC}/printers.h
                                ${FUSE_POSIX_TESTING_INC}/wrappers.h
                                ${FUSE_POSIX_TESTING_SRC}/mount_tests.cpp
                                ${FUSE_POSIX_TESTING_SRC}/platform_tests.cpp
                                ${FUSE_POSIX_TESTING_SRC}/printers.cpp
                                ${FUSE_POSIX_TESTING_SRC}/utility.cpp
                                ${FUSE_POSIX_TESTING_SRC}/wrappers.cpp
)

if (EXISTS ${FUSE_UNIX_SRC}/platform_tests.cpp)
    target_sources(test_integration PRIVATE
                                    ${FUSE_UNIX_SRC}/platform_tests.cpp
    )
endif()

