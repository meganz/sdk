target_include_directories(SDKlib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_sources(SDKlib PRIVATE
                      database_builder.cpp
                      directory_inode.cpp
                      file_inode.cpp
                      inode_cache.cpp
                      inode.cpp
                      inode_db.cpp
                      mount.cpp
                      mount_db.cpp
)

add_subdirectory(mega)
add_subdirectory(testing)
