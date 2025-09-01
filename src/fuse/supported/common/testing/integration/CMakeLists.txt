if(NOT TARGET test_integration)
    return()
endif()

add_subdirectory(mega)

target_include_directories(test_integration PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_sources(test_integration PRIVATE
                                client.cpp
                                common_tests.cpp
                                mount_event_observer.cpp
                                mount_tests.cpp
                                parameters.cpp
                                printers.cpp
                                real_client.cpp
                                sync_tests.cpp
                                test.cpp
                                test_base.cpp
                                utility.cpp
                                watchdog.cpp
)

if(ENABLE_SYNC)
    target_sources(test_integration PRIVATE sync_tests.cpp)
endif()
