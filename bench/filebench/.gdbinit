define load_sl
	sharedlibrary libshim.so
	sharedlibrary libmlfs.so
end

set follow-fork-mode child
set follow-exec-mode new

#set auto-solib-add on
set auto-load safe-path ../../shim/glibc-build:../../../shim/glibc-2.19
set libthread-db-search-path ../../shim/glibc-build/nptl_db/
set environment LD_PRELOAD ../../shim/libshim/libshim.so
#set environment LD_PRELOAD ../../shim/libshim/libshim.so:../../deps/mutrace/.libs/libmutrace.so
set environment LD_LIBRARY_PATH ../../libfs/lib/nvml/src/nondebug/:../../libfs/build/:../../shim/glibc-build/rt/:/usr/local/lib/:/usr/lib/x86_64-linux-gnu/:/lib/x86_64-linux-gnu/
set environment MLFS 1
#set environment MUTRACE_HASH_SIZE=10000000
#set environment MUTRACE_TRAP 1

# loading python modules.
source ../../../gdb_python_modules/load_modules.py

define do_run
b  1679
r -f varmail_mlfs.f
#r -f webserver_mlfs.f
#r -f fileserver_mlfs.f
#b mlfs_posix_open
#b mlfs_posix_unlink
#b flowoplib_createfile
#b flowoplib_openfile_common
end

define sigcont
signal SIGCONT
end

define do_normal_run
end

# this is macro to setup for breakpoint
define setup_br
	b 60
# run programe
	r w aa 40K

	l add_to_log
	b 541  if $caller_is("posix_write", 8)
	#b balloc if $caller_is("posix_write", 8)
end
