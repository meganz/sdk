
APP_PLATFORM=android-21

NDK_TOOLCHAIN_VERSION=clang
APP_STL := c++_shared
APP_OPTIM := release
APP_PIE := false

APP_CPPFLAGS += -Wno-extern-c-compat -mno-unaligned-access -fexceptions -frtti -std=c++17
APP_LDFLAGS += -v -Wl,-allow-multiple-definition
DISABLE_WEBRTC = true
USE_LIBWEBSOCKETS = false
