#include <stdio.h>
#include <sys/select.h>
#include <time.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>

#define DEFAULT 817
#define RIGHT_UP 766
#define RIGHT_DOWN 681
#define LEFT_UP  25
#define LEFT_DOWN 510
#define DISPLAY_NUM 6

unsigned char g_to_display[DISPLAY_NUM][32] = {
"1111111111111111",
"22222222222222222222222222",
"3333333333333333",
"4444444444444444",
"5555555555555555",
"6666666666666666",
};

int g_curNum = 0;

void milliseconds_sleep(unsigned long mSec){
    struct timeval tv;
    tv.tv_sec=mSec/1000;
    tv.tv_usec=(mSec%1000)*1000;
    int err;
    do{
       err=select(0,NULL,NULL,NULL,&tv);
    }while(err<0 && errno==EINTR);
}

void opscreen(char cBuf[]) {
    int fd,l = 0;
    fd = open("/sys/devices/i2c-2/2-003c/gmd_13002", O_WRONLY);
    if (fd == -1) {
        printf("Error: open() failed.\n");
        return;
    }
    l = write(fd, cBuf, strlen(cBuf));
    close(fd);
}

void displayArray() {
    int i = 0;
	unsigned char displayStr[200] = {0};
	unsigned char posStr[40] = {0};
	int tmpNum = 0;
	int offset = 0;
	for (i = 0; i < 4; i++) {
		tmpNum = g_curNum + i;
		if (tmpNum >= DISPLAY_NUM) {
			tmpNum = tmpNum - DISPLAY_NUM;
		}
		if (offset == 0) {
			snprintf(posStr, 40, "0200%s\r\n", g_to_display[tmpNum]);
		} else if (offset > 4) {
			break;
		} else {
			snprintf(posStr, 40, "02%2d%s\r\n", 16*offset, g_to_display[tmpNum]);
		}
		offset = (strlen(g_to_display[tmpNum]) -1) / 16 + offset;
		offset++;
		strcat(displayStr, posStr);
	}
	printf("the displayStr is %s", displayStr);
	opscreen(displayStr);
}

void handleKey(int fd) {
    int readNum = 0;
    int readings = 0;
    char readBuf[5] = {0};
    
    readNum = read(fd, readBuf, 4);
    if (readNum <= 0) {
        printf("read error. the errorno is %d\n", errno);
    	exit(-1);
    }
    (void)lseek(fd, 0, SEEK_SET);
    readings = atoi(readBuf);
    if ((DEFAULT < readings + 25) && (DEFAULT > readings - 25)) {
    	return;
    } else if ((RIGHT_UP < readings + 25) && (RIGHT_UP > readings - 25)) {
		g_curNum++;
		if (g_curNum > DISPLAY_NUM) {
			g_curNum = 0;
		}
		displayArray();
    	return;
    } else if ((RIGHT_DOWN < readings + 25) && (RIGHT_DOWN > readings - 25)) {
		g_curNum--;
		if (g_curNum < 0) {
			g_curNum = DISPLAY_NUM;
		}
		displayArray();
    	return;
    } else if ((LEFT_DOWN < readings + 25) && (LEFT_DOWN > readings - 25)) {
    	opscreen("2222left down\r\n");
    	return;
    } else if ((LEFT_UP < readings + 25) && (LEFT_UP > readings - 25)) {
    	opscreen("2222left up\r\n");
    	return;
    } else {
    	//opscreen("x22y22to do\r\n");
    }
}

int main() {
    int fd,i = 0;
    fd = open("/sys/class/saradc/ch0", O_RDONLY);
    if (fd == -1) {
        printf("Error: open() failed.\n");
        return 1;
    }
    while(1) {
        milliseconds_sleep(200);
        handleKey(fd);
    }
    close(fd);
}
