#include <execinfo.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>

#include "syscall_trace.h"
#include "shim_init.h"

#define trace_buf_size 40

static void show_trace_pread64(const char *name, int ret, va_list valist)
{
	int fd, i = 0;
	char *buf, trace_buf[trace_buf_size];
	size_t count, off, j = 0;

	memset(trace_buf, 0, trace_buf_size);

	fd = va_arg(valist, int);
	buf = va_arg(valist, char *);
	count = va_arg(valist, size_t);
	off = va_arg(valist, size_t);

	while (i < trace_buf_size - 1 && j < count) {
		// if \0 ~ buf[j] ~ \31 in ascii table.
		if (buf[j] < 32) {
			trace_buf[i++] = '\\';
			trace_buf[i++] = '0';
			j++;
		} else
			trace_buf[i++] = buf[j++];

	}

	fprintf(stderr, "\nTRACE %s  (%d, \"%s\", %lu, %lu) = %d (%s)\n",
			name, fd, trace_buf, count, off, ret, strerror(-ret));
}

static void show_trace_write(const char *name, int ret, va_list valist)
{
	int fd, i = 0;
	char *buf, trace_buf[trace_buf_size];
	size_t count, j = 0;

	memset(trace_buf, 0, trace_buf_size);

	fd = va_arg(valist, int);
	buf = va_arg(valist, char *);
	count = va_arg(valist, size_t);

	while (i < trace_buf_size - 1  && j < count) {
		// if \0 ~ buf[j] ~ \31 in ascii table.
		if (buf[j] < 32) {
			trace_buf[i++] = '\\';
			trace_buf[i++] = '0';
			j++;
		} else
			trace_buf[i++] = buf[j++];

	}

	fprintf(stderr, "\nTRACE %s  (%d, \"%s\", %lu) = %d (%s)\n",
			name, fd, trace_buf, count,  ret, strerror(-ret));
}

typedef enum {PATH, FD, NONE} arg_class;

int get_arg_class(const char *name)
{
	if (strcmp(name, "shim_do_sync") == 0 ||
		strcmp(name, "shim_do_mmap") == 0 ||
		strcmp(name, "shim_do_munmap") == 0)
		return NONE;

	if (strcmp(name, "shim_do_open") == 0 ||
		strcmp(name, "shim_do_openat") == 0 ||
		strcmp(name, "shim_do_creat") == 0 ||
		strcmp(name, "shim_do_mkdir") == 0 ||
		strcmp(name, "shim_do_stat") == 0 ||
		strcmp(name, "shim_do_lstat") == 0 ||
		strcmp(name, "shim_do_truncate") == 0 ||
		strcmp(name, "shim_do_access") == 0 ||
		strcmp(name, "shim_do_rename") == 0 ||
		strcmp(name, "shim_do_unlink") == 0)
		return PATH;
	else
		return FD;
}
		
void do_trace(const char *name, int ret, int args, ...)
{
	va_list valist;
	int arg_type;

	va_start(valist, args);

	if (strcmp(name, "shim_do_pread64") == 0) {
		show_trace_pread64(name, ret, valist);
		goto end_trace;
	} else if (strcmp(name, "shim_do_write") == 0) {
		show_trace_write(name, ret, valist);
		goto end_trace;
	}

	arg_type = get_arg_class(name);

	switch(arg_type) {
		case PATH:
			fprintf(stderr, "\nTRACE %s (path: %s) = %d (%s)\n", name, 
					va_arg(valist, char *), ret, strerror(-ret));
			break;
		case FD:
			fprintf(stderr, "\nTRACE %s (fd: %d) = %d (%s)\n", name, 
					va_arg(valist, int), ret, strerror(-ret));
			break;
		case NONE:
			fprintf(stderr, "\nTRACE %s: = %d\n (%s)\n", name, ret, strerror(-ret));
			break;
	}

end_trace:
	va_end(valist);

	return;
}
