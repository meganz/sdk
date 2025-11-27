LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := curl
LOCAL_SRC_FILES := $(LOCAL_PATH)/curl/curl-android-$(TARGET_ARCH_ABI)/lib/libcurl.a


LOCAL_EXPORT_C_INCLUDES := $(LOCAL_PATH)/curl/curl-android-$(TARGET_ARCH_ABI)/include

LOCAL_LDFLAGS += "-Wl,-z,max-page-size=16384"

LOCAL_STATIC_LIBRARIES := ares ssl crypto


include $(PREBUILT_STATIC_LIBRARY)


include $(CLEAR_VARS)

LOCAL_MODULE := ares
LOCAL_SRC_FILES := $(LOCAL_PATH)/ares/ares-android-$(TARGET_ARCH_ABI)/lib/libcares.a

LOCAL_EXPORT_C_INCLUDES := $(LOCAL_PATH)/ares/ares-android-$(TARGET_ARCH_ABI)/include


LOCAL_LDFLAGS += "-Wl,-z,max-page-size=16384"


LOCAL_STATIC_LIBRARIES := crypto ssl

include $(PREBUILT_STATIC_LIBRARY)
