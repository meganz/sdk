if(NOT TARGET test_integration)
    return()
endif()

target_include_directories(test_integration PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_sources(test_integration PRIVATE
                                directory_monitor.cpp
                                mount_tests.cpp
                                platform_tests.cpp
                                printers.cpp
                                sync_tests.cpp
                                wrappers.cpp
)

add_subdirectory(mega)
