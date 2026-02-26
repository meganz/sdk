# Assume the user hasn't specified where to find WinFSP.
if (NOT DEFINED WinFSP_PREFIX)
    # Search WinFSP's default installation path.
    set(WinFSP_PREFIX "C:/Program Files (x86)/WinFSP"
                      CACHE PATH
                      "The path where WinFSP is installed"
                      FORCE)
endif()

find_path(
  WinFSP_INCLUDE_DIR
  winfsp.h
  HINTS
  ${WinFSP_PREFIX}
  PATH_SUFFIXES
  inc/winfsp
)

# Assume we're targeting 64bit machines.
set(WinFSP_LIBRARY_SUFFIX x64)

# Really targeting 32bit machines.
if (CMAKE_VS_PLATFORM_NAME MATCHES "Win32")
    set(WinFSP_LIBRARY_SUFFIX x86)
endif() 

set(WinFSP_LIBRARY_NAME "winfsp-${WinFSP_LIBRARY_SUFFIX}")

find_library(
  WinFSP_LIBRARY
  ${WinFSP_LIBRARY_NAME}.lib
  HINTS
  ${WinFSP_PREFIX}
  PATH_SUFFIXES
  lib
)

if (WinFSP_INCLUDE_DIR AND WinFSP_LIBRARY)
    add_library(WinFSP STATIC IMPORTED)

    set_target_properties(
      WinFSP
      PROPERTIES
      IMPORTED_LOCATION ${WinFSP_LIBRARY}
    )

    if (CMAKE_VERSION VERSION_GREATER_EQUAL "3.20.0")
        cmake_path(GET WinFSP_INCLUDE_DIR PARENT_PATH WinFSP_INCLUDE_DIRS)
    else()
        get_filename_component(WinFSP_INCLUDE_DIRS "${WinFSP_INCLUDE_DIR}" DIRECTORY)
    endif()

    target_include_directories(WinFSP INTERFACE ${WinFSP_INCLUDE_DIRS})

    target_link_libraries(WinFSP INTERFACE delayimp)

    target_link_options(WinFSP INTERFACE /DELAYLOAD:${WinFSP_LIBRARY_NAME}.dll)
endif()

include(FindPackageHandleStandardArgs)

find_package_handle_standard_args(
  WinFSP
  REQUIRED_VARS
  WinFSP_INCLUDE_DIR
  WinFSP_LIBRARY
)

mark_as_advanced(WinFSP_INCLUDE_DIR WinFSP_LIBRARY)

