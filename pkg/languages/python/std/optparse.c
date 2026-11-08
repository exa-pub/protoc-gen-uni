/* optparse (Lib/optparse.py of CPython 3.13): OptionParser.parse_args for options that take no
 * value or exactly one value (nargs 1), with interspersed positional arguments, long option
 * abbreviations (_match_abbrev) and the error texts of OptionParser.error. */
#include "python.h"

#include <stdlib.h>
#include <string.h>

/* The next name of a space-separated list: its start and length, or NULL at the end. */
static const char *next_name(const char **p, size_t *n)
{
	while (**p == ' ')
		(*p)++;
	if (!**p)
		return NULL;
	const char *start = *p;
	while (**p && **p != ' ')
		(*p)++;
	*n = (size_t)(*p - start);
	return start;
}

static int cmp_cstr(const void *a, const void *b)
{
	return py_str_cmp(str_c(*(const char *const *)a), str_c(*(const char *const *)b));
}

/* _match_long_opt: the index of the option and its full name, or an error. */
static int match_long(const PyOption *opts, int nopts, Str s, const char **name, char **err)
{
	Vec pv = {0};
	int found = -1;
	for (int i = 0; i < nopts; i++) {
		const char *p = opts[i].names, *w;
		size_t n;
		while ((w = next_name(&p, &n)))
			if (n > 2 && w[1] == '-' && n >= s.n && !memcmp(w, s.p, s.n)) {
				char *full = xstrndup(w, n);
				if (n == s.n) {
					*name = full;
					return i;
				}
				vec_push(&pv, full);
				found = i;
			}
	}
	const char **poss = (const char **)pv.v;
	int np = pv.n;
	if (np == 1) {
		*name = poss[0];
		return found;
	}
	Buf b = {0};
	if (!np) {
		buf_adds(&b, "no such option: ");
		buf_add_str(&b, s);
	} else {
		qsort(poss, (size_t)np, sizeof *poss, cmp_cstr);
		buf_adds(&b, "ambiguous option: ");
		buf_add_str(&b, s);
		buf_adds(&b, " (");
		for (int i = 0; i < np; i++)
			buf_fmt(&b, "%s%s", i ? ", " : "", poss[i]);
		buf_adds(&b, "?)");
	}
	*err = (char *)buf_str(&b);
	return -1;
}

/* The option with this short name ("-x"), or -1. */
static int find_short(const PyOption *opts, int nopts, const char *opt)
{
	for (int i = 0; i < nopts; i++) {
		const char *p = opts[i].names, *w;
		size_t n;
		while ((w = next_name(&p, &n)))
			if (n == 2 && w[1] != '-' && !memcmp(w, opt, 2) && !opt[2])
				return i;
	}
	return -1;
}

/* len() of a str */
static size_t py_len(Str s)
{
	size_t n = 0;
	for (size_t i = 0; i < s.n; i++)
		n += ((unsigned char)s.p[i] & 0xc0) != 0x80;
	return n;
}

static char *requires_arg(const char *opt)
{
	return xcat(opt, " option requires 1 argument");
}

int py_optparse(const PyOption *opts, int nopts, const Str *args, int nargs, PyOptEvent **events,
	Str **positional, int *npositional, char **err)
{
	/* rargs: a stack of pending arguments (an inserted value goes in front) */
	Str *rargs = xalloc(sizeof *rargs * (size_t)(nargs + 1));
	int rn = 0;
	for (int i = nargs - 1; i >= 0; i--)
		rargs[rn++] = args[i]; /* rargs[rn-1] is rargs[0] in Python */
	/* at most one event per byte of the arguments */
	size_t total = 1;
	for (int i = 0; i < nargs; i++)
		total += args[i].n;
	PyOptEvent *ev = xalloc(sizeof *ev * total);
	int nev = 0;
	*events = ev;
	Str *pos = xalloc(sizeof *pos * (size_t)(nargs + 1));
	int npos = 0;
	*err = NULL;
	while (rn) {
		Str arg = rargs[rn - 1];
		if (arg.n == 2 && !memcmp(arg.p, "--", 2)) {
			rn--;
			break;
		} else if (arg.n >= 2 && !memcmp(arg.p, "--", 2)) {
			rn--;
			Str opt = arg;
			bool explicit_value = false;
			const char *eq = memchr(arg.p, '=', arg.n);
			if (eq) {
				opt = (Str){arg.p, (size_t)(eq - arg.p)};
				/* the list has room: one argument was just popped */
				rargs[rn++] = (Str){eq + 1, arg.n - (size_t)(eq + 1 - arg.p)};
				explicit_value = true;
			}
			const char *name = NULL;
			int o = match_long(opts, nopts, opt, &name, err);
			if (o < 0)
				return nev;
			Str value = {0};
			if (opts[o].takes_value) {
				if (rn < 1) {
					*err = requires_arg(name);
					return nev;
				}
				value = rargs[--rn];
			} else if (explicit_value) {
				*err = xcat(name, " option does not take a value");
				return nev;
			}
			ev[nev++] = (PyOptEvent){o, value};
		} else if (arg.n >= 1 && arg.p[0] == '-' && py_len(arg) > 1) {
			rn--;
			size_t i = 1;
			while (i < arg.n) {
				uint32_t r;
				int w = utf8_decode(arg.p + i, arg.n - i, &r);
				char *opt = xcat("-", xstrndup(arg.p + i, (size_t)w));
				i += (size_t)w;
				int o = find_short(opts, nopts, opt);
				if (o < 0) {
					*err = xcat("no such option: ", opt);
					return nev;
				}
				Str value = {0};
				bool stop = false;
				if (opts[o].takes_value) {
					if (i < arg.n) {
						rargs[rn++] = (Str){arg.p + i, arg.n - i};
						stop = true;
					}
					if (rn < 1) {
						*err = requires_arg(opt);
						return nev;
					}
					value = rargs[--rn];
				}
				ev[nev++] = (PyOptEvent){o, value};
				if (stop)
					break;
			}
		} else {
			pos[npos++] = arg;
			rn--;
		}
	}
	while (rn)
		pos[npos++] = rargs[--rn];
	*positional = pos;
	*npositional = npos;
	return nev;
}
