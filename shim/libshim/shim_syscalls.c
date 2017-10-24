#include "interfaces.h"

#include "shim_syscalls.h"

// Generate system call stub to call shim_do_xxx in shim_table.
#include "shim_syscall_macro.h"
#include "define_shim_syscalls.h"

int start_interpose __attribute((weak)) = 0;
