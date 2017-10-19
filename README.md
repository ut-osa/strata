Strata: A Cross Media File System
==================================
Assuming current directory is project root directory.

### build ###
1. Build kernel
cd kernel/kbuild
make -f Makefile.setup .config
make -f Makefile.setup
make -j

2. Build glibc
cd shim
make

3. Build dependent libraries (SPDK, NVML, JEMALLOC)
cd libfs/lib
git clone https://github.com/pmem/nvml
make
tar xvjf jemalloc-4.5.0.tar.bz2
./autogen
./configure
make

4. Build Libfs
cd libfs
make

5. Build KernelFS
cd kernfs
make
cd test
make

### Running Strata ###

Strata emulates NVM using a physically contiguous memory region, and relies on the kernel NVDIMM support.

You need to make sure that your kernel is built with NVDIMM support enabled (CONFIG_BLK_DEV_PMEM), and then you can reserve the memory space by booting the kernel with memmap command line option.
For instance, adding memmap=16G!8G to the kernel boot parameters will reserve 16GB memory starting from 8GB address, and the kernel will create a pmem0 block device under the /dev directory.

Details are available at:
http://pmem.io/2016/02/22/pm-emulation.html

