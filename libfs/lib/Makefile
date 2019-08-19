.PHONY: download all clean

all:
	make -C nvml -j8
	(cd syscall_intercept; \
	 mkdir build; \
	 mkdir install; \
	 cd build; \
	 cmake -DCMAKE_INSTALL_PREFIX=$(shell pwd)/syscall_intercept/install -DCMAKE_BUILD_TYPE=Release ..; \
	 make; \
	 make install)

#shouldn't redownload DPDK, there is an issue right now with fPIC; it's manually
#added in its Makefile (mk/rte.lib.mk, line 53 has CFLAGS += -fPIC).
#DPDK has a config to make it shared (CONFIG_RTE_BUILD_SHARED_LIB=y)
#but it breaks SPDK compilation
redownload:
	########
	# 	NVML
	########
	@rm -rf nvml
	git clone https://github.com/pmem/nvml
	########
	#       syscall_intercept
	########
	@rm -rf syscall_intercept
	git clone https://github.com/pmem/syscall_intercept.git

clean:
	make clean -C nvml
