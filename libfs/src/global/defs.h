#ifndef _DEFS_H_
#define _DEFS_H_

#include <stdio.h>
#include <sys/types.h>
#include <sys/syscall.h>

#define get_tid() syscall(__NR_gettid)

#ifdef __cplusplus
extern "C" {
#endif

#define LOG_PATH "/tmp/mlfs_log/log.txt"
extern int log_fd;

#ifdef MLFS_LOG
#define mlfs_log(fmt, ...) \
	do { \
		dprintf(log_fd, "[%s():%d] " fmt,  \
				__func__, __LINE__, __VA_ARGS__); \
		fsync(log_fd); \
	} while (0)
#else
#define mlfs_log(...)
#endif

#define mlfs_muffled(...)

#ifdef MLFS_DEBUG
#define MLFS_INFO
#define mlfs_debug(fmt, ...) \
	do { \
		fprintf(stdout, "[%s():%d] " fmt,  \
				__func__, __LINE__, __VA_ARGS__); \
	} while (0)
#else
#define mlfs_debug(...)
#endif

#ifdef MLFS_INFO
#define mlfs_info(fmt, ...) \
	do { \
		fprintf(stdout, "[%lu][%s():%d] " fmt,  \
				get_tid(), __func__, __LINE__, __VA_ARGS__); \
	} while (0)
#else
#define mlfs_info(...)
#endif

#define mlfs_printf(fmt, ...) \
	do { \
		fprintf(stdout, "[%s():%d] " fmt,  \
				__func__, __LINE__, __VA_ARGS__); \
	} while (0)

//void _panic(void) __attribute__((noreturn));
void _panic(void);

#define panic(str) \
	do { \
		fprintf(stdout, "%s:%d %s(): %s\n",  \
				__FILE__, __LINE__, __func__, str); \
		fflush(stdout);	\
		GDB_TRAP; \
		_panic(); \
	} while (0)

#define stringize(s) #s
#define XSTR(s) stringize(s)

#if defined MLFS_DEBUG
void abort (void);
# if defined __STDC_VERSION__ && __STDC_VERSION__ >= 199901L
# define mlfs_assert(a) \
	do { \
		if (0 == (a)) { \
			fprintf(stderr, \
					"Assertion failed: %s, " \
					"%s(), %d at \'%s\'\n", \
					__FILE__, \
					__func__, \
					__LINE__, \
					XSTR(a)); \
			GDB_TRAP; \
			abort(); \
		} \
	} while (0)
# else
# define mlfs_assert(a) \
	do { \
		if (0 == (a)) { \
			fprintf(stderr, \
					"Assertion failed: %s, " \
					"%d at \'%s\'\n", \
					__FILE__, \
					__LINE__, \
					XSTR(a)); \
			GDB_TRAP; \
			abort(); \
		} \
	} while (0)
# endif
#else
#if 0
# define mlfs_assert(a) assert(a)
#else // turn on assert in release build.
# define mlfs_assert(a) \
	do { \
		if (0 == (a)) { \
			fprintf(stderr, \
					"Assertion failed: %s, " \
					"%s(), %d at \'%s\'\n", \
					__FILE__, \
					__func__, \
					__LINE__, \
					XSTR(a)); \
			GDB_TRAP; \
			abort(); \
		} \
	} while (0)
#endif
#endif

#define GDB_TRAP asm("int $3;");
#define COMPILER_BARRIER() asm volatile("" ::: "memory")

// util.c
struct pipe;

void pipeclose(struct pipe*, int);
int piperead(struct pipe*, char*, int);
int pipewrite(struct pipe*, char*, int);

// number of elements in fixed-size array
#define NELEM(x) (sizeof(x)/sizeof((x)[0]))

#ifdef __cplusplus
}
#endif

#endif

