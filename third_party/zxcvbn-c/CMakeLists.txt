add_library(zxcvbn-c OBJECT
    zxcvbn.h
    dict-src.h
    zxcvbn.cpp
)

set(ZXCVBN_C_PUBLIC_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/include/zxcvbn-c")
file(MAKE_DIRECTORY "${ZXCVBN_C_PUBLIC_INCLUDE_DIR}")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/zxcvbn.h"
    "${ZXCVBN_C_PUBLIC_INCLUDE_DIR}/zxcvbn.h"
    COPYONLY
)

target_include_directories(zxcvbn-c SYSTEM
    PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
)

if(ENABLE_JAVA_BINDINGS)
    set_target_properties(zxcvbn-c PROPERTIES
        POSITION_INDEPENDENT_CODE ON
    )
endif()

if(WIN32 AND ENABLE_SDKLIB_WERROR)
    set_source_files_properties(
        zxcvbn.cpp
        PROPERTIES
        COMPILE_FLAGS "/wd4456"
    )
endif()

if(APPLE AND ENABLE_SDKLIB_WERROR)
    set_source_files_properties(
        zxcvbn.cpp
        PROPERTIES
        COMPILE_FLAGS "-Wno-sign-conversion"
    )
endif()
