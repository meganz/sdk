find_path(FUSE_INCLUDE_DIR
          fuse_common.h
          HINTS
          $ENV{FUSE_PREFIX}
          PATH_SUFFIXES
          include/fuse3
          include/fuse
)

find_library(FUSE_LIBRARY
             libfuse3.so
             libfuse.dylib
             libfuse.so
             HINTS
             $ENV{FUSE_PREFIX}
             PATH_SUFFIXES
             lib
)

if (FUSE_INCLUDE_DIR AND FUSE_LIBRARY)
    find_package(Threads)

    set(FUSE_DEFINITIONS -D_FILE_OFFSET_BITS=64)

    if (CMAKE_VERSION VERSION_GREATER_EQUAL "3.20.0")
        cmake_path(GET FUSE_INCLUDE_DIR PARENT_PATH FUSE_INCLUDE_DIRS)
    else()
        get_filename_component(FUSE_INCLUDE_DIRS "${FUSE_INCLUDE_DIR}" DIRECTORY)
    endif()

    set(FUSE_LIBRARIES ${CMAKE_THREAD_LIBS_INIT} ${FUSE_LIBRARY})

    if (NOT TARGET FUSE)
        add_library(FUSE UNKNOWN IMPORTED)

        set_target_properties(
          FUSE
          PROPERTIES
          IMPORTED_LOCATION ${FUSE_LIBRARY}
        )

        if (Threads_FOUND)
            target_link_libraries(FUSE INTERFACE Threads::Threads)
        endif()

        target_compile_definitions(FUSE INTERFACE ${FUSE_DEFINITIONS})
        target_include_directories(FUSE INTERFACE ${FUSE_INCLUDE_DIRS})
    endif()

    # Assume we've found libfuse 3.x.
    set(FUSE_VERSION_PATH "${FUSE_INCLUDE_DIR}/libfuse_config.h")

    # We've actually found libfuse 2.x.
    if (NOT EXISTS "${FUSE_VERSION_PATH}")
        set(FUSE_VERSION_PATH "${FUSE_INCLUDE_DIR}/fuse_common.h")
    endif()

    # Read the version file.
    file(READ "${FUSE_VERSION_PATH}" CONTENT)

    # Parse version.
    string(REGEX REPLACE ".*#define FUSE_MAJOR_VERSION +([0-9]+).*$"
                         "\\1"
                         FUSE_VERSION_MAJOR
                         ${CONTENT}
    )

    string(REGEX REPLACE ".*define FUSE_MINOR_VERSION +([0-9]+).*$"
                         "\\1"
                         FUSE_VERSION_MINOR
                         ${CONTENT}
    )

    # Latch full version.
    set(FUSE_VERSION "${FUSE_VERSION_MAJOR}.${FUSE_VERSION_MINOR}")

    # Cleanup after ourselves.
    unset(CONTENT)
    unset(FUSE_VERSION_PATH)
endif()

include(FindPackageHandleStandardArgs)

find_package_handle_standard_args(
  FUSE
  REQUIRED_VARS
  FUSE_INCLUDE_DIR
  FUSE_LIBRARY
  VERSION_VAR
  FUSE_VERSION
)

mark_as_advanced(FUSE_INCLUDE_DIR FUSE_LIBRARY)

