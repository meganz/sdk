target_include_directories(test_integration PRIVATE
                                            ${CMAKE_CURRENT_SOURCE_DIR}
)

add_subdirectory(mega)

target_sources(test_integration PRIVATE
                                path.cpp
)
