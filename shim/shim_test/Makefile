targets = fs_syscall

GLIBC_PATH=../glibc-build

CUR_DIR = $(shell pwd)
LIBFS_DIR = $(CUR_DIR)/../../libfs/

#LDFLAGS = -Wl,--version-script=shim.map \

LDFLAGS = -Wl,-rpath=$(abspath $(GLIBC_PATH)) \
		  -Wl,-rpath=$(abspath $(LIBFS_DIR)) \
		  -Wl,-dynamic-linker=$(abspath $(GLIBC_PATH))/ld-linux-x86-64.so.2 

all: $(targets)

fs_syscall: fs_syscall.o
	$(CC) -g -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -g $< -o $@

%.o: %.S
	$(AS) $(ASFLAGS) -c -g $< -o $@

clean:
	rm -rf $(targets) *.o
