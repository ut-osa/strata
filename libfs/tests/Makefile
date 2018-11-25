CC = gcc -std=c99
#CC = c99


EXE = iotest file_basic small_io falloc_test ftrunc_test lock_test lock_perf simple_spdk_test dir_test many_files_test fork_io readdir_test append_test fwrite_fread partial_update_test timer_api lease_test concurrent_write_crash
#$(info $(EXE))

CUR_DIR = $(shell pwd)
GLIBC_DIR = $(CUR_DIR)/../../shim/glibc-build/
LIBFS_DIR = $(CUR_DIR)/../build/
NVML_DIR = $(LIBFS_DIR)/../lib/nvml/src/nondebug/
CUCKOO_DIR = $(LIBFS_DIR)/../lib/cuckoofilter
LIBFS=$(LIBFS_DIR)/libmlfs.a
LIBSPDK_DIR =../src/storage/spdk/
LIBSPDK=../src/storage/spdk/libspdk.so
LIBSPDK_INC=../src/storage/spdk/

INCLUDES= $(LIBFS_DIR)/../src -I$(CUR_DIR)/../lib/nvml/src/include
DAX_OBJ = ../build/storage/storage_dax.o

DPDK_VER := 16.07.2
DPDK_DIR := $(abspath $(CURDIR)/../lib/dpdk-$(DPDK_VER)/x86_64-native-linuxapp-gcc)
SPDK_ROOT_DIR := $(abspath $(CURDIR)/../lib/spdk)
include $(SPDK_ROOT_DIR)/mk/spdk.common.mk
SPDK_LIBS += $(SPDK_ROOT_DIR)/build/lib/libspdk_nvme.a \
			 $(SPDK_ROOT_DIR)/build/lib/libspdk_util.a \
			 $(SPDK_ROOT_DIR)/build/lib/libspdk_log.a \
			 $(ENV_LIBS)

CFLAGS=-I $(LIBSPDK_INC) -I $(DPDK_DIR)/include -I$(INCLUDES)

LDFLAGS = -Wl,-rpath=$(abspath $(GLIBC_DIR)) \
		  -Wl,-rpath=$(abspath $(GLIBC_DIR))/rt \
		  -Wl,-rpath=$(abspath $(LIBFS_DIR)) \
		  -Wl,-rpath=$(abspath $(NVML_DIR)) \
		  -Wl,-rpath=$(abspath $(LIBSPDK_DIR)) \
		  -Wl,-rpath=/usr/local/lib \
		  -Wl,-rpath=/usr/lib/x86_64-linux-gnu/ \
		  -Wl,-rpath=/lib/x86_64-linux-gnu/ \
		  -Wl,-dynamic-linker=$(abspath $(GLIBC_DIR))/ld-linux-x86-64.so.2 \
		  -lpthread -lrt -lm -L libssl.a -L libcrypto.a -lrdmacm -libverbs

all: $(EXE)

%.o: %.c
	$(CC) $(CFLAGS) -c -g $< -o $@

%.o: %.cc
	$(CXX) -std=c++11 $(CFLAGS) -c -g $< -o $@

time_stat.o: time_stat.c
	$(CC) time_stat.c -c -o time_stat.o -D_BSD_SOURCE

simple_spdk_test: simple_spdk_test.c
	$(CC) -g -o $@ $^ -I$(INCLUDES) -L$(LIBFS_DIR) -lmlfs $(LIBSPDK) $(LDFLAGS)

file_basic: file_basic.c
	$(CC) -g -o $@ $^ -I$(INCLUDES) -L$(LIBFS_DIR) -lmlfs $(LDFLAGS)

small_io: small_io.c
	$(CC) -g -o $@ $^ -I$(INCLUDES) -L$(LIBFS_DIR) -lmlfs $(LDFLAGS)

append_test: append_test.c
	$(CC) -g -o $@ $^ -I$(INCLUDES) -L$(LIBFS_DIR) -lmlfs $(LDFLAGS)

signal_test: signal_test.c
	$(CC) -g -o $@ $^ -I$(INCLUDES) -L$(LIBFS_DIR) -lmlfs $(LDFLAGS)

falloc_test: falloc_test.c
	$(CC) -g -o $@ $^ -I$(INCLUDES) -L$(LIBFS_DIR) -lmlfs -DMLFS $(CFLAGS) $(LDFLAGS)

