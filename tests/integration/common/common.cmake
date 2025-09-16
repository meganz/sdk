target_include_directories(test_integration PRIVATE
                                            ${CMAKE_CURRENT_SOURCE_DIR}/common
)

target_link_libraries(test_integration PRIVATE
                                       MEGA::CommonHeaderPaths
)

target_sources(test_integration PRIVATE
                                common/mega/common/testing/client.h
                                common/mega/common/testing/client_forward.h
                                common/mega/common/testing/cloud_path.h
                                common/mega/common/testing/cloud_path_forward.h
                                common/mega/common/testing/directory.h
                                common/mega/common/testing/file.h
                                common/mega/common/testing/model.h
                                common/mega/common/testing/model_forward.h
                                common/mega/common/testing/path.h
                                common/mega/common/testing/path_forward.h
                                common/mega/common/testing/printers.h
                                common/mega/common/testing/real_client.h
                                common/mega/common/testing/single_client_test.h
                                common/mega/common/testing/test.h
                                common/mega/common/testing/utility.h
                                common/mega/common/testing/watchdog.h
)

target_sources(test_integration PRIVATE
                                common/client.cpp
                                common/cloud_path.cpp
                                common/directory.cpp
                                common/file.cpp
                                common/model.cpp
                                common/partial_download_tests.cpp
                                common/path.cpp
                                common/printers.cpp
                                common/real_client.cpp
                                common/utility.cpp
                                common/watchdog.cpp
)


target_include_directories(test_integration PRIVATE
    $<$<BOOL:UNIX>:${CMAKE_CURRENT_SOURCE_DIR}/common/posix>
    $<$<BOOL:WIN32>:${CMAKE_CURRENT_SOURCE_DIR}/common/windows>
    ${CMAKE_CURRENT_LIST_DIR}/../..
)

target_sources_conditional(test_integration FLAG UNIX PRIVATE
                                            common/posix/utility.cpp
)

target_sources_conditional(test_integration FLAG WIN32 PRIVATE
                                            common/windows/utility.cpp
)
