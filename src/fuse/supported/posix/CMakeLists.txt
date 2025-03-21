# Convenience.
set(FUSE_POSIX_SRC ${CMAKE_CURRENT_LIST_DIR})
set(FUSE_POSIX_INC ${FUSE_POSIX_SRC}/mega/fuse/platform)

# Let CMake know where it can find our FindFUSE module.
list(APPEND CMAKE_MODULE_PATH ${FUSE_POSIX_SRC}/cmake)

# Make sure libfuse is present.
find_package(FUSE REQUIRED)

# Make sure the SDKs linked against libfuse.
target_link_libraries(${SDK_TARGET} PRIVATE FUSE)

# Let the SDK know where it can find our headers.
target_include_directories(${SDK_TARGET} PRIVATE ${FUSE_POSIX_SRC})

# Add sources required by libfuse backend.
target_sources(${SDK_TARGET} PRIVATE
                             ${FUSE_POSIX_INC}/constants.h
                             ${FUSE_POSIX_INC}/directory_context.h
                             ${FUSE_POSIX_INC}/file_descriptor.h
                             ${FUSE_POSIX_INC}/file_descriptor_forward.h
                             ${FUSE_POSIX_INC}/inode_invalidator.h
                             ${FUSE_POSIX_INC}/library.h
                             ${FUSE_POSIX_INC}/mount.h
                             ${FUSE_POSIX_INC}/mount_db.h
                             ${FUSE_POSIX_INC}/path_adapter.h
                             ${FUSE_POSIX_INC}/process.h
                             ${FUSE_POSIX_INC}/process_forward.h
                             ${FUSE_POSIX_INC}/request.h
                             ${FUSE_POSIX_INC}/request_forward.h
                             ${FUSE_POSIX_INC}/session.h
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
                             ${FUSE_POSIX_SRC}/session.cpp
                             ${FUSE_POSIX_SRC}/session_base.cpp
                             ${FUSE_POSIX_SRC}/signal.cpp
                             ${FUSE_POSIX_SRC}/unmounter.cpp
                             ${FUSE_POSIX_SRC}/utility.cpp
)

# What kind of UNIX are we building for?
string(TOLOWER ${CMAKE_SYSTEM_NAME} FUSE_UNIX_TYPE)

# Where can we find UNIX-specific sources?
set(FUSE_UNIX_SRC ${FUSE_POSIX_SRC}/${FUSE_UNIX_TYPE})
set(FUSE_UNIX_INC ${FUSE_UNIX_SRC}/mega/fuse/platform)

# Add UNIX-specific sources.
target_sources(${SDK_TARGET} PRIVATE
                             ${FUSE_UNIX_INC}/date_time.h
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

