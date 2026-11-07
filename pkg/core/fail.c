/* fail()/try_run(): errors unwind with longjmp to the innermost try_run, like panic/recover in
 * the Go implementation. Nothing needs cleanup: all memory is in the arena. */
#include "sys.h"

#ifdef _WIN32
/* mingw-w64: setjmp without SEH, so longjmp restores registers without unwinding the stack (which
 * would need unwind tables). */
#define __USE_MINGW_SETJMP_NON_SEH
#endif
#include <setjmp.h>

static jmp_buf *fail_jmp;
static Str fail_msg;

void fail_str(Str msg)
{
	fail_msg = msg;
	if (!fail_jmp)
		sys_die(xcat("protoc-gen-uni: ", msg.p, "\n"));
	longjmp(*fail_jmp, 1);
}

void fail(const char *fmt, ...)
{
	Buf b = {0};
	va_list ap;
	va_start(ap, fmt);
	buf_vfmt(&b, fmt, ap);
	va_end(ap);
	fail_str(buf_view(&b));
}

Str fail_message(void) { return fail_msg; }

const char *try_run(void (*fn)(void *), void *arg)
{
	jmp_buf jb, *volatile prev = fail_jmp;
	fail_jmp = &jb;
	if (setjmp(jb)) {
		fail_jmp = prev;
		return fail_msg.p;
	}
	fn(arg);
	fail_jmp = prev;
	return NULL;
}
