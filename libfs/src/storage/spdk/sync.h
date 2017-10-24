#ifndef __libspdk_sync_h__
#define __libspdk_sync_h__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct spdk_sync_io {
	void* user_arg;
	void(* user_cb)(void*);
	volatile int done;
	int size;
	char *buffer;
	char *guest_buffer;
};

int spdk_sync_io_init(void);

int spdk_sync_io_read(uint8_t *buf, uint32_t blockno, uint32_t io_size,
	void(* cb)(void*), void* arg);
int spdk_sync_io_write(uint8_t *buf, uint32_t blockno, uint32_t io_size,
	void(* cb)(void*), void* arg);
void spdk_sync_io_exit(void);

unsigned int spdk_sync_get_n_lbas(void);
  
#ifdef __cplusplus
}
#endif

#endif
