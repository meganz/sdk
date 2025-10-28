target_include_directories(test_integration PRIVATE file_service)

target_link_libraries(test_integration PRIVATE MEGA::FileServiceHeaderPaths)

target_sources(test_integration PRIVATE
                                file_service/mega/file_service/testing/integration/client.h
                                file_service/mega/file_service/testing/integration/client_forward.h
                                file_service/mega/file_service/testing/integration/real_client.h
                                file_service/mega/file_service/testing/integration/scoped_file_event_observer.h
)

target_sources(test_integration PRIVATE
                                file_service/client.cpp
                                file_service/file_service_tests.cpp
                                file_service/real_client.cpp
)
