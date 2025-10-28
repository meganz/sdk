# Let the user know that they can build with FUSE support.
option(WITH_FUSE "Build with FUSE support." OFF)

# Add sources required by all backends.
add_subdirectory(common)

# Does the user want FUSE support built in?
if (WITH_FUSE)
    # Yep so build platform-specific backend.
    add_subdirectory(supported)
else()
    # Nope so build the dummy backend.
    add_subdirectory(unsupported)
endif()
