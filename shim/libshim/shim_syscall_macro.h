/* Syscall macro code adapted from Graphene and inktag/sego project */

#ifndef _SHIM_MACRO_H_
#define _SHIM_MACRO_H_

#include <asm/unistd.h>
#include "shim_types.h"
#include "syscall_trace.h"

#ifdef __cplusplus
extern "C" {
#endif

#define attribute_hidden __attribute__ ((visibility ("hidden")))

#define alias_str(name) #name

#define extern_alias(name) \
    extern __typeof(name) shim_##name __attribute ((alias (alias_str(name))))

# define BEGIN_SYSCALL_PROFILE()        do {} while (0)
# define END_SYSCALL_PROFILE(name)      do {} while (0)

#define DEBUG_TRAP asm("int $3;");

#define SHIM_NSYSCALLS 311

typedef void (*shim_fp)(void);
//extern int start_interpose __attribute__ ((weak));

#define SHIM_ARG_TYPE long

#define BEGIN_SHIM(name, args ...)				\
		SHIM_ARG_TYPE __shim_##name (args) {	\
        SHIM_ARG_TYPE ret = 0;					\
        BEGIN_SYSCALL_PROFILE();

#define END_SHIM(name, ...)				\
        END_SYSCALL_PROFILE(name);				\
        return ret;								\
    }


/*
#define MAKE_SYSCALL_INSTRUCTION(name, ret_type, ...) \
	asm("mov $__NR_##name, %%rax;"	\
		"syscall;"					\
		);							\
*/

#define DEFINE_SHIM_SYSCALL(name, n, func, ...)		\
    SHIM_SYSCALL_##n (name, func, __VA_ARGS__)		\
    EXPORT_SHIM_SYSCALL (name, n, __VA_ARGS__)		
	//MAKE_SYSCALL_INSTRUCTION(name, __VA_ARGS__)

#define PROTO_ARGS_0() void
#define PROTO_ARGS_1(t, a) t a
#define PROTO_ARGS_2(t, a, rest ...) t a, PROTO_ARGS_1(rest)
#define PROTO_ARGS_3(t, a, rest ...) t a, PROTO_ARGS_2(rest)
#define PROTO_ARGS_4(t, a, rest ...) t a, PROTO_ARGS_3(rest)
#define PROTO_ARGS_5(t, a, rest ...) t a, PROTO_ARGS_4(rest)
#define PROTO_ARGS_6(t, a, rest ...) t a, PROTO_ARGS_5(rest)

#define CAST_ARGS_0()
#define CAST_ARGS_1(t, a) (SHIM_ARG_TYPE) a
#define CAST_ARGS_2(t, a, rest ...) (SHIM_ARG_TYPE) a, CAST_ARGS_1(rest)
#define CAST_ARGS_3(t, a, rest ...) (SHIM_ARG_TYPE) a, CAST_ARGS_2(rest)
#define CAST_ARGS_4(t, a, rest ...) (SHIM_ARG_TYPE) a, CAST_ARGS_3(rest)
#define CAST_ARGS_5(t, a, rest ...) (SHIM_ARG_TYPE) a, CAST_ARGS_4(rest)
#define CAST_ARGS_6(t, a, rest ...) (SHIM_ARG_TYPE) a, CAST_ARGS_5(rest)

#define DEFINE_SHIM_FUNC(func, n, r, args ...)             \
    r func (PROTO_ARGS_##n (args));

#define TYPE_HASH(t) ({ const char * _s = #t;              \
       ((uint16_t) _s[0] << 8) +  _s[1]; })

#define POINTER_TYPE(t) ({ int _h = TYPE_HASH(t);                   \
       _h == TYPE_HASH(void *) || _h == TYPE_HASH(char *) ||        \
       _h == TYPE_HASH(const); })

#define EXPORT_SHIM_SYSCALL(name, n, r, args ...)                   \
    r shim_##name (PROTO_ARGS_##n (args)) {                         \
        SHIM_ARG_TYPE ret =  __shim_##name (CAST_ARGS_##n (args));  \
        if (POINTER_TYPE(r)) {                                      \
            if ((long) ret >= -4095L) return (r) 0;                 \
        } else {                                                    \
            if ((int) ret < 0) return (r) -1;                       \
        }                                                           \
        return (r) ret;                                             \
    }

// placeholders for debugging.
#define PARSE_SYSCALL1(name, ...)
#define PARSE_SYSCALL2(name, ...)

