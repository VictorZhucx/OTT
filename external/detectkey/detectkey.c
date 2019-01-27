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
    	opscreen("x22y22right up\r\n");
    	return;
    } else if ((RIGHT_DOWN < readings + 25) && (RIGHT_DOWN > readings - 25)) {
    	opscreen("x22y22right down\r\n");
    	return;
    } else if ((LEFT_DOWN < readings + 25) && (LEFT_DOWN > readings - 25)) {
    	opscreen("x22y22left down\r\n");
    	return;
    } else if ((LEFT_UP < readings + 25) && (LEFT_UP > readings - 25)) {
    	opscreen("x22y22left up\r\n");
    	return;
    } else {
    	opscreen("x22y22to do\r\n");
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
