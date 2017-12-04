#include <err.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "storage/spdk/async.h"
#include "storage/spdk/sync.h"

static void async_op_done(void* arg) {
  *((unsigned int*) arg) += 1;
}

int main(void)
{
  spdk_async_io_init();
  printf("SPDK is ready\n");
  volatile unsigned int done = 0;
  int ret;

  printf("Allocating buffer\n");
  char buf[4096];
  memset(buf, 0, 4096);


  printf("Writing 4k bytes\n");
  ret = spdk_async_io_write((unsigned char *)buf, 99999, 4096,
          async_op_done, (void*)&done);


  if(ret == -1 && errno == EBUSY) {
    exit(1);
  }

  while(!done) {
    spdk_process_completions(0);
  }
  printf("Done.\n\n");

  printf("Allocating/Writing 32M bytes\n");
  char* buf2 = malloc(32*1024*1024);
  done = 0;
  ret = spdk_async_io_write((unsigned char *)buf2, 99999, 32*1024*1024,
          async_op_done, (void*)&done);

  if(ret == -1 && errno == EBUSY) {
    exit(1);
  }

  while(!done) {
    spdk_process_completions(0);
  }

  printf("Done.\n\n");

  printf("Exiting.\n");
  spdk_async_io_exit();

  return 0;
}
