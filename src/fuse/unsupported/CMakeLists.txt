# Convenience.
set(FUSE_UNSUPPORTED_SRC ${CMAKE_CURRENT_LIST_DIR})
set(FUSE_UNSUPPORTED_INC ${FUSE_UNSUPPORTED_SRC}/mega/fuse/platform)

# Let the SDK know where it can find our headers.
target_include_directories(${SDK_TARGET} PRIVATE ${FUSE_UNSUPPORTED_SRC})

# Add sources required by dummy backend.
target_sources(${SDK_TARGET} PRIVATE
                             ${FUSE_UNSUPPORTED_INC}/service_context.h
                             ${FUSE_UNSUPPORTED_SRC}/service_context.cpp
                             ${FUSE_UNSUPPORTED_SRC}/service.cpp
)