#define SHIM_SYSCALL_0(name, func, r)                           \
    BEGIN_SHIM(name, void)                                      \
        PARSE_SYSCALL1(name, 0);                                \
        r __ret = func();                                       \
        PARSE_SYSCALL2(name, 0, #r, __ret);                     \
        ret = (SHIM_ARG_TYPE) __ret;                            \
    END_SHIM(name, void)                                      

#define SHIM_SYSCALL_1(name, func, r, t1, a1)                               \
    BEGIN_SHIM(name, SHIM_ARG_TYPE __arg1)                                  \
        t1 a1 = (t1) __arg1;                                                \
        PARSE_SYSCALL1(name, 1, #t1, a1);                                   \
        r __ret = func(a1);                                                 \
        PARSE_SYSCALL2(name, 1, #r, __ret, #t1, a1);                        \
        ret = (SHIM_ARG_TYPE) __ret;                                        \
    END_SHIM(name, SHIM_ARG_TYPE __arg1)                                  

#define SHIM_SYSCALL_2(name, func, r, t1, a1, t2, a2)                       \
    BEGIN_SHIM(name, SHIM_ARG_TYPE __arg1, SHIM_ARG_TYPE __arg2)            \
        t1 a1 = (t1) __arg1;                                                \
        t2 a2 = (t2) __arg2;                                                \
        PARSE_SYSCALL1(name, 2, #t1, a1, #t2, a2);                          \
        r __ret = func(a1, a2);                                             \
        PARSE_SYSCALL2(name, 2, #r, __ret, #t1, a1, #t2, a2);               \
        ret = (SHIM_ARG_TYPE) __ret;                                        \
   END_SHIM(name, SHIM_ARG_TYPE __arg1, SHIM_ARG_TYPE __arg2)            

#define SHIM_SYSCALL_3(name, func, r, t1, a1, t2, a2, t3, a3)               \
    BEGIN_SHIM(name, SHIM_ARG_TYPE __arg1, SHIM_ARG_TYPE __arg2,            \
                     SHIM_ARG_TYPE __arg3)                                  \
        t1 a1 = (t1) __arg1;                                                \
        t2 a2 = (t2) __arg2;                                                \
        t3 a3 = (t3) __arg3;                                                \
        PARSE_SYSCALL1(name, 3, #t1, a1, #t2, a2, #t3, a3);                 \
        r __ret = func(a1, a2, a3);                                         \
        PARSE_SYSCALL2(name, 3, #r, __ret, #t1, a1, #t2, a2, #t3, a3);      \
        ret = (SHIM_ARG_TYPE) __ret;                                        \
    END_SHIM(name, SHIM_ARG_TYPE __arg1, SHIM_ARG_TYPE __arg2,				\
                     SHIM_ARG_TYPE __arg3)                                  

#define SHIM_SYSCALL_4(name, func, r, t1, a1, t2, a2, t3, a3, t4, a4)       \
    BEGIN_SHIM(name, SHIM_ARG_TYPE __arg1, SHIM_ARG_TYPE __arg2,            \
                     SHIM_ARG_TYPE __arg3, SHIM_ARG_TYPE __arg4)            \
        t1 a1 = (t1) __arg1;                                                \
        t2 a2 = (t2) __arg2;                                                \
        t3 a3 = (t3) __arg3;                                                \
        t4 a4 = (t4) __arg4;                                                \
        PARSE_SYSCALL1(name, 4, #t1, a1, #t2, a2, #t3, a3, #t4, a4);        \
        r __ret = func(a1, a2, a3, a4);                                     \
        PARSE_SYSCALL2(name, 4, #r, __ret, #t1, a1, #t2, a2, #t3, a3,       \
                       #t4, a4);                                            \
        ret = (SHIM_ARG_TYPE) __ret;                                        \
    END_SHIM(name, SHIM_ARG_TYPE __arg1, SHIM_ARG_TYPE __arg2,				\
                     SHIM_ARG_TYPE __arg3, SHIM_ARG_TYPE __arg4)            

#define SHIM_SYSCALL_5(name, func, r, t1, a1, t2, a2, t3, a3, t4, a4, t5, a5) \
    BEGIN_SHIM(name, SHIM_ARG_TYPE __arg1, SHIM_ARG_TYPE __arg2,            \
                     SHIM_ARG_TYPE __arg3, SHIM_ARG_TYPE __arg4,            \
                     SHIM_ARG_TYPE __arg5)                                  \
        t1 a1 = (t1) __arg1;                                                \
        t2 a2 = (t2) __arg2;                                                \
        t3 a3 = (t3) __arg3;                                                \
        t4 a4 = (t4) __arg4;                                                \
        t5 a5 = (t5) __arg5;                                                \
        PARSE_SYSCALL1(name, 5, #t1, a1, #t2, a2, #t3, a3, #t4, a4,         \
                       #t5, a5);                                            \
        r __ret = func(a1, a2, a3, a4, a5);                                 \
        PARSE_SYSCALL2(name, 5, #r, __ret, #t1, a1, #t2, a2, #t3, a3,       \
                       #t4, a4, #t5, a5);                                   \
        ret = (SHIM_ARG_TYPE) __ret;                                        \
    END_SHIM(name, SHIM_ARG_TYPE __arg1, SHIM_ARG_TYPE __arg2,				\
                     SHIM_ARG_TYPE __arg3, SHIM_ARG_TYPE __arg4,            \
                     SHIM_ARG_TYPE __arg5)                                  

#define SHIM_SYSCALL_6(name, func, r, t1, a1, t2, a2, t3, a3, t4, a4, t5, a5, t6, a6) \
    BEGIN_SHIM(name, SHIM_ARG_TYPE __arg1, SHIM_ARG_TYPE __arg2,            \
                     SHIM_ARG_TYPE __arg3, SHIM_ARG_TYPE __arg4,            \
                     SHIM_ARG_TYPE __arg5, SHIM_ARG_TYPE __arg6)            \
        t1 a1 = (t1) __arg1;                                                \
        t2 a2 = (t2) __arg2;                                                \
        t3 a3 = (t3) __arg3;                                                \
        t4 a4 = (t4) __arg4;                                                \
        t5 a5 = (t5) __arg5;                                                \
        t6 a6 = (t6) __arg6;                                                \
        PARSE_SYSCALL1(name, 6, #t1, a1, #t2, a2, #t3, a3, #t4, a4,         \
                       #t5, a5, #t6, a6);                                   \
        r __ret = func(a1, a2, a3, a4, a5, a6);                             \
        PARSE_SYSCALL2(name, 6, #r, __ret, #t1, a1, #t2, a2, #t3, a3,       \
                       #t4, a4, #t5, a5, #t6, a6);  \
        ret = (SHIM_ARG_TYPE) __ret;                                        \
    END_SHIM(name, SHIM_ARG_TYPE __arg1, SHIM_ARG_TYPE __arg2,				\
                     SHIM_ARG_TYPE __arg3, SHIM_ARG_TYPE __arg4,            \
                     SHIM_ARG_TYPE __arg5, SHIM_ARG_TYPE __arg6)            

#define SHIM_PROTO_ARGS_0 void
#define SHIM_PROTO_ARGS_1 SHIM_ARG_TYPE __arg1
#define SHIM_PROTO_ARGS_2 SHIM_PROTO_ARGS_1, SHIM_ARG_TYPE __arg2
#define SHIM_PROTO_ARGS_3 SHIM_PROTO_ARGS_2, SHIM_ARG_TYPE __arg3
#define SHIM_PROTO_ARGS_4 SHIM_PROTO_ARGS_3, SHIM_ARG_TYPE __arg4
#define SHIM_PROTO_ARGS_5 SHIM_PROTO_ARGS_4, SHIM_ARG_TYPE __arg5
#define SHIM_PROTO_ARGS_6 SHIM_PROTO_ARGS_5, SHIM_ARG_TYPE __arg6

#if 0
#define SHIM_PASS_ARGS_1 __arg1
#define SHIM_PASS_ARGS_2 SHIM_PASS_ARGS_1, __arg2
#define SHIM_PASS_ARGS_3 SHIM_PASS_ARGS_2, __arg3
#define SHIM_PASS_ARGS_4 SHIM_PASS_ARGS_3, __arg4
#define SHIM_PASS_ARGS_5 SHIM_PASS_ARGS_4, __arg5
#define SHIM_PASS_ARGS_6 SHIM_PASS_ARGS_5, __arg6

#define DO_SYSCALL(...) DO_SYSCALL2(__VA_ARGS__)
#define DO_SYSCALL2(n, ...) -ENOSYS

#define DO_SYSCALL_0(sysno) -ENOSYS
#define DO_SYSCALL_1(sysno, ...) DO_SYSCALL(1, sysno, SHIM_PASS_ARGS_1)
#define DO_SYSCALL_2(sysno, ...) DO_SYSCALL(2, sysno, SHIM_PASS_ARGS_2)
#define DO_SYSCALL_3(sysno, ...) DO_SYSCALL(3, sysno, SHIM_PASS_ARGS_3)
#define DO_SYSCALL_4(sysno, ...) DO_SYSCALL(4, sysno, SHIM_PASS_ARGS_4)
#define DO_SYSCALL_5(sysno, ...) DO_SYSCALL(5, sysno, SHIM_PASS_ARGS_5)
#define DO_SYSCALL_6(sysno, ...) DO_SYSCALL(6, sysno, SHIM_PASS_ARGS_6)

#define SHIM_SYSCALL_PASSTHROUGH(name, n, ...)                      \
    DEFINE_PROFILE_INTERVAL(syscall_##name##_slow, syscall);        \
    DEFINE_PROFILE_INTERVAL(syscall_##name, syscall);               \
    BEGIN_SHIM(name, SHIM_PROTO_ARGS_##n)                           \
        debug("WARNING: shim_" #name " not implemented\n");         \
        ret = DO_SYSCALL_##n(__NR_##name);                          \
    END_SHIM(name)                                                  \
    EXPORT_SHIM_SYSCALL(name, n, __VA_ARGS__)
#endif

#ifndef container_of
/**
 * container_of - cast a member of a structure out to the containing structure
 * @ptr:	the pointer to the member.
 * @type:	the type of the container struct this is embedded in.
 * @member:	the name of the member within the struct.
 *
 */
#define container_of(ptr, type, member) ({			\
	const typeof( ((type *)0)->member ) *__mptr = (ptr);	\
	(type *)( (char *)__mptr - offsetof(type,member) );})
#endif

#define CONCAT2(t1, t2) __CONCAT2(t1, t2)
#define __CONCAT2(t1, t2) t1##_##t2

#define CONCAT3(t1, t2, t3) __CONCAT3(t1, t2, t3)
#define __CONCAT3(t1, t2, t3) t1##_##t2##_##t3

#ifdef __cplusplus
}
#endif

#endif
