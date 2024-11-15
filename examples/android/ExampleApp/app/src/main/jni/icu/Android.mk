LOCAL_PATH:= $(call my-dir)


ICU_VERSION:= 71_1

include $(CLEAR_VARS)
LOCAL_MODULE := icuuc
LOCAL_SRC_FILES := $(LOCAL_PATH)/icuSource-${ICU_VERSION}/icu/$(TARGET_ARCH_ABI)/lib/libicuuc.a
LOCAL_EXPORT_C_INCLUDES := $(LOCAL_PATH)/icuSource-${ICU_VERSION}/icu/source/common \
	$(LOCAL_PATH)/icuSource-${ICU_VERSION}/icu/source/i18n
LOCAL_LDFLAGS += "-Wl,-z,max-page-size=16384"
include $(PREBUILT_STATIC_LIBRARY)


include $(CLEAR_VARS)
LOCAL_MODULE := icui18n
LOCAL_EXPORT_C_INCLUDES := $(LOCAL_PATH)/icuSource-${ICU_VERSION}/icu/source/common \
	$(LOCAL_PATH)/icuSource-${ICU_VERSION}/icu/source/i18n
LOCAL_SRC_FILES := $(LOCAL_PATH)/icuSource-$(ICU_VERSION)/icu/$(TARGET_ARCH_ABI)/lib/libicui18n.a
LOCAL_LDFLAGS += "-Wl,-z,max-page-size=16384"
include $(PREBUILT_STATIC_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := icudata
LOCAL_EXPORT_C_INCLUDES := $(LOCAL_PATH)/icuSource-${ICU_VERSION}/icu/source/common \
	$(LOCAL_PATH)/icuSource-${ICU_VERSION}/icu/source/i18n
LOCAL_SRC_FILES := $(LOCAL_PATH)/icuSource-$(ICU_VERSION)/icu/$(TARGET_ARCH_ABI)/lib/libicudata.a
LOCAL_LDFLAGS += "-Wl,-z,max-page-size=16384"
include $(PREBUILT_STATIC_LIBRARY)


# Define the main module that depends on the static libraries
LOCAL_MODULE := icu
LOCAL_STATIC_LIBRARIES := icuuc icui18n icudata
LOCAL_EXPORT_C_INCLUDES := $(LOCAL_PATH)/icuSource-$(ICU_VERSION)/icu/source/common \
                           $(LOCAL_PATH)/icuSource-$(ICU_VERSION)/icu/source/i18n
LOCAL_LDFLAGS += "-Wl,-z,max-page-size=16384"
include $(BUILD_STATIC_LIBRARY)
