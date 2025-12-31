cmake_minimum_required(VERSION 3.10)

# project name
project(ActionPackets CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ActionPacketParser lib
add_library(ActionPacketParser
    ActionPacketParser.cpp
    ActionPacketParser.h
)

# header
target_include_directories(ActionPacketParser PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# executable 
add_executable(testActionPacketParser
    testActionPacketParser.cpp
)

# link
target_link_libraries(testActionPacketParser PRIVATE ActionPacketParser)

if(MSVC)
    # Microsoft Visual Studio
    target_compile_options(ActionPacketParser PRIVATE /W4 /WX)
    target_compile_options(testActionPacketParser PRIVATE /W4 /WX /wd4100) # 禁用未使用参数警告
else()
    # GCC/Clang
    target_compile_options(ActionPacketParser PRIVATE -Wall -Wextra -Wpedantic -Werror)
    target_compile_options(testActionPacketParser PRIVATE -Wall -Wextra -Wpedantic -Werror -Wno-unused-parameter) # 禁用未使用参数警告
endif()

# install rules
install(TARGETS ActionPacketParser testActionPacketParser
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

install(FILES ActionPacketParser.h DESTINATION include)

# CTest
# enable_testing()
# add_test(NAME ActionPacketParserTest COMMAND testActionPacketParser)
