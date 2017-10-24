#ifndef __libspdk_async_h__
#define __libspdk_async_h__

#ifdef __cplusplus
extern "C" {
#endif

int spdk_process_completions(int isread);
void spdk_wait_completions(int isread);
int spdk_async_io_init(void);
int spdk_async_io_read(unsigned char *buf, unsigned long blockno, 
		unsigned int io_size, void(* cb)(void*), void* arg);
int spdk_async_io_write(unsigned char *buf, unsigned long blockno, 
		unsigned int io_size, void(* cb)(void*), void* arg);
void spdk_async_io_exit(void);

int spdk_async_readahead(unsigned long blockno, unsigned int io_size);
int spdk_io_trim(unsigned long blockno, unsigned int n_bytes);

unsigned int spdk_async_get_n_lbas(void);

#ifdef __cplusplus
}
#endif


#endif
