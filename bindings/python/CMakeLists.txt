find_package(SWIG REQUIRED)
include(UseSWIG)

find_package(PythonInterp REQUIRED)
find_package(PythonLibs REQUIRED)

# Set the output directory for generated Python files
set(PYTHON_OUTPUT_DIR ${CMAKE_BINARY_DIR}/bindings/python)

# Define the interface file and its properties
set(SWIG_INTERFACE_FILE ${CMAKE_SOURCE_DIR}/bindings/megaapi.i)
set_source_files_properties(${SWIG_INTERFACE_FILE} PROPERTIES
    CPLUSPLUS ON
)

# Set SWIG flags for generating Python code
if(ENABLE_SYNC)
    list(APPEND ADDITIONAL_SWIG_DEFINES -DENABLE_SYNC)
endif()

if(USE_LIBUV)
    list(APPEND ADDITIONAL_SWIG_DEFINES -DHAVE_LIBUV)
endif()

set(CMAKE_SWIG_FLAGS -c++ -py3 -DPY3 ${ADDITIONAL_SWIG_DEFINES} -I${CMAKE_SOURCE_DIR}/include)

# Generate Python bindings
swig_add_library(SDKPythonBindings
    LANGUAGE python
    SOURCES ${SWIG_INTERFACE_FILE}
    OUTPUT_DIR ${PYTHON_OUTPUT_DIR}
)

target_include_directories(SDKPythonBindings
    PRIVATE
    ${PYTHON_INCLUDE_PATH}
)

target_link_libraries(SDKPythonBindings
    PRIVATE
    MEGA::SDKlib
)

# Generate Python wheel
set(SETUP_PYTHON_FILE ${CMAKE_SOURCE_DIR}/bindings/python/set.py)

add_custom_target(build_wheel ALL
    COMMAND ${PYTHON_EXECUTABLE} ${SETUP_PYTHON_FILE} bdist_wheel
    WORKING_DIRECTORY ${PYTHON_OUTPUT_DIR}
    DEPENDS SDKPythonBindings
    COMMENT "Building Python wheel..."
)