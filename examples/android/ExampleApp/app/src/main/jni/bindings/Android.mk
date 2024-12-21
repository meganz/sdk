LOCAL_PATH := $(call my-dir)

# This last step is intended to strip all unneeded code from the shared library
# All other compilations have the flag -fvisibility=hidden -fdata-sections -ffunction-sections 
# The link step uses -Wl,-dead_strip,-gc-sections to strip all unused code
include $(CLEAR_VARS)
LOCAL_MODULE := mega
LOCAL_CFLAGS := -fdata-sections -ffunction-sections -DDEBUG
LOCAL_SRC_FILES := $(LOCAL_PATH)/megasdk.cpp
LOCAL_LDLIBS := -lm -lz -llog -lGLESv2 -lOpenSLES
LOCAL_LDFLAGS :=  -Wl,-gc-sections 
LOCAL_LDFLAGS += "-Wl,-z,max-page-size=16384"
LOCAL_STATIC_LIBRARIES := megasdk
include $(BUILD_SHARED_LIBRARY)
