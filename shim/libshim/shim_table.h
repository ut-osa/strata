#ifndef _SHIM_TABLE_H_
#define _SHIM_TABLE_H_

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "shim_syscall_macro.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*shim_fp)(void);

extern shim_fp shim_table [SHIM_NSYSCALLS];

#if 0
// Create declarations
#define DECLARE_SYSCALL_STUB(name, args ...) \
	SHIM_ARG_TYPE __shim_##name(args);

/*
#define DECLARE_SYSCALL_HANDLER(name, func, ret_type, args ...) \
	ret_type func(args);
*/

#define DECLARE_SHIM_SYSCALL(name, n, func, ret_type, args ...) \
	DECLARE_SYSCALL_STUB(name, SHIM_PROTO_ARGS_##n) 
	//DECLARE_SYSCALL_HANDLER(name, func, ret_type, args ...)

#include "define_shim_syscalls.h"
//#undef DECLARE_SHIM_SYSCALL
#endif

#ifdef __cplusplus
}
#endif

#endif
