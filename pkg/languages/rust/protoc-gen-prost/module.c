/* Port of src/module.rs of prost-build 0.14.1 (https://github.com/tokio-rs/prost, Apache-2.0). */
#include "prost.h"

#include <string.h>

ProstModule *prost_module_from_package(const char *package)
{
	Vec parts = str_split(package, '.');
	ProstModule *m = xalloc(sizeof *m);
	m->parts = xalloc(sizeof *m->parts * (size_t)(parts.n ? parts.n : 1));
	for (int i = 0; i < parts.n; i++) {
		const char *p = parts.v[i];
		if (*p)
			m->parts[m->n++] = prost_to_snake(p);
	}
	return m;
}

int prost_module_cmp(const ProstModule *a, const ProstModule *b)
{
	for (int i = 0; i < a->n && i < b->n; i++) {
		int c = strcmp(a->parts[i], b->parts[i]);
		if (c)
			return c < 0 ? -1 : 1;
	}
	return a->n < b->n ? -1 : a->n > b->n ? 1 : 0;
}

static char *join(const ProstModule *m, const char *sep)
{
	Buf b = {0};
	for (int i = 0; i < m->n; i++) {
		if (i)
			buf_adds(&b, sep);
		buf_adds(&b, m->parts[i]);
	}
	return xstrdup(buf_str(&b));
}

char *prost_module_to_file_name_or(const ProstModule *m, const char *def)
{
	return xcat(m->n ? join(m, ".") : def, ".rs");
}

char *prost_module_display(const ProstModule *m) { return join(m, "::"); }
