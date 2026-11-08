#include "sys.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#define sys_read _read
#define sys_write _write
typedef int ssize_t_;
#else
#include <unistd.h>
#define sys_read read
#define sys_write write
typedef ssize_t ssize_t_;
#endif

/* Windows: stdin and stdout are opened in text mode, which would translate "\r\n" and stop at
 * 0x1a. The request and the response are binary. */
static void set_binary(int fd)
{
#ifdef _WIN32
	_setmode(fd, _O_BINARY);
#else
	(void)fd;
#endif
}

bool sys_read_all(Buf *b)
{
	set_binary(SYS_STDIN);
	for (;;) {
		buf_grow(b, 65536);
		size_t room = b->cap - b->n - 1;
		if (room > 1u << 30)
			room = 1u << 30;
		ssize_t_ n = sys_read(SYS_STDIN, b->p + b->n, (unsigned)room);
		if (n == 0)
			return true;
		if (n < 0) {
			if (errno == EINTR)
				continue;
			return false;
		}
		b->n += (size_t)n;
		b->p[b->n] = 0;
	}
}

bool sys_write_all(int fd, const void *p, size_t n)
{
	set_binary(fd);
	const char *s = p;
	while (n) {
		unsigned chunk = n > 1u << 30 ? 1u << 30 : (unsigned)n;
		ssize_t_ w = sys_write(fd, s, chunk);
		if (w < 0 && errno == EINTR)
			continue;
		if (w <= 0)
			return false;
		s += w, n -= (size_t)w;
	}
	return true;
}

void sys_die(const char *msg)
{
	sys_write_all(SYS_STDERR, msg, strlen(msg));
	exit(1);
}
