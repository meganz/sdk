add_library(ccronexpr STATIC
    ccronexpr.h
    ccronexpr.cpp
)

set(CCRONEXPR_PUBLIC_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/include/ccronexpr")
file(MAKE_DIRECTORY "${CCRONEXPR_PUBLIC_INCLUDE_DIR}")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/ccronexpr.h"
    "${CCRONEXPR_PUBLIC_INCLUDE_DIR}/ccronexpr.h"
    COPYONLY
)

target_include_directories(ccronexpr SYSTEM
    PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
)

if(WIN32)
    set_source_files_properties(
        ccronexpr.cpp
        PROPERTIES
        COMPILE_FLAGS "/wd4456" # declaration hides previous local declaration
    )
endif()

if(ENABLE_JAVA_BINDINGS)
    set_target_properties(ccronexpr PROPERTIES
        POSITION_INDEPENDENT_CODE ON
    )
endif()
