# Convenience.
set(FUSE_WINDOWS_SRC ${CMAKE_CURRENT_LIST_DIR})
set(FUSE_WINDOWS_INC ${FUSE_WINDOWS_SRC}/mega/fuse/platform)

# Let CMake know where it can find our FindWinFsP module.
list(APPEND CMAKE_MODULE_PATH ${FUSE_WINDOWS_SRC}/cmake)

# Make sure WinFSP is present.
find_package(WinFSP REQUIRED)

# Make sure the SDK links against WinFSP.
target_link_libraries(${SDK_TARGET} PUBLIC WinFSP)

# Let the SDK know where it can find our headers.
target_include_directories(${SDK_TARGET} PRIVATE ${FUSE_WINDOWS_SRC})

# Add sources required by WinFSP backend.
target_sources(${SDK_TARGET} PRIVATE
                             ${FUSE_WINDOWS_INC}/constants.h
                             ${FUSE_WINDOWS_INC}/date_time.h
                             ${FUSE_WINDOWS_INC}/directory_context.h
                             ${FUSE_WINDOWS_INC}/dispatcher.h
                             ${FUSE_WINDOWS_INC}/dispatcher_forward.h
                             ${FUSE_WINDOWS_INC}/handle.h
                             ${FUSE_WINDOWS_INC}/handle_forward.h
                             ${FUSE_WINDOWS_INC}/library.h
                             ${FUSE_WINDOWS_INC}/local_pointer.h
                             ${FUSE_WINDOWS_INC}/mount.h
                             ${FUSE_WINDOWS_INC}/mount_db.h
                             ${FUSE_WINDOWS_INC}/path_adapter.h
                             ${FUSE_WINDOWS_INC}/platform.h
                             ${FUSE_WINDOWS_INC}/security_descriptor.h
                             ${FUSE_WINDOWS_INC}/security_descriptor_forward.h
                             ${FUSE_WINDOWS_INC}/security_identifier.h
                             ${FUSE_WINDOWS_INC}/security_identifier_forward.h
                             ${FUSE_WINDOWS_INC}/shell.h
                             ${FUSE_WINDOWS_INC}/utility.h
                             ${FUSE_WINDOWS_INC}/windows.h
                             ${FUSE_WINDOWS_SRC}/constants.cpp
                             ${FUSE_WINDOWS_SRC}/directory_context.cpp
                             ${FUSE_WINDOWS_SRC}/dispatcher.cpp
                             ${FUSE_WINDOWS_SRC}/local_pointer.cpp
                             ${FUSE_WINDOWS_SRC}/mount.cpp
                             ${FUSE_WINDOWS_SRC}/mount_db.cpp
                             ${FUSE_WINDOWS_SRC}/security_descriptor.cpp
                             ${FUSE_WINDOWS_SRC}/security_identifier.cpp
                             ${FUSE_WINDOWS_SRC}/service.cpp
                             ${FUSE_WINDOWS_SRC}/shell.cpp
                             ${FUSE_WINDOWS_SRC}/unmounter.cpp
                             ${FUSE_WINDOWS_SRC}/utility.cpp
)

# Are we building integration tests?
if (NOT TARGET test_integration)
    return()
endif()

set(FUSE_WINDOWS_TESTING_SRC ${FUSE_WINDOWS_SRC}/testing)
set(FUSE_WINDOWS_TESTING_INC ${FUSE_WINDOWS_INC}/testing)

target_include_directories(test_integration PRIVATE
                                            ${FUSE_WINDOWS_SRC}
)

target_sources(test_integration PRIVATE
                                ${FUSE_WINDOWS_TESTING_INC}/directory_monitor.h
                                ${FUSE_WINDOWS_TESTING_INC}/printers.h
                                ${FUSE_WINDOWS_TESTING_INC}/wrappers.h
                                ${FUSE_WINDOWS_TESTING_SRC}/directory_monitor.cpp
                                ${FUSE_WINDOWS_TESTING_SRC}/mount_tests.cpp
                                ${FUSE_WINDOWS_TESTING_SRC}/platform_tests.cpp
                                ${FUSE_WINDOWS_TESTING_SRC}/printers.cpp
                                ${FUSE_WINDOWS_TESTING_SRC}/utility.cpp
                                ${FUSE_WINDOWS_TESTING_SRC}/wrappers.cpp
)

