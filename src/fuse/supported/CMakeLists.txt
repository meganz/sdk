# Convenience.
set(FUSE_SUPPORTED_SRC ${CMAKE_CURRENT_LIST_DIR})
set(FUSE_SUPPORTED_INC ${FUSE_SUPPORTED_SRC}/mega/fuse/platform)

# Let the SDK know where it can find common platform headers.
target_include_directories(${SDK_TARGET} PRIVATE ${FUSE_SUPPORTED_SRC})

# Add sources required by all concrete backends.
target_sources(${SDK_TARGET} PRIVATE
                             ${FUSE_SUPPORTED_INC}/context_forward.h
                             ${FUSE_SUPPORTED_INC}/context.h
                             ${FUSE_SUPPORTED_INC}/file_context_forward.h
                             ${FUSE_SUPPORTED_INC}/file_context.h
                             ${FUSE_SUPPORTED_INC}/mount_db_forward.h
                             ${FUSE_SUPPORTED_INC}/mount_forward.h
                             ${FUSE_SUPPORTED_INC}/path_adapter_forward.h
                             ${FUSE_SUPPORTED_INC}/service_context_forward.h
                             ${FUSE_SUPPORTED_INC}/service_context.h
                             ${FUSE_SUPPORTED_INC}/unmounter.h
                             ${FUSE_SUPPORTED_SRC}/context.cpp
                             ${FUSE_SUPPORTED_SRC}/file_context.cpp
                             ${FUSE_SUPPORTED_SRC}/service_context.cpp
                             ${FUSE_SUPPORTED_SRC}/service.cpp
                             ${FUSE_SUPPORTED_SRC}/unmounter.cpp
)

# Which backend are we building?
if (UNIX)
    # libfuse backend.
    include(${FUSE_SUPPORTED_SRC}/posix/CMakeLists.txt)
else()
    # No backend exists for Windows yet.
    message(FATAL_ERROR "FUSE support is not yet implemented for Windows")
endif()

# Are we building integration tests?
if (NOT TARGET test_integration)
    return()
endif()

target_include_directories(test_integration PRIVATE
                                            ${FUSE_SUPPORTED_SRC}
)

set(FUSE_SUPPORTED_TESTING_SRC ${FUSE_SUPPORTED_SRC}/testing)
set(FUSE_SUPPORTED_TESTING_INC ${FUSE_SUPPORTED_INC}/testing)

target_sources(test_integration PRIVATE
                                ${FUSE_SUPPORTED_TESTING_INC}/platform_tests.h
                                ${FUSE_SUPPORTED_TESTING_SRC}/platform_tests.cpp
)

