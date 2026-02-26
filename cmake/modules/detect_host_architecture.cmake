# Detects the host architecture (HOST_ARCH) on Linux systems using `uname -m`.
# If the value of HOST_ARCH is already set, it will not be changed, allowing
# users to override the default detection if needed.
#
# This module should be included before calling project().
#
# Usage:
# 
#  include(detect_host_architecture)
# 
# After inclusion, HOST_ARCH will be set to one of the following values:
# - "x86_64" for 64-bit x86 systems
# - "ARM" for ARM-based systems (includes aarch64, armv7, etc.)
# - "Unknown" if the architecture is not recognized

if (UNIX)
    if (NOT DEFINED HOST_ARCH)
        execute_process(COMMAND uname -m
                        OUTPUT_VARIABLE HOST_ARCHITECTURE
                        OUTPUT_STRIP_TRAILING_WHITESPACE)

        set(HOST_ARCH ${HOST_ARCHITECTURE} CACHE STRING "Host architecture")
        unset(HOST_ARCHITECTURE)
    endif ()
endif ()