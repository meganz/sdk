#ifndef MEGA_MAJOR_VERSION
#define MEGA_MAJOR_VERSION 10
#endif
#ifndef MEGA_MINOR_VERSION
#define MEGA_MINOR_VERSION 19
#endif
#ifndef MEGA_MICRO_VERSION
#define MEGA_MICRO_VERSION 0
#endif

// Supports versions [0-255].[0-255].[0-255]
#ifndef MEGA_SDK_BUILD_VERSION
#define MEGA_SDK_BUILD_VERSION(major, minor, patch) ((((major) << 16) | ((minor) << 8) | (patch)))
#endif

#ifndef MEGA_SDK_VERSION
#define MEGA_SDK_VERSION \
    MEGA_SDK_BUILD_VERSION(MEGA_MAJOR_VERSION, MEGA_MINOR_VERSION, MEGA_MICRO_VERSION)
#endif