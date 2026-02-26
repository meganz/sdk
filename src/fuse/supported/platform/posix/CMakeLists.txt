# Let the SDK know where it can find our headers.
target_include_directories(SDKlib PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_include_directories(SDKlib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Add sources required by libfuse backend.
target_sources(SDKlib PRIVATE
                      constants.cpp
                      directory_context.cpp
                      file_descriptor.cpp
                      inode_invalidator.cpp
                      mount.cpp
                      mount_db.cpp
                      process.cpp
                      request.cpp
                      service.cpp
                      session_base.cpp
                      signal.cpp
                      unmounter.cpp
                      utility.cpp
)

# Add libfuse sources.
add_subdirectory(libfuse)

# Add libfuse headers.
add_subdirectory(mega)

# Add platform-specific sources.
if(APPLE)
    add_subdirectory(darwin)
else()
    add_subdirectory(linux)
endif()

# Add libfuse tests.
add_subdirectory(testing)
