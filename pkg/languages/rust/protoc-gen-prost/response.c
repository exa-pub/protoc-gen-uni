/* Ports of protoc-gen-prost 0.5.0 (https://github.com/neoeinstein/protoc-gen-prost, Apache-2.0):
 * ModuleRequest::write_to_file and append_to_file (src/lib.rs), GeneratorResultExt
 * (src/generator.rs) and FileDescriptorSetGenerator (src/generator/file_descriptor_set.rs). */
#include "prost.h"

static ProstFile *file(const ProstModuleRequest *r, Str content, const char *insertion_point)
{
	char *name = prost_module_request_output_filepath(r);
	if (!name)
		return NULL;
	ProstFile *f = xalloc(sizeof *f);
	*f = (ProstFile){name, insertion_point, content};
	return f;
}

ProstFile *prost_write_to_file(const ProstModuleRequest *r, Str content) { return file(r, content, NULL); }

ProstFile *prost_append_to_file(const ProstModuleRequest *r, Str content) { return file(r, content, "module"); }

/* The bytes of a file as the buf.build remote plugin receives them. The server decodes the request
 * and encodes it again: buf's own SourceCodeInfo extension (buf_source_code_info_extension, field
 * 536000000), which a local buf writes before the locations as a known extension, is an unknown
 * field there and moves after them. Everything else is already encoded the same way (protobuf-go:
 * extensions first, then known fields by number, then unknown fields). */
static void remote_encoding(Str raw, Buf *out)
{
	PbReader r = {(const uint8_t *)raw.p, (const uint8_t *)raw.p + raw.n};
	const uint8_t *start = r.p;
	PbRawField f;
	while (pb_next_field(&r, &f)) {
		if (f.num != FDP_SOURCE_CODE_INFO || f.wt != WT_LEN) {
			buf_add(out, start, (size_t)(r.p - start));
			start = r.p;
			continue;
		}
		Buf locations = {0}, rest = {0};
		PbReader s = {f.val, f.val + f.vlen};
		const uint8_t *fs = s.p;
		PbRawField g;
		while (pb_next_field(&s, &g)) {
			buf_add(g.num == SCI_LOCATION ? &locations : &rest, fs, (size_t)(s.p - fs));
			fs = s.p;
		}
		buf_add(&rest, fs, (size_t)(s.end - fs)); /* malformed tail, if any */
		buf_add_str(&locations, buf_view(&rest));
		pb_put_bytes(out, FDP_SOURCE_CODE_INFO, buf_view(&locations));
		start = r.p;
	}
	buf_add(out, start, (size_t)(r.end - start));
}

/* append_file_descriptor_set_bytes: the module's files as a FileDescriptorSet (RawProtosSet, field
 * 1 per file), 16 bytes per line as {:#04x}. Upstream copies the files from the request; see
 * remote_encoding. */
static Str file_descriptor_set(const ProstModuleRequest *r)
{
	Buf set = {0};
	for (int i = 0; i < r->raw.n; i++) {
		Buf file = {0};
		remote_encoding(*VEC_AT(r->raw, Str, i), &file);
		pb_put_bytes(&set, 1, buf_view(&file));
	}
	Buf b = {0};
	buf_adds(&b, "/// Encoded file descriptor set for the `");
	buf_adds(&b, r->proto_package_name);
	buf_adds(&b, "` package\n");
	buf_adds(&b, "pub const FILE_DESCRIPTOR_SET: &[u8] = &[\n");
	const unsigned char *p = (const unsigned char *)set.p;
	size_t n = set.n, full = n / 16 * 16;
	for (size_t i = 0; i < full; i += 16) {
		buf_adds(&b, "    ");
		for (size_t j = 0; j < 16; j++)
			buf_fmt(&b, j < 15 ? "0x%02x, " : "0x%02x,\n", (unsigned)p[i + j]);
	}
	if (full < n) {
		buf_adds(&b, "    ");
		for (size_t i = full; i < n; i++)
			buf_fmt(&b, i + 1 < n ? "0x%02x, " : "0x%02x,\n", (unsigned)p[i]);
	}
	buf_adds(&b, "];\n");
	return buf_view(&b);
}

void prost_file_descriptor_set_generate(const ProstModuleRequestSet *set, Vec *files)
{
	for (int i = 0; i < set->requests.n; i++) {
		const ProstModuleRequest *r = set->requests.v[i];
		if (!r->output_filename)
			continue;
		vec_push(files, prost_append_to_file(r, file_descriptor_set(r)));
	}
}

enum { FEATURE_PROTO3_OPTIONAL = 1, CGRESP_FILE_INSERTION_POINT = 2 };

void prost_write_response(Buf *out, const Vec *files)
{
	pb_put_tag(out, CGRESP_SUPPORTED_FEATURES, WT_VARINT);
	pb_put_varint(out, FEATURE_PROTO3_OPTIONAL);
	for (int i = 0; i < files->n; i++) {
		const ProstFile *f = files->v[i];
		Buf b = {0};
		pb_put_bytes(&b, CGRESP_FILE_NAME, str_c(f->name));
		if (f->insertion_point)
			pb_put_bytes(&b, CGRESP_FILE_INSERTION_POINT, str_c(f->insertion_point));
		pb_put_bytes(&b, CGRESP_FILE_CONTENT, f->content);
		pb_put_bytes(out, CGRESP_FILE, buf_view(&b));
	}
}

void prost_write_error(Buf *out, Str error)
{
	pb_put_bytes(out, CGRESP_ERROR, error);
	pb_put_tag(out, CGRESP_SUPPORTED_FEATURES, WT_VARINT);
	pb_put_varint(out, FEATURE_PROTO3_OPTIONAL);
}
