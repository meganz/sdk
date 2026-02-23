add_library(csv INTERFACE)

target_include_directories(csv SYSTEM
    INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

if(ENABLE_JAVA_BINDINGS)
    set_property(TARGET csv PROPERTY
        INTERFACE_POSITION_INDEPENDENT_CODE ON
    )
endif()
