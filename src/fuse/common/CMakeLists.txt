# Convenience
set(FUSE_COMMON_SRC ${CMAKE_CURRENT_LIST_DIR})
set(FUSE_COMMON_INC ${FUSE_COMMON_SRC}/../../../include/mega/fuse/common)

# Let the SDK know where it can find our headers.
target_include_directories(
    ${SDK_TARGET} PUBLIC $<BUILD_INTERFACE:${FUSE_COMMON_SRC}>)

# Add sources required by all backends.
target_sources(${SDK_TARGET} PRIVATE
                             ${FUSE_COMMON_INC}/activity_monitor.h
                             ${FUSE_COMMON_INC}/activity_monitor_forward.h
                             ${FUSE_COMMON_INC}/any_lock.h
                             ${FUSE_COMMON_INC}/any_lock_forward.h
                             ${FUSE_COMMON_INC}/any_lock_set.h
                             ${FUSE_COMMON_INC}/any_lock_set_forward.h
                             ${FUSE_COMMON_INC}/bind_handle.h
                             ${FUSE_COMMON_INC}/bind_handle_forward.h
                             ${FUSE_COMMON_INC}/client.h
                             ${FUSE_COMMON_INC}/client_adapter.h
                             ${FUSE_COMMON_INC}/client_callbacks.h
                             ${FUSE_COMMON_INC}/client_forward.h
                             ${FUSE_COMMON_INC}/database.h
                             ${FUSE_COMMON_INC}/date_time.h
                             ${FUSE_COMMON_INC}/date_time_forward.h
                             ${FUSE_COMMON_INC}/error_or.h
                             ${FUSE_COMMON_INC}/error_or_forward.h
                             ${FUSE_COMMON_INC}/file_open_flag.h
                             ${FUSE_COMMON_INC}/file_open_flag_forward.h
                             ${FUSE_COMMON_INC}/inode_cache_flags.h
                             ${FUSE_COMMON_INC}/inode_cache_flags_forward.h
                             ${FUSE_COMMON_INC}/inode_id.h
                             ${FUSE_COMMON_INC}/inode_id_forward.h
                             ${FUSE_COMMON_INC}/inode_info.h
                             ${FUSE_COMMON_INC}/inode_info_forward.h
                             ${FUSE_COMMON_INC}/lock.h
                             ${FUSE_COMMON_INC}/lock_forward.h
                             ${FUSE_COMMON_INC}/lockable.h
                             ${FUSE_COMMON_INC}/lockable_forward.h
                             ${FUSE_COMMON_INC}/log_level.h
                             ${FUSE_COMMON_INC}/log_level_forward.h
                             ${FUSE_COMMON_INC}/logger.h
                             ${FUSE_COMMON_INC}/logger_forward.h
                             ${FUSE_COMMON_INC}/logging.h
                             ${FUSE_COMMON_INC}/mount_event.h
                             ${FUSE_COMMON_INC}/mount_event_forward.h
                             ${FUSE_COMMON_INC}/mount_event_type.h
                             ${FUSE_COMMON_INC}/mount_event_type_forward.h
                             ${FUSE_COMMON_INC}/mount_flags.h
                             ${FUSE_COMMON_INC}/mount_flags_forward.h
                             ${FUSE_COMMON_INC}/mount_info.h
                             ${FUSE_COMMON_INC}/mount_info_forward.h
                             ${FUSE_COMMON_INC}/mount_inode_id.h
                             ${FUSE_COMMON_INC}/mount_inode_id_forward.h
                             ${FUSE_COMMON_INC}/mount_result.h
                             ${FUSE_COMMON_INC}/mount_result_forward.h
                             ${FUSE_COMMON_INC}/node_event.h
                             ${FUSE_COMMON_INC}/node_event_forward.h
                             ${FUSE_COMMON_INC}/node_event_observer.h
                             ${FUSE_COMMON_INC}/node_event_observer_forward.h
                             ${FUSE_COMMON_INC}/node_event_queue.h
                             ${FUSE_COMMON_INC}/node_event_queue_forward.h
                             ${FUSE_COMMON_INC}/node_event_type.h
                             ${FUSE_COMMON_INC}/node_event_type_forward.h
                             ${FUSE_COMMON_INC}/node_info.h
                             ${FUSE_COMMON_INC}/node_info_forward.h
                             ${FUSE_COMMON_INC}/normalized_path.h
                             ${FUSE_COMMON_INC}/normalized_path_forward.h
                             ${FUSE_COMMON_INC}/pending_callbacks.h
                             ${FUSE_COMMON_INC}/query.h
                             ${FUSE_COMMON_INC}/query_forward.h
                             ${FUSE_COMMON_INC}/scoped_query.h
                             ${FUSE_COMMON_INC}/scoped_query_forward.h
                             ${FUSE_COMMON_INC}/service.h
                             ${FUSE_COMMON_INC}/service_callbacks.h
                             ${FUSE_COMMON_INC}/service_context.h
                             ${FUSE_COMMON_INC}/service_context_forward.h
                             ${FUSE_COMMON_INC}/service_flags.h
                             ${FUSE_COMMON_INC}/service_flags_forward.h
                             ${FUSE_COMMON_INC}/service_forward.h
                             ${FUSE_COMMON_INC}/shared_mutex.h
                             ${FUSE_COMMON_INC}/shared_mutex_forward.h
                             ${FUSE_COMMON_INC}/task_executor.h
                             ${FUSE_COMMON_INC}/task_executor_flags.h
                             ${FUSE_COMMON_INC}/task_executor_flags_forward.h
                             ${FUSE_COMMON_INC}/task_executor_forward.h
                             ${FUSE_COMMON_INC}/task_queue.h
                             ${FUSE_COMMON_INC}/task_queue_forward.h
                             ${FUSE_COMMON_INC}/transaction.h
                             ${FUSE_COMMON_INC}/transaction_forward.h
                             ${FUSE_COMMON_INC}/type_traits.h
                             ${FUSE_COMMON_INC}/upload.h
                             ${FUSE_COMMON_INC}/upload_callbacks.h
                             ${FUSE_COMMON_INC}/upload_forward.h
                             ${FUSE_COMMON_INC}/utility.h
                             ${FUSE_COMMON_SRC}/activity_monitor.cpp
                             ${FUSE_COMMON_SRC}/any_lock_set.cpp
                             ${FUSE_COMMON_SRC}/bind_handle.cpp
                             ${FUSE_COMMON_SRC}/client.cpp
                             ${FUSE_COMMON_SRC}/client_adapter.cpp
                             ${FUSE_COMMON_SRC}/database.cpp
                             ${FUSE_COMMON_SRC}/date_time.cpp
                             ${FUSE_COMMON_SRC}/inode_id.cpp
                             ${FUSE_COMMON_SRC}/inode_info.cpp
                             ${FUSE_COMMON_SRC}/log_level.cpp
                             ${FUSE_COMMON_SRC}/logger.cpp
                             ${FUSE_COMMON_SRC}/mount_event.cpp
                             ${FUSE_COMMON_SRC}/mount_event_type.cpp
                             ${FUSE_COMMON_SRC}/mount_flags.cpp
                             ${FUSE_COMMON_SRC}/mount_info.cpp
                             ${FUSE_COMMON_SRC}/mount_inode_id.cpp
                             ${FUSE_COMMON_SRC}/mount_result.cpp
                             ${FUSE_COMMON_SRC}/node_event_type.cpp
                             ${FUSE_COMMON_SRC}/normalized_path.cpp
                             ${FUSE_COMMON_SRC}/pending_callbacks.cpp
                             ${FUSE_COMMON_SRC}/query.cpp
                             ${FUSE_COMMON_SRC}/scoped_query.cpp
                             ${FUSE_COMMON_SRC}/service.cpp
                             ${FUSE_COMMON_SRC}/service_context.cpp
                             ${FUSE_COMMON_SRC}/shared_mutex.cpp
                             ${FUSE_COMMON_SRC}/task_executor.cpp
                             ${FUSE_COMMON_SRC}/task_queue.cpp
                             ${FUSE_COMMON_SRC}/transaction.cpp
                             ${FUSE_COMMON_SRC}/utility.cpp
)

