add_library(http_parser OBJECT
    http_parser.h
    http_parser.cpp
)

set(HTTP_PARSER_PUBLIC_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/include/http_parser")
file(MAKE_DIRECTORY "${HTTP_PARSER_PUBLIC_INCLUDE_DIR}")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/http_parser.h"
    "${HTTP_PARSER_PUBLIC_INCLUDE_DIR}/http_parser.h"
    COPYONLY
)

target_include_directories(http_parser SYSTEM
    PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
)

if(ENABLE_JAVA_BINDINGS)
    set_target_properties(http_parser PROPERTIES
        POSITION_INDEPENDENT_CODE ON
    )
endif()

if(APPLE AND ENABLE_SDKLIB_WERROR)
    set_source_files_properties(
        http_parser.cpp
        PROPERTIES
        COMPILE_FLAGS "-Wno-sign-conversion"
    )
endif()
