add_library(CommonHeaderPaths INTERFACE)
add_library(MEGA::CommonHeaderPaths ALIAS CommonHeaderPaths)

target_include_directories(CommonHeaderPaths INTERFACE
    $<$<BOOL:${UNIX}>:${CMAKE_CURRENT_SOURCE_DIR}/src/common/platform/posix>
    $<$<BOOL:${WIN32}>:${CMAKE_CURRENT_SOURCE_DIR}/src/common/platform/windows>
)

target_link_libraries(SDKlib PRIVATE CommonHeaderPaths)

target_sources(SDKlib PRIVATE
                      include/mega/common/activity_monitor.h
                      include/mega/common/activity_monitor_forward.h
                      include/mega/common/badge.h
                      include/mega/common/badge_forward.h
                      include/mega/common/client.h
                      include/mega/common/client_adapter.h
                      include/mega/common/client_callbacks.h
                      include/mega/common/client_forward.h
                      include/mega/common/database.h
                      include/mega/common/database_builder.h
                      include/mega/common/database_forward.h
                      include/mega/common/database_utilities.h
                      include/mega/common/date_time.h
                      include/mega/common/date_time_forward.h
                      include/mega/common/deciseconds.h
                      include/mega/common/directory.h
                      include/mega/common/error_or.h
                      include/mega/common/error_or_forward.h
                      include/mega/common/expected.h
                      include/mega/common/expected_forward.h
                      include/mega/common/lock.h
                      include/mega/common/lock_forward.h
                      include/mega/common/lockable.h
                      include/mega/common/lockable_forward.h
                      include/mega/common/logger.h
                      include/mega/common/logger_forward.h
                      include/mega/common/logging.h
                      include/mega/common/node_event.h
                      include/mega/common/node_event_forward.h
                      include/mega/common/node_event_observer.h
                      include/mega/common/node_event_observer_forward.h
                      include/mega/common/node_event_queue.h
                      include/mega/common/node_event_queue_forward.h
                      include/mega/common/node_event_type.h
                      include/mega/common/node_event_type_forward.h
                      include/mega/common/node_info.h
                      include/mega/common/node_info_forward.h
                      include/mega/common/node_key_data.h
                      include/mega/common/node_key_data_forward.h
                      include/mega/common/normalized_path.h
                      include/mega/common/normalized_path_forward.h
                      include/mega/common/partial_download.h
                      include/mega/common/partial_download_callback.h
                      include/mega/common/partial_download_callback_forward.h
                      include/mega/common/partial_download_forward.h
                      include/mega/common/pending_callbacks.h
                      include/mega/common/query.h
                      include/mega/common/query_forward.h
                      include/mega/common/scoped_query.h
                      include/mega/common/scoped_query_forward.h
                      include/mega/common/serialization_traits.h
                      include/mega/common/serialization_traits_forward.h
                      include/mega/common/shared_mutex.h
                      include/mega/common/shared_mutex_forward.h
                      include/mega/common/status_flag.h
                      include/mega/common/subsystem_logger.h
                      include/mega/common/task_executor.h
                      include/mega/common/task_executor_flags.h
                      include/mega/common/task_executor_flags_forward.h
                      include/mega/common/task_executor_forward.h
                      include/mega/common/task_queue.h
                      include/mega/common/task_queue_forward.h
                      include/mega/common/transaction.h
                      include/mega/common/transaction_forward.h
                      include/mega/common/type_traits.h
                      include/mega/common/unexpected.h
                      include/mega/common/unexpected_forward.h
                      include/mega/common/upload.h
                      include/mega/common/upload_callbacks.h
                      include/mega/common/upload_forward.h
                      include/mega/common/utility.h
)

target_sources_conditional(SDKlib FLAG UNIX PRIVATE
                                  src/common/platform/posix/mega/common/platform/date_time.h
                                  src/common/platform/posix/mega/common/platform/folder_locker.h
)

target_sources_conditional(SDKlib FLAG WIN32 PRIVATE
                                  src/common/platform/windows/mega/common/platform/date_time.h
                                  src/common/platform/windows/mega/common/platform/folder_locker.h
                                  src/common/platform/windows/mega/common/platform/folder_locker.cpp
)

target_sources(SDKlib PRIVATE
                      src/common/activity_monitor.cpp
                      src/common/client.cpp
                      src/common/client_adapter.cpp
                      src/common/database.cpp
                      src/common/database_builder.cpp
                      src/common/date_time.cpp
                      src/common/directory.cpp
                      src/common/logger.cpp
                      src/common/node_event_type.cpp
                      src/common/normalized_path.cpp
                      src/common/pending_callbacks.cpp
                      src/common/query.cpp
                      src/common/scoped_query.cpp
                      src/common/shared_mutex.cpp
                      src/common/subsystem_logger.cpp
                      src/common/task_executor.cpp
                      src/common/task_queue.cpp
                      src/common/transaction.cpp
                      src/common/upload.cpp
                      src/common/utility.cpp
)

target_sources_conditional(SDKlib FLAG ENABLE_SYNC PRIVATE
                           src/common/client_adapter_with_sync.cpp
)

target_sources_conditional(SDKlib FLAG NOT ENABLE_SYNC PRIVATE
                           src/common/client_adapter_without_sync.cpp
)
