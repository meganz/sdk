LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_MODULE := ssl
LOCAL_SRC_FILES := $(LOCAL_PATH)/openssl/openssl-android-$(TARGET_ARCH_ABI)/lib/libssl.a
LOCAL_EXPORT_C_INCLUDES := $(LOCAL_PATH)/openssl/openssl-android-$(TARGET_ARCH_ABI)/include
LOCAL_LDFLAGS += "-Wl,-z,max-page-size=16384"
LOCAL_SHARED_LIBRARIES := crypto
include $(PREBUILT_STATIC_LIBRARY)

LOCAL_MODULE := crypto
LOCAL_SRC_FILES := $(LOCAL_PATH)/openssl/openssl-android-$(TARGET_ARCH_ABI)/lib/libcrypto.a
LOCAL_EXPORT_C_INCLUDES := $(LOCAL_PATH)/openssl/openssl-android-$(TARGET_ARCH_ABI)/include
LOCAL_LDFLAGS += "-Wl,-z,max-page-size=16384"
include $(PREBUILT_STATIC_LIBRARY)
