find_package(Java REQUIRED)
find_package(SWIG REQUIRED)
include(UseSWIG)

# For Android we do not need JNI
if(ANDROID)
    include_directories(
        ${ANDROID_NDK_HOME}/sysroot/usr/include
        ${ANDROID_NDK_HOME}/sysroot/usr/include/${ANDROID_ABI}
    )
else()
    find_package(JNI REQUIRED)
endif()

# Set the paths for Java include directories
set(JAVA_INCLUDE_DIR "${Java_INCLUDE_DIRS}")
set(JAVA_OUTPUT_DIR "${CMAKE_BINARY_DIR}/bindings/java/nz/mega/sdk")

# Define the interface file and its properties
set(SWIG_INTERFACE_FILE "${CMAKE_SOURCE_DIR}/bindings/megaapi.i")
set_source_files_properties(${SWIG_INTERFACE_FILE} PROPERTIES
    CPLUSPLUS ON
)

include_directories(
    ${JAVA_INCLUDE_DIR}
    ${CMAKE_SOURCE_DIR}/include
    ${JNI_INCLUDE_DIRS}
    ${JAVA_OUTPUT_DIR}
)

# Set SWIG flags for generating Java code
if(ENABLE_SYNC)
    list(APPEND ADDITIONAL_SWIG_DEFINES -DENABLE_SYNC)
endif()

if(USE_LIBUV)
    list(APPEND ADDITIONAL_SWIG_DEFINES -DHAVE_LIBUV)
endif()

set(CMAKE_SWIG_FLAGS -c++ -package "nz.mega.sdk" -O ${ADDITIONAL_SWIG_DEFINES})

# Generate java binding files
swig_add_library(SDKJavaBindings
    LANGUAGE java
    SOURCES ${SWIG_INTERFACE_FILE}
    OUTPUT_DIR ${JAVA_OUTPUT_DIR}
)

target_link_libraries(SDKJavaBindings
    PRIVATE
    MEGA::SDKlib
)

# Compile Java code and create a JAR file
add_custom_command(TARGET SDKJavaBindings POST_BUILD
    COMMAND ${Java_JAVAC_EXECUTABLE} -d ${JAVA_OUTPUT_DIR} -cp ${JAVA_OUTPUT_DIR} ${JAVA_OUTPUT_DIR}/*.java
    COMMENT "Compiling Java classes..."
)

add_custom_command(TARGET SDKJavaBindings POST_BUILD
    COMMAND ${Java_JAR_EXECUTABLE} cf ${CMAKE_BINARY_DIR}/SDKJavaBindings.jar -C ${JAVA_OUTPUT_DIR} .
    COMMENT "Creating JAR file..."
)