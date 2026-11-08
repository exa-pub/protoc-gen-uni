/* Unit tests of shlex.c, optparse.c and format.c. The expected values were printed by CPython
 * 3.13 (shlex.split, shlex.shlex with whitespace ",", OptionParser.parse_args, str % str). */
#include "python.h"

#include "test.h"

#include <string.h>

static const char *lex(const char *s, bool posix)
{
	Str *v;
	const char *err;
	int n = py_shlex(str_c(s), posix, posix ? " \t\r\n" : ",", &v, &err);
	if (err)
		return xcat("ERR ", err);
	Buf b = {0};
	for (int i = 0; i < n; i++) {
		buf_addc(&b, '[');
		buf_add_str(&b, v[i]);
		buf_addc(&b, ']');
	}
	return buf_str(&b);
}

static void test_shlex(void)
{
	CHECK_STR(lex("-C,--strip-path", false), "[-C][--strip-path]");
	CHECK_STR(lex("a,,b,", false), "[a][b]");
	CHECK_STR(lex("\"a,b\",c", false), "[\"a,b\"][c]");
	CHECK_STR(lex("'x y'z,w", false), "['x y'][z][w]");
	CHECK_STR(lex("a\"b,c\"", false), "[a\"b][c\"]");
	CHECK_STR(lex("\"abc", false), "ERR No closing quotation");
	CHECK_STR(lex(" a , b ", false), "[ a ][ b ]");
	CHECK_STR(lex("-C -e .x", true), "[-C][-e][.x]");
	CHECK_STR(lex("-s \"max_size: 3\" -e x", true), "[-s][max_size: 3][-e][x]");
	CHECK_STR(lex("a\\ b 'c d' \"e\\\"f\"", true), "[a b][c d][e\"f]");
	CHECK_STR(lex("\"\" x", true), "[][x]");
	CHECK_STR(lex("a\\", true), "ERR No escaped character");
	CHECK_STR(lex("x \"a\\\\b\" 'q\\'", true), "[x][a\\b][q\\]");
}

static const PyOption opts[] = {
	{"-h --help", false}, {"-e --extension", true}, {"-s", true},
	{"--strip-path", false}, {"--source-extension", true}, {"-C --c-style", false},
};

static const char *parse(int n, const char **argv)
{
	Str args[8];
	for (int i = 0; i < n; i++)
		args[i] = str_c(argv[i]);
	PyOptEvent *ev;
	Str *pos;
	int npos;
	char *err;
	int nev = py_optparse(opts, 6, args, n, &ev, &pos, &npos, &err);
	if (err)
		return xcat("ERR ", err);
	Buf b = {0};
	for (int i = 0; i < nev; i++) {
		buf_fmt(&b, "%d", ev[i].option);
		if (ev[i].value.p) {
			buf_addc(&b, '=');
			buf_add_str(&b, ev[i].value);
		}
		buf_addc(&b, ' ');
	}
	for (int i = 0; i < npos; i++) {
		buf_addc(&b, '+');
		buf_add_str(&b, pos[i]);
	}
	return buf_str(&b);
}

#define PARSE(...) parse((int)(sizeof((const char *[]){__VA_ARGS__}) / sizeof(char *)), (const char *[]){__VA_ARGS__})

static void test_optparse(void)
{
	CHECK_STR(PARSE("-e", ".x", "pos", "-Cs", "a:1", "--ext=.y"), "1=.x 5 2=a:1 1=.y +pos");
	CHECK_STR(PARSE("--s"), "ERR ambiguous option: --s (--source-extension, --strip-path?)");
	CHECK_STR(PARSE("--so=.c"), "4=.c ");
	CHECK_STR(PARSE("--strip-path=1"), "ERR --strip-path option does not take a value");
	CHECK_STR(PARSE("-e"), "ERR -e option requires 1 argument");
	CHECK_STR(PARSE("-Ce"), "ERR -e option requires 1 argument");
	CHECK_STR(PARSE("-z"), "ERR no such option: -z");
	CHECK_STR(PARSE("--", "-C"), "+-C");
	CHECK_STR(PARSE("-se"), "2=e ");
	CHECK_STR(PARSE("--=x"), "ERR ambiguous option: -- (--c-style, --extension, --help, --source-extension, --strip-path?)");
}

static const char *fmt(const char *f)
{
	Buf b = {0};
	char *msg;
	PyFormat e = py_percent_format(str_c(f), STR_LIT("pb.h"), &b, &msg);
	if (e == PY_FORMAT_TYPE_ERROR)
		return xcat("TypeError: ", msg);
	if (e == PY_FORMAT_VALUE_ERROR)
		return xcat("ValueError: ", msg);
	return buf_str(&b);
}

static void test_format(void)
{
	CHECK_STR(fmt("#include <%s>"), "#include <pb.h>");
	CHECK_STR(fmt("x"), "TypeError: not all arguments converted during string formatting");
	CHECK_STR(fmt("%s%s"), "TypeError: not enough arguments for format string");
	CHECK_STR(fmt("%d"), "TypeError: %d format: a real number is required, not str");
	CHECK_STR(fmt("%x"), "TypeError: %x format: an integer is required, not str");
	CHECK_STR(fmt("%f"), "TypeError: must be real number, not str");
	CHECK_STR(fmt("%c"), "TypeError: %c requires int or char");
	CHECK_STR(fmt("%(a"), "TypeError: format requires a mapping");
	CHECK_STR(fmt("%y"), "ValueError: unsupported format character 'y' (0x79) at index 1");
	CHECK_STR(fmt("%5"), "ValueError: incomplete format");
	CHECK_STR(fmt("%05s|%-6s|"), "TypeError: not enough arguments for format string");
	CHECK_STR(fmt("%-6s|"), "pb.h  |");
	CHECK_STR(fmt("%10.1s|"), "         p|");
	CHECK_STR(fmt("%5%"), "ValueError: unsupported format character '%' (0x25) at index 2");
	CHECK_STR(fmt("%%"), "TypeError: not all arguments converted during string formatting");
	CHECK_STR(fmt("%r"), "'pb.h'");
	CHECK_STR(fmt("%*s"), "TypeError: * wants int");
	CHECK_STR(fmt("%ls"), "pb.h");
	CHECK_STR(fmt("%\xc3\xa9"), "ValueError: unsupported format character '?' (0xe9) at index 1");
	CHECK_STR(fmt("%s%y"), "TypeError: not enough arguments for format string");
	CHECK_STR(fmt("%s%"), "ValueError: incomplete format");
}

TESTS(TEST(test_shlex), TEST(test_optparse), TEST(test_format))
