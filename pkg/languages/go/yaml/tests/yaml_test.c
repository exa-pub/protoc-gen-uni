/* Unit test of the go.yaml.in/yaml/v3 port: every case of testdata/cases.txt ("<mode> <hex>")
 * is run like the reference program that produced testdata/expected.txt (go.yaml.in/yaml/v3
 * v3.0.4, see testdata/README.md):
 *   s: a string scalar as a mapping value, a key and a sequence item;
 *   y: YAML unmarshalled into a Node (null on error, as gnostic-models Any.ToRawInfo), as a
 *      mapping value and a sequence item;
 *   f: a float scalar formatted with %g.
 * The document has a head comment and is marshalled; the output line is the hex of the
 * document or "E" + the hex of the error.
 *
 *   yaml_test [<cases> <expected>]   compare (default: the testdata of the source tree)
 *   yaml_test --filter               read cases on stdin, write output lines */
#include "go_std.h"
#include "yaml.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static Str unhex(const char *h, size_t n)
{
	char *p = xalloc(n / 2 + 1);
	for (size_t i = 0; i + 1 < n; i += 2) {
		unsigned v;
		char t[3] = {h[i], h[i + 1], 0};
		sscanf(t, "%2x", &v);
		p[i / 2] = (char)v;
	}
	return (Str){p, n / 2};
}

static void tohex(Buf *b, Str s)
{
	static const char x[] = "0123456789abcdef";
	for (size_t i = 0; i < s.n; i++) {
		buf_addc(b, x[(uint8_t)s.p[i] >> 4]);
		buf_addc(b, x[(uint8_t)s.p[i] & 15]);
	}
}

static YamlNode *str(Str s)
{
	YamlNode *n = yaml_node_new(YAML_SCALAR_NODE);
	n->tag = STR_LIT("!!str");
	n->value = s;
	return n;
}

static YamlNode *any_node(Str s)
{
	YamlNode *node = yaml_node_new(0);
	const char *err = yaml_unmarshal(s, node);
	if (!err) {
		if (node->kind == YAML_DOCUMENT_NODE)
			return node->content[0];
		return node;
	}
	YamlNode *n = yaml_node_new(YAML_SCALAR_NODE);
	n->tag = STR_LIT("!!null");
	return n;
}

static void run_case(char mode, Str in, Buf *out)
{
	YamlNode *m = yaml_node_new(YAML_MAPPING_NODE);
	m->content_set = true;
	if (mode == 's') {
		yaml_node_append(m, str(STR_LIT("key")));
		yaml_node_append(m, str(in));
		yaml_node_append(m, str(in));
		yaml_node_append(m, str(STR_LIT("v")));
		YamlNode *seq = yaml_node_new(YAML_SEQUENCE_NODE);
		yaml_node_append(seq, str(in));
		yaml_node_append(m, str(STR_LIT("seq")));
		yaml_node_append(m, seq);
	} else if (mode == 'y') {
		yaml_node_append(m, str(STR_LIT("example")));
		yaml_node_append(m, any_node(in));
		YamlNode *seq = yaml_node_new(YAML_SEQUENCE_NODE);
		yaml_node_append(seq, any_node(in));
		yaml_node_append(m, str(STR_LIT("enum")));
		yaml_node_append(m, seq);
	} else {
		char *t = xstrndup(in.p, in.n);
		YamlNode *f = yaml_node_new(YAML_SCALAR_NODE);
		f->tag = STR_LIT("!!float");
		f->value = str_c(go_std_format_float(strtod(t, NULL), 'g', -1, 64));
		yaml_node_append(m, str(STR_LIT("maximum")));
		yaml_node_append(m, f);
	}
	YamlNode *doc = yaml_node_new(YAML_DOCUMENT_NODE);
	yaml_node_append(doc, m);
	doc->head_comment = STR_LIT("Generated with protoc-gen-openapi\nhttps://example.com");
	Buf b = {0};
	const char *err = yaml_marshal(doc, &b);
	if (err) {
		buf_addc(out, 'E');
		tohex(out, str_c(err));
	} else {
		tohex(out, buf_view(&b));
	}
}

static char *read_line(FILE *f)
{
	Buf b = {0};
	int c;
	while ((c = fgetc(f)) != EOF && c != '\n')
		buf_addc(&b, (char)c);
	if (c == EOF && b.n == 0)
		return NULL;
	return (char *)buf_str(&b);
}

int main(int argc, char **argv)
{
	if (argc == 2 && strcmp(argv[1], "--filter") == 0) {
		char *line;
		while ((line = read_line(stdin))) {
			Buf out = {0};
			run_case(line[0], unhex(line + 2, strlen(line + 2)), &out);
			printf("%s\n", buf_str(&out));
		}
		return 0;
	}
	const char *cases = argc > 2 ? argv[1] : "pkg/languages/go/yaml/tests/testdata/cases.txt";
	const char *expected = argc > 2 ? argv[2] : "pkg/languages/go/yaml/tests/testdata/expected.txt";
	FILE *fc = fopen(cases, "r"), *fe = fopen(expected, "r");
	if (!fc || !fe) {
		fprintf(stderr, "cannot open %s or %s\n", cases, expected);
		return 1;
	}
	int n = 0, bad = 0;
	char *line;
	while ((line = read_line(fc))) {
		char *want = read_line(fe);
		Buf out = {0};
		run_case(line[0], unhex(line + 2, strlen(line + 2)), &out);
		n++;
		if (!want || strcmp(buf_str(&out), want) != 0) {
			if (bad++ < 10)
				fprintf(stderr, "case %d (%s): got %s\nwant %s\n", n, line, buf_str(&out), want ? want : "(none)");
		}
	}
	printf("%d cases, %d failed\n", n, bad);
	return bad != 0;
}
