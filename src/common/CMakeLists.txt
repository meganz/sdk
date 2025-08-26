add_subdirectory(
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/mega/common
    ${CMAKE_CURRENT_BINARY_DIR}/../../mega/common
)

target_sources(SDKlib PRIVATE
                      activity_monitor.cpp
                      client.cpp
                      client_adapter.cpp
                      database.cpp
                      database_builder.cpp
                      date_time.cpp
                      directory.cpp
                      logger.cpp
                      node_event_type.cpp
                      normalized_path.cpp
                      pending_callbacks.cpp
                      query.cpp
                      scoped_query.cpp
                      shared_mutex.cpp
                      subsystem_logger.cpp
                      task_executor.cpp
                      task_queue.cpp
                      transaction.cpp
                      upload.cpp
                      utility.cpp
)

if(ENABLE_SYNC)
    target_sources(SDKlib PRIVATE client_adapter_with_sync.cpp)
else()
    target_sources(SDKlib PRIVATE client_adapter_without_sync.cpp)
endif()

add_subdirectory(platform)

if(TARGET test_integration)
    add_subdirectory(testing)
endif()
