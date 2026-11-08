/* Port of ModuleRequestSet and ModuleRequest of src/lib.rs of protoc-gen-prost 0.5.0
 * (https://github.com/neoeinstein/protoc-gen-prost, Apache-2.0). */
#include "prost.h"

#include <stdlib.h>
#include <string.h>

/* RawProtos: the bytes of every CodeGeneratorRequest.proto_file as in the request. */
static Vec raw_protos(Str raw)
{
	Vec v = {0};
	PbReader r = {(const uint8_t *)raw.p, (const uint8_t *)raw.p + raw.n};
	PbRawField f;
	while (pb_next_field(&r, &f)) {
		if (f.num != CGR_PROTO_FILE || f.wt != WT_LEN)
			continue;
		Str *s = xalloc(sizeof *s);
		*s = (Str){(const char *)f.val, f.vlen};
		vec_push(&v, s);
	}
	return v;
}

/* The index of the request of m in set, or -(insertion point) - 1. */
static int find(const ProstModuleRequestSet *set, const ProstModule *m)
{
	int lo = 0, hi = set->requests.n;
	while (lo < hi) {
		int mid = lo + (hi - lo) / 2;
		int c = prost_module_cmp(VEC_AT(set->requests, ProstModuleRequest, mid)->module, m);
		if (!c)
			return mid;
		if (c < 0)
			lo = mid + 1;
		else
			hi = mid;
	}
	return -lo - 1;
}

/* A proto file and its module, ordered by module, then by position in the request. */
typedef struct {
	ProstModule *module;
	int i;
} FileModule;

static int cmp_file_module(const void *a, const void *b)
{
	const FileModule *x = a, *y = b;
	int c = prost_module_cmp(x->module, y->module);
	return c ? c : (x->i > y->i) - (x->i < y->i);
}

/* Upstream folds the files into a BTreeMap in request order; sorting the files by (module,
 * position) and grouping them builds the same map in O(n log n). */
ProstModuleRequestSet *prost_module_request_set_new(PbMsg *req, Str raw, const char *default_package_filename,
						    bool flat_output_dir)
{
	if (!default_package_filename)
		default_package_filename = "_";
	Map input = {0};
	for (int i = 0; i < pb_count(req, CGR_FILE_TO_GENERATE); i++)
		map_put(&input, pb_str_at(req, CGR_FILE_TO_GENERATE, i), (void *)1);
	Vec raws = raw_protos(raw);
	ProstModuleRequestSet *set = xalloc(sizeof *set);
	int n = pb_count(req, CGR_PROTO_FILE);
	if (n > raws.n) /* zip */
		n = raws.n;
	FileModule *order = xalloc(sizeof *order * (size_t)(n ? n : 1));
	for (int i = 0; i < n; i++) {
		const char *package = pb_str(pb_msg_at(req, CGR_PROTO_FILE, i), FDP_PACKAGE);
		order[i] = (FileModule){prost_module_from_package(package), i};
	}
	qsort(order, (size_t)n, sizeof *order, cmp_file_module);
	ProstModuleRequest *entry = NULL;
	for (int k = 0; k < n; k++) {
		int i = order[k].i;
		PbMsg *proto = pb_msg_at(req, CGR_PROTO_FILE, i);
		const char *package = pb_str(proto, FDP_PACKAGE);
		if (!entry || prost_module_cmp(entry->module, order[k].module)) {
			entry = xalloc(sizeof *entry);
			entry->proto_package_name = package;
			entry->module = order[k].module;
			entry->flat_output_dir = flat_output_dir;
			vec_push(&set->requests, entry);
		}
		if (!entry->output_filename && map_has(&input, pb_str(proto, FDP_NAME)))
			entry->output_filename = *package ? xcat(package, ".rs") : default_package_filename;
		vec_push(&entry->files, proto);
		vec_push(&entry->raw, raws.v[i]);
	}
	return set;
}

ProstModuleRequest *prost_module_request_set_for_module(const ProstModuleRequestSet *set, const ProstModule *m)
{
	int at = find(set, m);
	return at >= 0 ? set->requests.v[at] : NULL;
}

char *prost_module_request_output_dir(const ProstModuleRequest *r)
{
	if (r->flat_output_dir || !r->module->n)
		return "";
	Buf b = {0};
	for (int i = 0; i < r->module->n; i++) {
		buf_adds(&b, r->module->parts[i]);
		buf_addc(&b, '/');
	}
	return xstrdup(buf_str(&b));
}

char *prost_module_request_output_filepath(const ProstModuleRequest *r)
{
	return r->output_filename ? xcat(prost_module_request_output_dir(r), r->output_filename) : NULL;
}
