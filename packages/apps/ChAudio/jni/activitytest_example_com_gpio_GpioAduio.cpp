//
// Created by mst on 2019/3/28.
//
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <android/log.h>
#include <unistd.h>
#include "activitytest_example_com_gpio_GpioAduio.h"

#define LOG_TAG "GpioAduio"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR,LOG_TAG, __VA_ARGS__)

JNIEXPORT void JNICALL Java_activitytest_example_com_gpio_GpioAduio_enableSpk
(JNIEnv *, jclass)
{
    int fd,i,l = 0;
	LOGE("Enter Java_activitytest_example_com_gpio_GpioAduio_enableSpk\n");
    fd = open("/sys/devices/virtual/ch_audio/gpio_audio/gpio_audio", O_WRONLY);
    if (fd == -1) {
        LOGE("Error: open() failed. the error is %s\n", strerror(errno));
        return;
    }
    l = write(fd, "1", strlen("1"));
    if (l < 0) {
        LOGE("Error: write failed. the error is %s\n", strerror(errno));
    }
    close(fd);
}

JNIEXPORT void JNICALL Java_activitytest_example_com_gpio_GpioAduio_disableSpk
  (JNIEnv *, jclass){
    int fd,i,l = 0;
	LOGE("Enter Java_activitytest_example_com_gpio_GpioAduio_disableSpk\n");
    fd = open("/sys/devices/virtual/ch_audio/gpio_audio/gpio_audio", O_WRONLY);
    if (fd == -1) {
        LOGE("Error: open() failed. the error is %s\n", strerror(errno));
        return;
    }
    l = write(fd, "0", strlen("0"));
    if (l < 0) {
        LOGE("Error: write failed. the error is %s\n", strerror(errno));
    }
    close(fd);
}