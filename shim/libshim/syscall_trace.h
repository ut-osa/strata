#ifndef _SYSCALL_TRACE_H_
#define _SYSCALL_TRACE_H_

#include <stdio.h>

void do_trace(const char *name, int ret, int args, ...);

#ifdef SYS_TRACE
#define syscall_trace(name, ret, args, ...) \
	do_trace(name, ret, args, __VA_ARGS__);
#else
#define syscall_trace(name, ret, args, ...) 
#endif 

#endif
