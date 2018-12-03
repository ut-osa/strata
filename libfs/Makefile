########
#
#   Simplified Makefile, for each source we call a function (make-goal) to
#	compile, this is done in the last line of this file.
#
########

#CC=clang-3.8
#CC=gcc
CXX=g++ --std=c++11
CC=gcc

########
#  List of all modules' directories
########
MODULES   := io concurrency config filesystem global ds log mkfs \
	mlfs posix storage

########
#  List of all modules' directories
########

########
#  All modules' directories in src and build
########
SRC_DIR   := $(addprefix src/,$(MODULES))
BUILD_DIR := $(addprefix build/,$(MODULES))

########
#  Source and Object files in their  module directories
########
SRC       := $(foreach sdir,$(SRC_DIR),$(wildcard $(sdir)/*.c))
OBJ       := $(patsubst src/%.c,build/%.o,$(SRC))

########
#  Lib directories
########
NVML_DIR := $(abspath $(CURDIR)/lib/nvml/src)
SPDK_DIR := $(abspath $(CURDIR)/lib/spdk)
LIBSPDK_DIR := $(abspath $(CURDIR)/src/storage/spdk/)
JEMALLOC_DIR := $(abspath $(CURDIR)/lib/jemalloc-4.5.0)

INCLUDES  := $(addprefix -I,src/ $(LIBSPDK_DIR) $(NVML_DIR)/include)

LD_FLAGS = -lpthread -lm -L$(NVML_DIR)/nondebug/ -lpmem -lrt #-Wl,-fuse-ld=gold

#MLFS_FLAGS = -DUSE_SLAB -DLIBFS -DMLFS_INFO
#MLFS_FLAGS = -DLIBFS
MLFS_FLAGS = -DLIBFS -DMLFS_INFO
#MLFS_FLAGS += -DCONCURRENT
MLFS_FLAGS += -DINVALIDATION
MLFS_FLAGS += -DKLIB_HASH
#MLFS_FLAGS += -DLEASE_TIMEOUT_TEST
#MLFS_FLAGS += -DUSE_SSD
#MLFS_FLAGS += -DUSE_HDD
#MLFS_FLAGS += -DMLFS_LOG

#DEBUG = -g3 -O0 
#DEBUG = -O0 -gdwarf-2 -g3 -rdynamic
DEBUG = -g -Ofast
#DEBUG = -O3

########
#  vpath and compile function for each file
########
vpath %.c $(SRC_DIR)

define make-goal
$1/%.o: %.c 
	$(CC) $(INCLUDES) $(MLFS_FLAGS) $(DEBUG) -fPIC -c $$< -o $$@ -DLIBFS
endef

########
#  Phony targets
########
.PHONY: all checkdirs clean mkfs spdk

all: checkdirs spdk libmlfs mkfs
checkdirs: $(BUILD_DIR)
	@mkdir -p bin

clean:
	@rm -rf $(BUILD_DIR)
	make -C src/storage/spdk/ clean

########
#  Create dirs recursively
########
$(BUILD_DIR):
	@mkdir -p $@

########
#  Targets
########
#build/libmlfs.a: $(OBJ)
libmlfs: $(OBJ) spdk
	ar cr build/libmlfs.a $(OBJ) 
	$(CC) -shared $(DEBUG) -o build/libmlfs.so $(OBJ) $(LD_FLAGS) -DLIBFS $(MLFS_FLAGS) -L$(LIBSPDK_DIR) -lspdk -L$(JEMALLOC_DIR)/lib -Wl,-rpath=$(abspath $(JEMALLOC_DIR)/lib) -ljemalloc 

spdk: $(OBJ)
	make -C src/storage/spdk/

mkfs: $(OBJ) libmlfs
	$(CC) -o build/mkfs.o -c src/mkfs/mkfs.c $(INCLUDES) $(MLFS_FLAGS)
	$(CC) -g -o bin/mkfs.mlfs  build/libmlfs.a $(INCLUDES) -L$(LIBSPDK_DIR) -lspdk $(LD_FLAGS) -DLIBFS -Wl,-rpath=$(abspath $(LIBSPDK_DIR)) -Wl,-rpath=$(abspath $(NVML_DIR)/nondebug) 


########
#  Compile each source into an object
########
$(foreach bdir,$(BUILD_DIR),$(eval $(call make-goal,$(bdir))))
