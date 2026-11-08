/* protoc-gen-uni: a single protoc plugin that bundles byte-exact ports of upstream plugins. The
 * generator is selected with the uni-name and uni-version parameters, all other parameters are
 * passed to it unchanged:
 *
 *	opt: [uni-name=bufbuild/es, uni-version=v2.17.0, target=ts]
 *
 * Reads a CodeGeneratorRequest from stdin and writes the CodeGeneratorResponse to stdout
 * (pkg/uni). Flags: --version, --list ("<name> <version>" per generator, for tests). */
#include "registry.h"
#include "sys.h"

#include <string.h>

#ifndef VERSION
#define VERSION "dev"
#endif

static int write_str(int fd, const char *s) { return sys_write_all(fd, s, strlen(s)) ? 0 : 1; }

/* Command line flags: this is a protoc plugin, it has no arguments. */
static COLD int flags(int argc, char **argv)
{
	if (argc == 2 && str_eq(argv[1], "--version"))
		return write_str(SYS_STDOUT, VERSION "\n");
	if (argc == 2 && str_eq(argv[1], "--list")) {
		Buf b = {0};
		for (int i = 0; i < uni_n_generators; i++)
			buf_adds(&b, xcat(uni_generators[i]->name, " ", uni_generators[i]->version, "\n"));
		return write_str(SYS_STDOUT, buf_str(&b));
	}
	/* fmt's %q of a []string: ["a" "b"] */
	Buf b = {0};
	buf_adds(&b, "protoc-gen-uni: unexpected arguments [");
	for (int i = 1; i < argc; i++)
		buf_adds(&b, xcat(i > 1 ? " " : "", quote_go(str_c(argv[i]))));
	buf_adds(&b, "]; this is a protoc plugin, run it via protoc or buf\n");
	write_str(SYS_STDERR, buf_str(&b));
	return 1;
}

int main(int argc, char **argv)
{
	if (argc > 1)
		return flags(argc, argv);
	Buf in = {0}, out = {0};
	if (!sys_read_all(&in))
		sys_die("protoc-gen-uni: read request: I/O error\n");
	const char *err = uni_main((const uint8_t *)in.p, in.n, uni_generators, uni_n_generators, &out);
	if (err)
		sys_die(xcat(err, "\n"));
	if (!sys_write_all(SYS_STDOUT, out.p, out.n))
		sys_die("protoc-gen-uni: write response: I/O error\n");
	return 0;
}
