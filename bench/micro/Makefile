CC = gcc -std=c99
#CC = c99
EXE = iobench membench iobench_lat
#$(info $(EXE))

CUR_DIR = $(shell pwd)
PROJECT_ROOT = $(CUR_DIR)/../../
GLIBC_DIR = $(abspath $(PROJECT_ROOT)/shim/glibc-build/)
LIBFS_DIR = $(abspath $(PROJECT_ROOT)/libfs/build/)
NVML_DIR = $(abspath $(LIBFS_DIR)/../lib/nvml/src/nondebug/)
LIBFS = $(LIBFS_DIR)/libmlfs.a
LIBSPDK_DIR = $(abspath $(PROJECT_ROOT)/libfs/src/storage/spdk/)
LIBSPDK = $(abspath $(PROJECT_ROOT)/libfs/src/storage/spdk/libspdk.so)
LIBSPDK_INC = $(abspath $(PROJECT_ROOT)/libfs/src/storage/spdk/)

INCLUDES= $(LIBFS_DIR)/../src -I$(CUR_DIR)/../lib/nvml/src/include
DAX_OBJ = $(abspath $(PROJECT_ROOT)/libfs/build/storage/storage_dax.o)

DPDK_VER := 16.07.2
DPDK_DIR := $(abspath $(PROJECT_ROOT)/libfs/lib/dpdk-$(DPDK_VER)/x86_64-native-linuxapp-gcc)
SPDK_ROOT_DIR := $(abspath $(PROJECT_ROOT)/libfs/lib/spdk)
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
		  -lpthread -lrt -lm -lssl -lcrypto

all: $(EXE)

%.o: %.c
	$(CC) $(CFLAGS) -c -g $< -o $@

%.o: %.cc
	$(CXX) -std=c++11 $(CFLAGS) -c -g $< -o $@

time_stat.o: time_stat.c
	$(CC) time_stat.c -c -o time_stat.o -D_BSD_SOURCE

iobench: iobench.cc time_stat.o thread.cc
	$(CXX) -std=c++11 -g -Ofast -o $@ $^  -I$(INCLUDES) -L$(LIBFS_DIR) -lmlfs -L$(LIBSPDK_DIR) -lspdk -DMLFS $(CFLAGS) $(LDFLAGS) 
	$(CXX) -std=c++11 -g -Ofast -o $(addsuffix .normal, $@) $^ $(LIBSPDK) $(CFLAGS) $(DAX_OBJ) -L$(NVML_DIR) -lpmem -lpthread -lm -lrt -Wl,-rpath=$(abspath $(NVML_DIR))

iobench_lat: iobench_lat.cc time_stat.o thread.cc
	$(CXX) -std=c++11 -g -Ofast -o $@ $^  -I$(INCLUDES) -L$(LIBFS_DIR) -lmlfs -L$(LIBSPDK_DIR) -lspdk -DMLFS $(CFLAGS) $(LDFLAGS)
	$(CXX) -std=c++11 -g -Ofast -o $(addsuffix .normal, $@) $^ $(LIBSPDK) $(CFLAGS) $(DAX_OBJ) -L$(NVML_DIR) -lpmem -lpthread -lm -lrt -Wl,-rpath=$(abspath $(NVML_DIR))

membench: membench.cc time_stat.o thread.cc
	$(CXX) -std=c++11 -Ofast -g -o $@ $^ $(CFLAGS) -L$(LIBSPDK_DIR) -lspdk -L$(NVML_DIR) -lpmem -lpthread -lm -lrt -Wl,-rpath=$(abspath $(NVML_DIR)) -I$(INCLUDES) $(LDFLAGS)

strawman: time_stat.c thread.cc
	gcc time_stat.c -c -std=c99 -D_BSD_SOURCE -O2
	g++ -g -c iobench.cc -std=c++11 -lpthread -O2 -DUSER_BLOCK_MIGRATION
	g++ thread.cc -c -std=c++11 -O2
	#gcc aligned_block_migration.c -c
	g++ batch_migration.cc -c -std=c++11 -O2
	g++ migration_manager.cc -c -std=c++11 -msse4.1 -mavx -O2 -g
	g++ iobench.o thread.o batch_migration.o time_stat.o migration_manager.o -lpthread -std=c++11 -g -O2

clean:
	rm -rf *.o $(EXE) *.normal

