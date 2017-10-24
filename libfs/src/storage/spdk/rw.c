#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "sync.h"

int main(int argc, char** argv)
{
	unsigned int i;
	char buffer[4096];

	if(argc != 3) {
		printf("need 2 arguments: r/w block_number\n");
		return 1;
	}

	spdk_sync_io_init();

	memset(buffer, 0, 4096);

	if(argv[1][0] == 'r') {
		spdk_sync_io_read(buffer, atoi(argv[2]), 4096);
		for (i = 0 ; i < 4096; i++) {
			if (buffer[i] != '0' + (i % 10)) {
				printf("read incorrect data\n");
				printf("Reading block %d:\n\t%s\n", atoi(argv[2]), buffer);
				exit(-1);
			}
		}
		printf("Reading block %d:\n\t%s\n", atoi(argv[2]), buffer);
	}
	else if(argv[1][0] == 'w'){
		sprintf(buffer, "this is the content of block %d", atoi(argv[2]));
		for (i = 0 ; i < 4096; i++)
			buffer[i] = '0' + (i % 10);
		spdk_sync_io_write(buffer, atoi(argv[2]), 4096);
		printf("Wrote block %d:\n\t%s\n", atoi(argv[2]), buffer);
	}
	else {
		printf("wrong argument\n");
	}

	return 0;

}
