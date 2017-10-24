#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "async.h"

int main(int argc, char** argv)
{
	unsigned int i;

	if(argc != 3) {
		printf("need 2 arguments: #blocks first_block\n");
		return 1;
	}

	int bytes = atoi(argv[1])*4096;
	int block = atoi(argv[2]);
	spdk_async_io_init();
	
	char* buffer = malloc(bytes);
	//memset(buffer, 0, bytes);
	for(int i = 0 ; i < bytes ; i++)
	  buffer[i] = i % 15;
	
	spdk_async_io_write(buffer, block, bytes, NULL, NULL);

	spdk_wait_completions();

	//just to make sure.
	memset(buffer, 0x01, bytes);

	spdk_async_io_read(buffer, block, bytes, NULL, NULL);
	spdk_wait_completions();
	/*
	printf("Read:\n");
	for (i = 0 ; i < bytes; i++) {
	  printf("%x", buffer[i]);
	}
	*/
	for (i = 0 ; i < bytes; i++) {
	  if (buffer[i] != i % 15) {
	    printf("read incorrect data at byte %d of %d\n", i, bytes);
	    printf("Should have 0x%x but had 0x%x\n", i%15, buffer[i]);
	    exit(-1);
	  }
	}

	printf("Data read was equal to written! Test passed!\n");


	return 0;

}
