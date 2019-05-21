LOCAL_PATH:= $(call my-dir)

# gpio_audio
include $(CLEAR_VARS)

LOCAL_CFLAGS := -std=c++11
LOCAL_NDK_STL_VARIANT := c++_static
LOCAL_LDFLAGS   := -llog
LOCAL_SDK_VERSION := 9
LOCAL_MODULE    := libjni_gpioaduio
LOCAL_SRC_FILES := activitytest_example_com_gpio_GpioAduio.cpp

LOCAL_CFLAGS    += -ffast-math -O3 -funroll-loops
LOCAL_ARM_MODE := arm

include $(BUILD_SHARED_LIBRARY)
