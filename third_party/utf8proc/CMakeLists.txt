add_library(utf8proc STATIC
    utf8proc/utf8proc.h
    utf8proc/utf8proc.cpp
)

target_include_directories(utf8proc SYSTEM
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

if(ENABLE_JAVA_BINDINGS)
    set_target_properties(utf8proc PROPERTIES
        POSITION_INDEPENDENT_CODE ON
    )
endif()

if(APPLE AND ENABLE_SDKLIB_WERROR)
    set_source_files_properties(
        utf8proc/utf8proc.cpp
        PROPERTIES
        COMPILE_FLAGS "-Wno-sign-conversion"
    )
endif()
