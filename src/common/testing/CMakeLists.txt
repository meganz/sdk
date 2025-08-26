target_include_directories(test_integration PRIVATE
                                            ${CMAKE_CURRENT_SOURCE_DIR}
)

add_subdirectory(mega)

target_sources(test_integration PRIVATE
                                cloud_path.cpp
                                directory.cpp
                                file.cpp
                                model.cpp
                                path.cpp
                                printers.cpp
                                utility.cpp
)

if (UNIX)
    add_subdirectory(posix)
else()
    add_subdirectory(windows)
endif()
