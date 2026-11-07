/* Corpus test of the Go formatter port: every case <hash>.in.go / .imports / .out.go / .idents in
 * $UNI_GOFMT_CORPUS (recorded from the Go implementation, see testdata/make-corpus.sh) must be
 * formatted byte for byte like Go, and the declaration identifiers must match. Exit code 77
 * (skipped) if the corpus is absent.
 *
 *	go1_26_5_corpus [-v] [-k] [case-hash...]   -k: keep going, -v: print a diff excerpt
 *	go1_26_5_corpus --mutate N [seed]          robustness: N mutated inputs per case
 *	                                           (truncation, byte flips) must not crash or hang */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "go1_26_5.h"

static char *read_file(const char *path, size_t *n)
{
	FILE *f = fopen(path, "rb");
	if (!f)
		return NULL;
	Buf b = {0};
	char tmp[65536];
	size_t r;
	while ((r = fread(tmp, 1, sizeof tmp, f)) > 0)
		buf_add(&b, tmp, r);
	fclose(f);
	*n = b.n;
	return b.p ? b.p : xstrdup("");
}

static go1_26_5_import *parse_imports(char *s, size_t *n)
{
	size_t cap = 16, k = 0;
	go1_26_5_import *v = xalloc(cap * sizeof *v);
	for (char *line = s; *line;) {
		char *nl = strchr(line, '\n');
		if (nl)
			*nl = 0;
		char *tab = strchr(line, '\t');
		if (tab) {
			*tab = 0;
			if (k == cap) {
				v = xrealloc(v, cap * sizeof *v, 2 * cap * sizeof *v);
				cap *= 2;
			}
			v[k++] = (go1_26_5_import){line, tab + 1};
		}
		if (!nl)
			break;
		line = nl + 1;
	}
	*n = k;
	return v;
}

static void show_diff(const char *got, size_t gn, const char *want, size_t wn)
{
	size_t i = 0, line = 1, bol = 0;
	while (i < gn && i < wn && got[i] == want[i]) {
		if (got[i] == '\n')
			line++, bol = i + 1;
		i++;
	}
	fprintf(stderr, "  first difference at line %zu (offset %zu)\n", line, i);
	size_t ge = bol, we = bol;
	for (int k = 0; k < 3 && ge < gn; ge++)
		if (got[ge] == '\n')
			k++;
	for (int k = 0; k < 3 && we < wn; we++)
		if (want[we] == '\n')
			k++;
	fprintf(stderr, "  got:\n%.*s\n  want:\n%.*s\n", (int)(ge - bol), got + bol, (int)(we - bol), want + bol);
}

typedef struct {
	const char *src;
	size_t n;
	const go1_26_5_import *imports;
	size_t nimports;
	Buf out;
	const char *err;
} format_job;

static void run_format(void *arg)
{
	format_job *j = arg;
	j->err = go1_26_5_format_content(j->src, j->n, j->imports, j->nimports, &j->out);
}

static int mutate(const char *dir, char **names, int nnames, int count, unsigned seed)
{
	srand(seed);
	long runs = 0, errors = 0, fails = 0;
	for (int i = 0; i < nnames; i++) {
		size_t n, ni;
		char *src = read_file(xfmt("%s/%s.in.go", dir, names[i]), &n);
		char *imps = read_file(xfmt("%s/%s.imports", dir, names[i]), &ni);
		go1_26_5_import *imports = parse_imports(imps, &ni);
		for (int k = 0; k < count; k++) {
			char *m = xalloc(n + 1);
			memcpy(m, src, n);
			size_t mn = n;
			switch (rand() % 3) {
			case 0: /* truncate */
				mn = n ? (size_t)rand() % n : 0;
				break;
			case 1: /* random byte flips */
				for (int f = 1 + rand() % 8; f > 0 && n; f--)
					m[(size_t)rand() % n] = (char)(rand() % 256);
				break;
			default: { /* delete or duplicate a range */
				if (n < 2)
					break;
				size_t a = (size_t)rand() % n, len = 1 + (size_t)rand() % 64;
				if (a + len > n)
					len = n - a;
				if (rand() % 2) {
					memmove(m + a, m + a + len, n - a - len);
					mn = n - len;
				} else {
					char *d = xalloc(n + len + 1);
					memcpy(d, m, a + len);
					memcpy(d + a + len, m + a, n - a);
					m = d;
					mn = n + len;
				}
			}
			}
			format_job j = {m, mn, imports, ni, {0}, NULL};
			const char *fail_msg = try_run(run_format, &j);
			runs++;
			if (fail_msg)
				fails++;
			else if (j.err)
				errors++;
		}
	}
	printf("mutate: %ld runs, %ld parse errors, %ld internal failures (fail())\n", runs, errors, fails);
	return 0;
}

