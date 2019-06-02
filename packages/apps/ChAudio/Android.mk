LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_PACKAGE_NAME := Chaudio
LOCAL_CERTIFICATE := platform
LOCAL_MODULE_TAGS := optional
LOCAL_PRIVILEGED_MODULE := true

LOCAL_SRC_FILES := $(call all-java-files-under, src)
LOCAL_SDK_VERSION := current

LOCAL_JNI_SHARED_LIBRARIES := libjni_gpioaduio


include $(BUILD_PACKAGE)
include $(call all-makefiles-under,$(LOCAL_PATH))
