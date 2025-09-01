add_subdirectory(
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../include/mega/fuse/common
    ${CMAKE_CURRENT_BINARY_DIR}/../../../include/mega/fuse/common
)

# Add sources required by all backends.
target_sources(SDKlib PRIVATE
                      any_lock_set.cpp
                      client.cpp
                      file_explorer_view.cpp
                      file_move_flag.cpp
                      inode_id.cpp
                      inode_info.cpp
                      logger.cpp
                      mount_event.cpp
                      mount_event_type.cpp
                      mount_flags.cpp
                      mount_info.cpp
                      mount_inode_id.cpp
                      mount_result.cpp
                      service.cpp
                      service_context.cpp
)