int main(int argc, char **argv)
{
	const char *dir = getenv("UNI_GOFMT_CORPUS");
	if (!dir || !*dir) {
		fprintf(stderr, "UNI_GOFMT_CORPUS not set: skipped\n");
		return 77;
	}
	size_t nindex;
	char *index = read_file(xfmt("%s/index.txt", dir), &nindex);
	if (!index) {
		fprintf(stderr, "%s: no corpus (pkg/languages/go/stdlib/go1.26.5/testdata/make-corpus.sh): skipped\n", dir);
		return 77;
	}
	bool verbose = false, keep_going = false;
	int mutate_count = 0;
	unsigned seed = 1;
	Vec only = {0};
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-v") == 0)
			verbose = true;
		else if (strcmp(argv[i], "-k") == 0)
			keep_going = true;
		else if (strcmp(argv[i], "--mutate") == 0 && i + 1 < argc) {
			mutate_count = atoi(argv[++i]);
			if (i + 1 < argc && argv[i + 1][0] != '-')
				seed = (unsigned)strtoul(argv[++i], NULL, 10);
		} else
			vec_push(&only, argv[i]);
	}
	int cap = 256, nnames = 0;
	char **names = xalloc(sizeof *names * (size_t)cap);
	for (char *line = index; *line;) {
		char *nl = strchr(line, '\n');
		if (nl)
			*nl = 0;
		if (*line) {
			if (nnames == cap) {
				names = xrealloc(names, sizeof *names * (size_t)cap, sizeof *names * (size_t)cap * 2);
				cap *= 2;
			}
			names[nnames++] = line;
		}
		if (!nl)
			break;
		line = nl + 1;
	}
	if (nnames == 0) {
		fprintf(stderr, "%s: empty corpus: skipped\n", dir);
		return 77;
	}
	if (mutate_count > 0)
		return mutate(dir, names, nnames, mutate_count, seed);

	int ok = 0, total = 0, failed = 0;
	for (int i = 0; i < nnames; i++) {
		if (only.n) {
			bool want = false;
			for (int k = 0; k < only.n; k++)
				want |= strcmp(only.v[k], names[i]) == 0;
			if (!want)
				continue;
		}
		total++;
		size_t n, ni, no, nid;
		char *src = read_file(xfmt("%s/%s.in.go", dir, names[i]), &n);
		char *imps = read_file(xfmt("%s/%s.imports", dir, names[i]), &ni);
		char *want = read_file(xfmt("%s/%s.out.go", dir, names[i]), &no);
		char *want_idents = read_file(xfmt("%s/%s.idents", dir, names[i]), &nid);
		if (!src || !imps || !want || !want_idents) {
			fprintf(stderr, "%s: incomplete case\n", names[i]);
			failed++;
			continue;
		}
		go1_26_5_import *imports = parse_imports(imps, &ni);
		format_job j = {src, n, imports, ni, {0}, NULL};
		const char *fail_msg = try_run(run_format, &j);
		if (fail_msg || j.err) {
			fprintf(stderr, "FAIL %s: %s\n", names[i], fail_msg ? fail_msg : j.err);
			failed++;
		} else if (j.out.n != no || memcmp(j.out.p, want, no) != 0) {
			fprintf(stderr, "FAIL %s: output differs\n", names[i]);
			if (verbose)
				show_diff(j.out.p ? j.out.p : "", j.out.n, want, no);
			failed++;
		} else {
			go1_26_5_ident *ids;
			size_t nids;
			const char *err = go1_26_5_decl_idents(want, no, &ids, &nids);
			Buf got = {0};
			for (size_t k = 0; !err && k < nids; k++)
				buf_fmt(&got, "%s\t%d\t%d\n", ids[k].symbol, ids[k].begin, ids[k].end);
			if (err || got.n != nid || memcmp(got.p ? got.p : "", want_idents, nid) != 0) {
				fprintf(stderr, "FAIL %s: identifiers differ%s%s\n", names[i], err ? ": " : "", err ? err : "");
				if (verbose && !err)
					show_diff(got.p ? got.p : "", got.n, want_idents, nid);
				failed++;
			} else
				ok++;
		}
		if (failed && !keep_going)
			break;
	}
	printf("corpus: %d/%d identical (%s)\n", ok, total, dir);
	return failed ? 1 : 0;
}