# Assume the sync engine is being built.
set(TEMP "with")

# The sync engine isn't being built.
if (NOT ENABLE_SYNC)
    string(APPEND TEMP "out")
endif()

# Compute path to sync-engine-dependent client-adapter code.
set(TEMP "${FUSE_COMMON_SRC}/client_adapter_${TEMP}_sync.cpp")

# Make sure code is built into the SDK.
target_sources(${SDK_TARGET} PRIVATE ${TEMP})

# Clean up after ourselves.
unset(TEMP)

# Not building with FUSE support.
if (NOT WITH_FUSE)
    return()
endif()

# Add sources required by concrete backends.
target_sources(${SDK_TARGET} PRIVATE
                             ${FUSE_COMMON_INC}/badge.h
                             ${FUSE_COMMON_INC}/badge_forward.h
                             ${FUSE_COMMON_INC}/constants.h
                             ${FUSE_COMMON_INC}/database_builder.h
                             ${FUSE_COMMON_INC}/database_forward.h
                             ${FUSE_COMMON_INC}/directory_inode.h
                             ${FUSE_COMMON_INC}/directory_inode_forward.h
                             ${FUSE_COMMON_INC}/directory_inode_results.h
                             ${FUSE_COMMON_INC}/error_or.h
                             ${FUSE_COMMON_INC}/error_or_forward.h
                             ${FUSE_COMMON_INC}/file_cache.h
                             ${FUSE_COMMON_INC}/file_cache_forward.h
                             ${FUSE_COMMON_INC}/file_extension_db.h
                             ${FUSE_COMMON_INC}/file_extension_db_forward.h
                             ${FUSE_COMMON_INC}/file_info.h
                             ${FUSE_COMMON_INC}/file_info_forward.h
                             ${FUSE_COMMON_INC}/file_inode.h
                             ${FUSE_COMMON_INC}/file_inode_forward.h
                             ${FUSE_COMMON_INC}/file_io_context.h
                             ${FUSE_COMMON_INC}/file_io_context_forward.h
                             ${FUSE_COMMON_INC}/inode.h
                             ${FUSE_COMMON_INC}/inode_badge.h
                             ${FUSE_COMMON_INC}/inode_badge_forward.h
                             ${FUSE_COMMON_INC}/inode_cache.h
                             ${FUSE_COMMON_INC}/inode_cache_forward.h
                             ${FUSE_COMMON_INC}/inode_db.h
                             ${FUSE_COMMON_INC}/inode_db_forward.h
                             ${FUSE_COMMON_INC}/inode_forward.h
                             ${FUSE_COMMON_INC}/mount.h
                             ${FUSE_COMMON_INC}/mount_db.h
                             ${FUSE_COMMON_INC}/mount_db_forward.h
                             ${FUSE_COMMON_INC}/mount_forward.h
                             ${FUSE_COMMON_INC}/path_adapter.h
                             ${FUSE_COMMON_INC}/path_adapter_forward.h
                             ${FUSE_COMMON_INC}/ref.h
                             ${FUSE_COMMON_INC}/ref_forward.h
                             ${FUSE_COMMON_INC}/tags.h
                             ${FUSE_COMMON_SRC}/database_builder.cpp
                             ${FUSE_COMMON_SRC}/directory_inode.cpp
                             ${FUSE_COMMON_SRC}/file_cache.cpp
                             ${FUSE_COMMON_SRC}/file_extension_db.cpp
                             ${FUSE_COMMON_SRC}/file_info.cpp
                             ${FUSE_COMMON_SRC}/file_inode.cpp
                             ${FUSE_COMMON_SRC}/file_io_context.cpp
                             ${FUSE_COMMON_SRC}/inode.cpp
                             ${FUSE_COMMON_SRC}/inode_cache.cpp
                             ${FUSE_COMMON_SRC}/inode_db.cpp
                             ${FUSE_COMMON_SRC}/mount.cpp
                             ${FUSE_COMMON_SRC}/mount_db.cpp
)

