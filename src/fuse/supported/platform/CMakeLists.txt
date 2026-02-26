# Add sources required by all platforms.
target_include_directories(SDKlib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_sources(SDKlib PRIVATE
                      context.cpp
                      file_context.cpp
                      service.cpp
                      service_context.cpp
                      unmounter.cpp
)

# Add headers required by all platforms.
add_subdirectory(mega)

# Add tests common to all platforms.
add_subdirectory(testing)

# Add backend-specific sources.
if (UNIX)
    add_subdirectory(posix)
else()
    add_subdirectory(windows)
endif()
