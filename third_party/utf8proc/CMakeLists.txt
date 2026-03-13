add_library(utf8proc STATIC
    utf8proc.h
    utf8proc.cpp
)

set(UTF8PROC_PUBLIC_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/include/utf8proc")
file(MAKE_DIRECTORY "${UTF8PROC_PUBLIC_INCLUDE_DIR}")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/utf8proc.h"
    "${UTF8PROC_PUBLIC_INCLUDE_DIR}/utf8proc.h"
    COPYONLY
)

target_include_directories(utf8proc SYSTEM
    PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
)

if(ENABLE_JAVA_BINDINGS)
    set_target_properties(utf8proc PROPERTIES
        POSITION_INDEPENDENT_CODE ON
    )
endif()

if(APPLE AND ENABLE_SDKLIB_WERROR)
    set_source_files_properties(
        utf8proc.cpp
        PROPERTIES
        COMPILE_FLAGS "-Wno-sign-conversion"
    )
endif()