# Are we building integration tests?
if (NOT TARGET test_integration)
    return()
endif()

set(FUSE_COMMON_TESTING_INC ${FUSE_COMMON_SRC}/mega/fuse/common/testing)
set(FUSE_COMMON_TESTING_SRC ${FUSE_COMMON_SRC}/testing)

# Assume we're not building using the legacy scripts.
set(SDK_SOURCE_DIR ${PROJECT_SOURCE_DIR})

# We are building using the legacy scripts.
if (TARGET Mega)
    set(SDK_SOURCE_DIR ${MegaDir})
endif()

target_include_directories(test_integration PRIVATE ${SDK_SOURCE_DIR})
target_include_directories(test_unit PRIVATE ${SDK_SOURCE_DIR})

target_sources(test_integration PRIVATE
                                ${FUSE_COMMON_TESTING_INC}/client.h
                                ${FUSE_COMMON_TESTING_INC}/cloud_path.h
                                ${FUSE_COMMON_TESTING_INC}/cloud_path_forward.h
                                ${FUSE_COMMON_TESTING_INC}/directory.h
                                ${FUSE_COMMON_TESTING_INC}/file.h
                                ${FUSE_COMMON_TESTING_INC}/mock_client.h
                                ${FUSE_COMMON_TESTING_INC}/model.h
                                ${FUSE_COMMON_TESTING_INC}/model_forward.h
                                ${FUSE_COMMON_TESTING_INC}/mount_event_observer.h
                                ${FUSE_COMMON_TESTING_INC}/mount_event_observer_forward.h
                                ${FUSE_COMMON_TESTING_INC}/mount_tests.h
                                ${FUSE_COMMON_TESTING_INC}/parameters.h
                                ${FUSE_COMMON_TESTING_INC}/parameters_forward.h
                                ${FUSE_COMMON_TESTING_INC}/path.h
                                ${FUSE_COMMON_TESTING_INC}/path_forward.h
                                ${FUSE_COMMON_TESTING_INC}/printers.h
                                ${FUSE_COMMON_TESTING_INC}/real_client.h
                                ${FUSE_COMMON_TESTING_INC}/test.h
                                ${FUSE_COMMON_TESTING_INC}/test_base.h
                                ${FUSE_COMMON_TESTING_INC}/utility.h
                                ${FUSE_COMMON_TESTING_INC}/watchdog.h
                                ${FUSE_COMMON_TESTING_SRC}/client.cpp
                                ${FUSE_COMMON_TESTING_SRC}/cloud_path.cpp
                                ${FUSE_COMMON_TESTING_SRC}/common_tests.cpp
                                ${FUSE_COMMON_TESTING_SRC}/directory.cpp
                                ${FUSE_COMMON_TESTING_SRC}/file.cpp
                                ${FUSE_COMMON_TESTING_SRC}/mock_client.cpp
                                ${FUSE_COMMON_TESTING_SRC}/model.cpp
                                ${FUSE_COMMON_TESTING_SRC}/mount_event_observer.cpp
                                ${FUSE_COMMON_TESTING_SRC}/mount_tests.cpp
                                ${FUSE_COMMON_TESTING_SRC}/parameters.cpp
                                ${FUSE_COMMON_TESTING_SRC}/path.cpp
                                ${FUSE_COMMON_TESTING_SRC}/printers.cpp
                                ${FUSE_COMMON_TESTING_SRC}/real_client.cpp
                                ${FUSE_COMMON_TESTING_SRC}/test.cpp
                                ${FUSE_COMMON_TESTING_SRC}/test_base.cpp
                                ${FUSE_COMMON_TESTING_SRC}/utility.cpp
                                ${FUSE_COMMON_TESTING_SRC}/watchdog.cpp
)

target_sources(test_unit PRIVATE
                         ${FUSE_COMMON_TESTING_SRC}/shared_mutex_tests.cpp
)

IF (NOT ENABLE_SYNC)
    return()
endif()

target_sources(test_integration PRIVATE
                                ${FUSE_COMMON_TESTING_SRC}/sync_tests.cpp
)

