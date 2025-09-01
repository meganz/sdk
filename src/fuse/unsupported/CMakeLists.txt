# Let the SDK know where it can find our headers.
target_include_directories(SDKlib PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Add sources required by dummy backend.
target_sources(SDKlib PRIVATE
                      service_context.cpp
                      service.cpp
)

add_subdirectory(mega)
