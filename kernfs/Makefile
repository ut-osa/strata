########
#
#   Simplified Makefile, for each source we call a function (make-goal) to
#	compile, this is done in the last line of this file.
#
########

SUBDIR	:= io cache concurrency global ds mlfs storage .

SRC_DIR   := $(addprefix , $(SUBDIR))
BUILD_DIR := $(addprefix build/, $(SUBDIR))
LIBFS_DIR := $(CURDIR)/../libfs

########
#  Source and Object files in their  module directories
########
SRC	:= $(foreach sdir, $(SRC_DIR), $(wildcard $(sdir)/*.c))
SRC := $(filter-out %mkfs.c %kernf.c %lease.c,$(SRC))
OBJ	:= $(patsubst %.c,build/%.o,$(SRC))
HDER := $(foreach sdir, $(SRC_DIR), $(wildcard $(sdir)/*.h))

########
#  Lib directories
########
NVML_DIR := $(abspath $(LIBFS_DIR)/lib/nvml/src)
DPDK_DIR := $(abspath $(LIBFS_DIR)/lib/dpdk-16.07/x86_64-native-linuxapp-gcc)
SPDK_DIR := $(abspath $(LIBFS_DIR)/lib/spdk)
LIBSPDK_DIR := $(abspath $(LIBFS_DIR)/src/storage/spdk/)
CUCKOO_FILTER_DIR := $(abspath $(LIBFS_DIR)/lib/cuckoofilter)
CUCKOO_DIR := $(abspath $(LIBFS_DIR)/lib/libcuckoo)
GLIB_DIR := $(abspath $(LIBFS_DIR)/lib/glib)
JEMALLOC_DIR := $(abspath $(LIBFS_DIR)/lib/jemalloc-4.5.0)
STORAGE_ENGINE := $(wildcard $(LIBFS_DIR)/build/storage/*.o)
#LIBSPDK := $(abspath $(LIBFS_DIR)/src/storage/spdk/libspdk.so)

INCLUDES  := $(addprefix -I, . $(LIBSPDK_DIR) $(NVML_DIR)/include $(CUCKOO_DIR) $(CUCKOO_FILTER_DIR)/src $(GLIB_DIR)/ $(GLIB_DIR)/glib)

LD_FLAGS = -lpthread -lm -L$(NVML_DIR)/nondebug/ -lpmem -lrt

#MLFS_FLAGS = -DUSE_SLAB -DMLFS_INFO -DKERNFS 
MLFS_FLAGS = -DMLFS_INFO -DKERNFS 
#MLFS_FLAGS = -DKERNFS 
MLFS_FLAGS += -DBALLOC
#MLFS_FLAGS += -DDIGEST_OPT
#MLFS_FLAGS += -DIOMERGE
#MLFS_FLAGS += -DCONCURRENT
#MLFS_FLAGS += -DFCONCURRENT
#MLFS_FLAGS += -DUSE_SSD
#MLFS_FLAGS += -DUSE_HDD
#MLFS_FLAGS += -DMIGRATION
#MLFS_FLAGS += -DEXPERIMENTAL

DEBUG = -g -O0 
#DEBUG = -g -Ofast
#DEBUG = -O3

########
#  vpath and compile function for each file
########
vpath %.c $(SRC_DIR)

#CC = clang-3.8
CC = gcc 
CXX = g++ -std=c++11

define make-goal
$1/%.o: %.c
	$(CC) $(INCLUDES) $(DEBUG) -fPIC -c $$< -o $$@ -DKERNFS $(MLFS_FLAGS)
endef

########
#  Phony targets
########
.PHONY: all checkdirs clean filesystem 

all: $(BUILD_DIR) filesystem

clean:
	@rm -rf ./build

########
#  Create dirs recursively
########
$(BUILD_DIR):
	@mkdir -p $@

########
#  Targets
########
filesystem: $(OBJ) $(SRC) $(HDER)
	#$(CC) -shared $(DEBUG) -o build/libkernfs.so $(OBJ) $(CUCKOO_FILTER_DIR)/libcuckoofilter.a $(LD_FLAGS) $(MLFS_FLAGS)
	$(CC) -shared $(DEBUG) -o build/libkernfs.so $(OBJ) $(LD_FLAGS) $(MLFS_FLAGS)

########
#  Compile each source into an object
########
$(foreach bdir,$(BUILD_DIR),$(eval $(call make-goal,$(bdir))))
