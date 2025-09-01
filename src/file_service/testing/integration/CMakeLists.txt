if (NOT TARGET test_integration)
    return()
endif()

target_link_libraries(test_integration PRIVATE FileServiceHeaders)

target_sources(test_integration PRIVATE client.cpp
                                        file_service_tests.cpp
                                        real_client.cpp
)
