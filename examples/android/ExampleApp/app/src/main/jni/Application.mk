APP_PLATFORM=android-16
NDK_TOOLCHAIN_VERSION=clang
APP_STL := c++_static
APP_ABI := armeabi-v7a x86 arm64-v8a x86_64
APP_OPTIM := release
APP_PIE := false

# then enable c++11 extentions in source code
APP_CPPFLAGS += -std=c++11 -Wno-extern-c-compat -mno-unaligned-access -fexceptions -frtti

DISABLE_WEBRTC = true
USE_LIBWEBSOCKETS = false

