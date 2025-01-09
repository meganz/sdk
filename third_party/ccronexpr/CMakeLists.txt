add_library(ccronexpr STATIC
    ccronexpr.h
    ccronexpr.cpp
)

target_include_directories(ccronexpr SYSTEM
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

if(ENABLE_JAVA_BINDINGS OR ENABLE_PYTHON_BINDINGS OR ENABLE_PHP_BINDINGS)
    set_target_properties(ccronexpr PROPERTIES
        POSITION_INDEPENDENT_CODE ON
    )
endif()
