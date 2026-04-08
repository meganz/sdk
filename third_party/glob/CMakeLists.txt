add_library(glob OBJECT
    glob.h
    glob.c
)

target_include_directories(glob SYSTEM
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

if(ENABLE_JAVA_BINDINGS)
    set_target_properties(glob PROPERTIES
        POSITION_INDEPENDENT_CODE ON
    )
endif()
