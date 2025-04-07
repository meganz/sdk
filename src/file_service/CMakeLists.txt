add_subdirectory(
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/mega/file_service
    ${CMAKE_CURRENT_BINARY_DIR}/../../mega/file_service
)

add_subdirectory(mega/file_service)

target_include_directories(SDKlib PRIVATE
                                  ${CMAKE_CURRENT_SOURCE_DIR}
)

target_sources(SDKlib PRIVATE
                      construction_logger.cpp
                      database_builder.cpp
                      destruction_logger.cpp
                      file_id.cpp
                      file_info.cpp
                      file_info_context.cpp
                      file_service.cpp
                      file_service_context.cpp
                      file_service_result.cpp
                      file_storage.cpp
                      logger.cpp
)

