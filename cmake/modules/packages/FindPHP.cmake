# Try to find the PHP executable and php-config executable
find_program(PHP_EXECUTABLE NAMES php php.exe)
FIND_PROGRAM(PHP_CONFIG_EXECUTABLE NAMES php-config)

if(NOT PHP_EXECUTABLE)
    message(FATAL_ERROR "PHP executable not found. Please ensure PHP is installed and available in PATH.")
endif()

if(NOT PHP_CONFIG_EXECUTABLE)
    message(FATAL_ERROR "PHP configuration executable not found. Please check php-config is available in the PATH.")
endif()

# Get include paths using php-config
execute_process(
    COMMAND ${PHP_CONFIG_EXECUTABLE} --includes
    OUTPUT_VARIABLE PHP_INCLUDES_OUTPUT
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

if(NOT PHP_INCLUDES_OUTPUT)
    message(FATAL_ERROR "PHP include directories not found using php-config.")
else()
    message(STATUS "Raw include directories output: ${PHP_INCLUDES_OUTPUT}")
endif()

# Create a list with the include directories
string(REPLACE "-I" "" PHP_INCLUDE_DIRS "${PHP_INCLUDES_OUTPUT}")
string(REGEX REPLACE " " ";" PHP_INCLUDE_DIRS_LIST "${PHP_INCLUDE_DIRS}")

message(STATUS "Processed PHP include directories: ${PHP_INCLUDE_DIRS_LIST}")