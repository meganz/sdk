add_subdirectory(
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/mega/file_service
    ${CMAKE_CURRENT_BINARY_DIR}/../../mega/file_service
)

add_subdirectory(mega/file_service)

add_library(FileServiceHeaders INTERFACE)

target_include_directories(
    FileServiceHeaders INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(SDKlib PRIVATE FileServiceHeaders)

target_sources(SDKlib PRIVATE
                      database_builder.cpp
                      file.cpp
                      file_context.cpp
                      file_id.cpp
                      file_info.cpp
                      file_info_context.cpp
                      file_range.cpp
                      file_read_request_set.cpp
                      file_read_write_state.cpp
                      file_result.cpp
                      file_service.cpp
                      file_service_context.cpp
                      file_service_queries.cpp
                      file_service_result.cpp
                      file_storage.cpp
                      logger.cpp
                      memory_buffer.cpp
)

add_subdirectory(testing)

