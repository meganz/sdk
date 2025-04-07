add_subdirectory(
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/mega/file_service
    ${CMAKE_CURRENT_BINARY_DIR}/../../mega/file_service
)

target_sources(SDKlib PRIVATE
                      file_service_result.cpp
)

