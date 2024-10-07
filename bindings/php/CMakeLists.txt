find_package(SWIG REQUIRED)
include(UseSWIG)

find_package(PHP REQUIRED)

# Set the output directory for generated PHP files
set(PHP_OUTPUT_DIR ${CMAKE_BINARY_DIR}/bindings/php)

# Define the interface file and its properties
set(SWIG_INTERFACE_FILE ${CMAKE_SOURCE_DIR}/bindings/megaapi.i)
set_source_files_properties(${SWIG_INTERFACE_FILE} PROPERTIES
    CPLUSPLUS ON
)

# Set SWIG flags for generating PHP code
if(ENABLE_SYNC)
    list(APPEND ADDITIONAL_SWIG_DEFINES -DENABLE_SYNC)
endif()

if(USE_LIBUV)
    list(APPEND ADDITIONAL_SWIG_DEFINES -DHAVE_LIBUV)
endif()

set(CMAKE_SWIG_FLAGS -c++ ${ADDITIONAL_SWIG_DEFINES} -I${CMAKE_SOURCE_DIR}/include)

# Generate PHP bindings
swig_add_library(SDKPhpBindings
    LANGUAGE php
    SOURCES ${SWIG_INTERFACE_FILE}
    OUTPUT_DIR ${PHP_OUTPUT_DIR}
)

target_include_directories(SDKPhpBindings
    PRIVATE
    ${PHP_INCLUDE_DIRS_LIST}
)

target_link_libraries(SDKPhpBindings
    PRIVATE
    MEGA::SDKlib
)