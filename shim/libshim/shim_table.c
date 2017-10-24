#include <stdio.h>
#include "shim_syscall_macro.h"
#include "shim_table.h"
#include "shim_syscalls.h"

void syscall_instruction(void)
{
	asm("syscall;");
}

shim_fp shim_table [SHIM_NSYSCALLS] = {
	// For system call passthrough
	[0 ... (SHIM_NSYSCALLS - 1)] = (shim_fp) syscall_instruction, 
#include <asm/unistd.h>

#undef DEFINE_SHIM_SYSCALL
#define DEFINE_SHIM_SYSCALL(name, n, func, ...) \
	[__NR_##name] = (shim_fp) __shim_##name, 
#include "define_shim_syscalls.h"
#undef DEFINE_SHIM_SYSCALL
};
