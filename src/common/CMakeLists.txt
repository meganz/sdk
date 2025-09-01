add_subdirectory(
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/mega/common
    ${CMAKE_CURRENT_BINARY_DIR}/../../mega/common
)

# Apple and 32bit UNIX systems have a time_t distinct time m_time_t.
if (APPLE OR CMAKE_SIZEOF_VOID_P EQUAL 4)
    target_compile_definitions(SDKlib PUBLIC HAS_DISTINCT_TIME_T)
endif()

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

add_subdirectory(testing)
