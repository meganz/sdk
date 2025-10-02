if(NOT TARGET test_integration)
    return()
endif()

add_subdirectory(mega)

target_include_directories(test_integration PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_sources(test_integration PRIVATE platform_tests.cpp)
