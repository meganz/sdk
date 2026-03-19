add_library(csv INTERFACE)

set(CSV_PUBLIC_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/include/csv")
file(MAKE_DIRECTORY "${CSV_PUBLIC_INCLUDE_DIR}")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/csv.h"
    "${CSV_PUBLIC_INCLUDE_DIR}/csv.h"
    COPYONLY
)

target_include_directories(csv SYSTEM
    INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
)

if(ENABLE_JAVA_BINDINGS)
    set_property(TARGET csv PROPERTY
        INTERFACE_POSITION_INDEPENDENT_CODE ON
    )
endif()
