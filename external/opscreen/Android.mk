LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_SRC_FILES := opscreen.c

LOCAL_MODULE := opscreen

LOCAL_MODULE_TAGS := optional

LOCAL_MODULE_PATH := $(TARGET_OUT_EXECUTABLES)

include $(BUILD_EXECUTABLE)
