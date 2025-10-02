target_include_directories(SDKlib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_sources(SDKlib PRIVATE utility.cpp)

add_subdirectory(mega)
