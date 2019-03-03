#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

typedef struct displayInfo_S {
	char xBuf[3];
	char yBuf[3];
	char strBuf[15];
} displayInfo;

static void print_usage(char *argv0)
{
    fprintf(stderr, "Usage: %s x<value>y<value><display string> ...\n", argv0);
    fprintf(stderr, "       value: postion(should between 10-50)\n");
    fprintf(stderr, "       display string: which will show on the screen\n");
    fprintf(stderr, "       Example: %s x12y14789 x22y24xxx\n", argv0);
}

int getInfo(char *buf, int count, displayInfo* dInfo) {
	int num = 0;
	dInfo->xBuf[0] = buf[1];
	dInfo->xBuf[1] = buf[2];
	dInfo->xBuf[2] = '\0';
	dInfo->yBuf[0] = buf[4];
	dInfo->yBuf[1] = buf[5];
	dInfo->yBuf[2] = '\0';
	for (num = 0; num < count - 6; num++) {
		dInfo->strBuf[num] = buf[num + 6];
	}
	dInfo->strBuf[num+1] = '\0';
	return 0;
}

int getInputInfo(char *buf, int count, displayInfo* dInfo) {
	int tmpCount = 0;
	int displayCount = 0;
	for (tmpCount = 0; tmpCount < count; tmpCount++) {
		if (tmpCount == 0) {
			if (buf[tmpCount] != 'x') {
				printf("The first char should be x.\n");
				return -1;
			}
		} else if (tmpCount == 3) {
			if (buf[tmpCount] != 'y') {
				printf("The third char should be y.\n");
				return -1;
			}
		} else if (tmpCount == 24) {
			printf("The length of input should less than 24.\n");
			return -1;
		} 
	}
	(void)getInfo(buf, count, dInfo);
	return 0;
}

int main(int argc, char *argv[])
{
	char tmpCmdStr[100] = {0};
	displayInfo dInfo;
	int fd,l,i = 0;
	if (argc >= 2) {
		if (strncmp(argv[1], "image", 5) != 0) {
			for (; i < argc - 1; i++) {
        		if (i > 5) {
        			printf("You should not input exceed 5 args\n");
        			return -1;
        		}
        		memset(&dInfo, 0, sizeof(dInfo));
        		if (-1 == getInputInfo(argv[i+1], strlen(argv[i+1]), &dInfo)) {
        			return -1;
        		}
        		strcat(tmpCmdStr, dInfo.xBuf);
        		strcat(tmpCmdStr, dInfo.yBuf);
        		strcat(tmpCmdStr, dInfo.strBuf);
			    strcat(tmpCmdStr, "\r\n");
        	}
		} else {
			strncpy(tmpCmdStr, argv[1], 5);
			printf("here %s\n",tmpCmdStr);
		}
        fd = open("/sys/devices/i2c-2/2-003c/gmd_13002", O_WRONLY);
        if (fd == -1) {
            printf("Error: open() failed.\n");
            return 1;
        }
		l = write(fd, tmpCmdStr, strlen(tmpCmdStr));
		close(fd);
		return 0;
	}

	print_usage(argv[0]);

	return 1;
}
