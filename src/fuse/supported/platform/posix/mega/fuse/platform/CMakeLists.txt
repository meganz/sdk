target_sources(SDKlib PRIVATE
                      constants.h
                      directory_context.h
                      file_descriptor.h
                      file_descriptor_forward.h
                      inode_invalidator.h
                      mount.h
                      mount_db.h
                      path_adapter.h
                      process.h
                      process_forward.h
                      request.h
                      request_forward.h
                      session_base.h
                      session_forward.h
                      signal.h
                      utility.h
)

# Convenience.
set(CAPABILITY_HEADER_PATH "${CMAKE_CURRENT_BINARY_DIR}")
set(CAPABILITY_HEADER_FILE "${CAPABILITY_HEADER_PATH}/capability_flags.i")

# Do we need to generate a list of known capability flags?
if (NOT EXISTS "${CAPABILITY_HEADER_FILE}")
    # Convenience.
    set(CAPABILITY_PATTERN "^#define (FUSE_CAP_[^ \t]+).*$")

    # Extract capability flag definitions.
    file(STRINGS "${FUSE_INCLUDE_DIR}/fuse_common.h"
                 CAPABILITY_DEFINITIONS
                 REGEX "${CAPABILITY_PATTERN}")

    # Create mega/fuse/platform if necessary.
    file(MAKE_DIRECTORY "${CAPABILITY_HEADER_PATH}")

    # Write capability flag entries to a header.
    foreach (CAPABILITY_DEFINITION ${CAPABILITY_DEFINITIONS})
        # Generate an entry for this capability flag.
        string(REGEX REPLACE "${CAPABILITY_PATTERN}"
                             "ENTRY(\\1)\n"
                             CAPABILITY_ENTRY
                             "${CAPABILITY_DEFINITION}")

        # Write the entry to our header.
        file(APPEND "${CAPABILITY_HEADER_FILE}" "${CAPABILITY_ENTRY}")
    endforeach()

    # Cleanup.
    unset(CAPABILITY_ENTRY)
    unset(CAPABILITY_DEFINITION)
    unset(CAPABILITY_DEFINITIONS)
    unset(CAPABILITY_PATTERN)
endif()

# Cleanup.
unset(CAPABILITY_HEADER_FILE)
unset(CAPABILITY_HEADER_PATH)
