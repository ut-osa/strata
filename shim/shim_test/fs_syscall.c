#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include "../libshim/interfaces.h"

#define BUF_SIZE 128

int main(void)
{
	int fd;
	char buf[BUF_SIZE];
	char *str="glibc system call hooking test.";

	// open file with posix API
	fd = open("./test_file", O_RDWR|O_CREAT, S_IRUSR|S_IWUSR);

	//write(fd, str, strlen(str));
	write(fd, str, 20);

	lseek(fd, 0, SEEK_SET);

	memset(buf, 0, BUF_SIZE);

	read(fd, buf, BUF_SIZE);
	
	printf("%s\n", buf);

	close(fd);
	
	return 0;
}
