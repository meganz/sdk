find_package(SWIG REQUIRED)
include(UseSWIG)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake/modules/packages)

# Set the output directory for generated PHP files
set(GO_OUTPUT_DIR ${CMAKE_BINARY_DIR}/bindings/go)

# Define the interface file and its properties
set(SWIG_INTERFACE_FILE ${CMAKE_SOURCE_DIR}/bindings/megaapi.i)
set_source_files_properties(${SWIG_INTERFACE_FILE} PROPERTIES
    CPLUSPLUS ON
)

# Set SWIG flags for generating GO code
if(ENABLE_SYNC)
    list(APPEND ADDITIONAL_SWIG_DEFINES -DENABLE_SYNC)
endif()

if(USE_LIBUV)
    list(APPEND ADDITIONAL_SWIG_DEFINES -DHAVE_LIBUV)
endif()

set(CMAKE_SWIG_FLAGS -c++ ${ADDITIONAL_SWIG_DEFINES} -I${CMAKE_SOURCE_DIR}/include)

# Generate GO bindings
swig_add_library(SDKGoBindings
    LANGUAGE go
    SOURCES ${SWIG_INTERFACE_FILE}
    OUTPUT_DIR ${GO_OUTPUT_DIR}
)

target_include_directories(SDKGoBindings
    PRIVATE
    ${GO_INCLUDE_DIRS_LIST}
)

target_link_libraries(SDKGoBindings
    PRIVATE
    MEGA::SDKlib
)

add_custom_command(
    OUTPUT ${GO_OUTPUT_DIR}/go.mod
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/bindings/go/go.mod ${GO_OUTPUT_DIR}/go.mod
    DEPENDS ${CMAKE_SOURCE_DIR}/bindings/go/go.mod
    COMMENT "Copying go.mod to ${GO_OUTPUT_DIR}"
)

add_custom_target(CopyGoMod ALL
    DEPENDS ${GO_OUTPUT_DIR}/go.mod
)

add_dependencies(SDKGoBindings CopyGoMod)