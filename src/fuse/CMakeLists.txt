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

# Only the dummy backend is implemented so far.
include(${FUSE_DIR}/unsupported/CMakeLists.txt)

