# Let the user know that they can build with FUSE support.
option(WITH_FUSE "Build with FUSE support." OFF)

# Convenience.
set(FUSE_DIR ${CMAKE_CURRENT_LIST_DIR})

# Assume that we're not building via legacy scripts.
set(SDK_TARGET SDKlib)

# Actually building via legacy scripts.
if (TARGET Mega)
    set(SDK_TARGET Mega)
endif()

# Add sources required by all backends.
include(${FUSE_DIR}/common/CMakeLists.txt)

# Does the user want FUSE support built in?
if (WITH_FUSE)
    # Yep so build platform-specific backend.
    include(${FUSE_DIR}/supported/CMakeLists.txt)
else()
    # Nope so build the dummy backend.
    include(${FUSE_DIR}/unsupported/CMakeLists.txt)
endif()

