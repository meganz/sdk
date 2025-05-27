if (NOT TARGET test_integration OR NOT WITH_FUSE)
    return()
endif()

target_link_libraries(test_integration PRIVATE FileServiceHeaders)

target_sources(test_integration PRIVATE file_service.cpp)


