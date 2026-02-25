add_library(http_parser STATIC
    http_parser/http_parser.h
    http_parser/http_parser.cpp
)

target_include_directories(http_parser SYSTEM
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

if(ENABLE_JAVA_BINDINGS)
    set_target_properties(http_parser PROPERTIES
        POSITION_INDEPENDENT_CODE ON
    )
endif()

if(APPLE AND ENABLE_SDKLIB_WERROR)
    set_source_files_properties(
        http_parser/http_parser.cpp
        PROPERTIES
        COMPILE_FLAGS "-Wno-sign-conversion"
    )
endif()