ftrunc_test: ftrunc_test.c
	$(CC) -g -o $@ $^ -I$(INCLUDES) -L$(LIBFS_DIR) -lmlfs -DMLFS $(CFLAGS) $(LDFLAGS)

readdir_test: readdir_test.c
	$(CC) -g -o $@ $^ -I$(INCLUDES) -L$(LIBFS_DIR) -lmlfs -DMLFS $(CFLAGS) $(LDFLAGS)

write_read: write_read.c time_stat.o
	$(CC) -g -o $(addsuffix .normal, $@) $^ $(LIBSPDK) $(CFLAGS) $(DAX_OBJ) -L$(NVML_DIR) -lpmem -lpthread -lm -lrt  -Wl,-rpath=$(abspath $(NVML_DIR))

iotest: iotest.cc time_stat.o thread.cc
	$(CXX) -std=c++11 -g -Ofast -o $@ $^  -I$(INCLUDES) -L$(LIBFS_DIR) -lmlfs -L$(LIBSPDK_DIR) -lspdk -DMLFS -DMLFS_INFO $(CFLAGS) $(LDFLAGS) 
	$(CXX) -std=c++11 -g -Ofast -o $(addsuffix .normal, $@) $^ $(LIBSPDK) $(CFLAGS) $(DAX_OBJ) -L$(NVML_DIR) -lpmem -lpthread -lm -lrt -Wl,-rpath=$(abspath $(NVML_DIR))

fwrite_fread: fwrite_fread.cc time_stat.o thread.cc
	$(CXX) -std=c++11 -g -Ofast -o $@ $^  -I$(INCLUDES) -L$(LIBFS_DIR) -lmlfs -L$(LIBSPDK_DIR) -lspdk -DMLFS $(CFLAGS) $(LDFLAGS) 

lock_test: lock_test.c
	$(CC) -g -o $@ $^ -I$(INCLUDES) ../build/concurrency/locks.o ../build/concurrency/condvar.o -pthread -lrt

lock_perf: lock_perf.c
	$(CC) -g -o $@ $^ -I$(INCLUDES) ../build/concurrency/locks.o ../build/concurrency/condvar.o -pthread -lrt -DPERF_mlfs

dir_test: dir_test.c
	$(CC) -O0 -g -o $@ $^ $(CFLAGS) -L$(LIBFS_DIR) -lmlfs -lm -lrt -L$(LIBSPDK_DIR) -lspdk -L$(NVML_DIR) -lpmem -lpthread -lm -lrt -Wl,-rpath=$(abspath $(NVML_DIR)) -I$(INCLUDES) $(LDFLAGS)

many_files_test: many_files_test.cc time_stat.o thread.cc
	$(CXX) -std=c++11 -O2 -g -o $@ $^ $(CFLAGS) -L$(LIBFS_DIR) -lmlfs -lm -lrt -L$(LIBSPDK_DIR) -lspdk -L$(NVML_DIR) -lpmem -lpthread -lm -lrt -Wl,-rpath=$(abspath $(NVML_DIR)) -I$(INCLUDES) $(LDFLAGS)

partial_update_test: partial_update_test.cc
	$(CXX) -std=c++11 -g -O0 -o $@ $^ $(CFLAGS) -I$(INCLUDES) -L$(LIBFS_DIR) -lmlfs -lm -lrt -lpthread $(LDFLAGS) -fopenmp
	#$(CXX) -std=c++11 -g -o $@ $^  -I$(INCLUDES) -L$(LIBFS_DIR) -lmlfs -L$(LIBSPDK_DIR) -lspdk -DMLFS $(CFLAGS) $(LDFLAGS) 

fork_io: fork_io.cc time_stat.o 
	$(CXX) -std=c++11 -g -o $@ $^  -I$(INCLUDES) -L$(LIBFS_DIR) -lmlfs -L$(LIBSPDK_DIR) -lspdk -DMLFS $(CFLAGS) $(LDFLAGS) 

lease_test: lease_test.cc time_stat.o 
	$(CXX) -std=c++11 -g -o $@ $^  -I$(INCLUDES) -L$(LIBFS_DIR) -lmlfs -L$(LIBSPDK_DIR) -lspdk -DMLFS $(CFLAGS) $(LDFLAGS) 

timer_api: timer_api.c
	$(CC) -o $@ $^

concurrent_write_crash: concurrent_write_crash.cc
	$(CXX) -std=c++11 -g -o $@ $^  -I$(INCLUDES) $(CFLAGS) $(LDFLAGS) 

clean:
	rm -rf *.o $(EXE) *.normal

