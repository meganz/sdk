if(NOT TARGET test_integration)
    return()
endif()

target_include_directories(test_integration PRIVATE
                                            ${CMAKE_CURRENT_SOURCE_DIR}
)

add_subdirectory(mega)

target_sources(test_integration PRIVATE
                                client.cpp
                                cloud_path.cpp
                                directory.cpp
                                file.cpp
                                model.cpp
                                partial_download_tests.cpp
                                path.cpp
                                printers.cpp
                                real_client.cpp
                                utility.cpp
                                watchdog.cpp
)

if (UNIX)
    add_subdirectory(posix)
else()
    add_subdirectory(windows)
endif()
