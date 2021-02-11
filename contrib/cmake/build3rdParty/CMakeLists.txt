

cmake_minimum_required(VERSION 3.13)
project(Build3rdParty)

if (WIN32)
    # accurate __cplusplus macro for vc++, selecting c++17 here for windows builds though the MEGA SDK library must build for older c++ standards also
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /Zc:__cplusplus /std:c++17")
endif()

if (APPLE)
    set(CMAKE_CXX_STANDARD 11)
    SET(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()

add_executable(build3rdParty build3rdParty.cpp)
if (NOT WIN32 AND NOT APPLE)
    target_link_libraries(build3rdParty stdc++fs)
endif()
