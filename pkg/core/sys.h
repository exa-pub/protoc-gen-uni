/* Platform layer: the only code that talks to the operating system (POSIX read/write, or the
 * Windows CRT in binary mode). Everything else is plain C11. */
#ifndef SYS_H
#define SYS_H

#include "util.h"

enum { SYS_STDIN = 0, SYS_STDOUT = 1, SYS_STDERR = 2 };

/* Reads all of stdin (binary) into b. Returns false on an I/O error. */
bool sys_read_all(Buf *b);
/* Writes p[0:n] to fd (binary). Returns false on an I/O error. */
bool sys_write_all(int fd, const void *p, size_t n);
/* Writes a message to stderr and exits with status 1 immediately. */
NORETURN void sys_die(const char *msg);

#endif
