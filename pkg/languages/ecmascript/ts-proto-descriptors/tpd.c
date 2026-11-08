/* ts-proto-descriptors 2.1.0: see tpd.h. */
#include "tpd.h"

#include <stdlib.h>
#include <string.h>

#define NEW(T) ((T *)xalloc(sizeof(T)))

static const char *s(const PbMsg *m, uint32_t num) { return pb_str(m, num); }
static int32_t i32(const PbMsg *m, uint32_t num, int32_t def) { return (int32_t)pb_int(m, num, def); }
static bool b(const PbMsg *m, uint32_t num) { return pb_int(m, num, 0) != 0; }

static TpdOptions *options(const PbMsg *m, uint32_t num, uint32_t deprecated_num)
{
	if (!pb_has(m, num))
		return NULL;
	TpdOptions *o = NEW(TpdOptions);
	o->deprecated = b(pb_msg(m, num), deprecated_num);
	return o;
}

typedef struct {
	uint32_t tag;
	int index;
	Str value;
} RawUnknown;

static int raw_unknown_cmp(const void *a, const void *b)
{
	const RawUnknown *x = a, *y = b;
	if (x->tag != y->tag)
		return x->tag < y->tag ? -1 : 1;
	return x->index - y->index;
}

/* The `_unknownFields` the decoder of a ts-proto message with unknownFields builds: `reader.skip`
 * returns the bytes of a value after its tag. known_varint (optional) receives the varint values
 * of field `known` (a closed enum value pkg/pb kept as unknown), which ts-proto reads as a field. */
static TpdUnknownField *unknown_fields(const PbMsg *m, int *n, uint32_t known, int32_t *known_varint)
{
	*n = 0;
	PbReader r = pb_unknown_fields(m);
	int cap = 0, nraw = 0;
	RawUnknown *raw = NULL;
	while (r.p && r.p < r.end) {
		uint32_t num;
		int wt;
		if (!pb_read_tag(&r, &num, &wt))
			break;
		const uint8_t *start = r.p;
		const uint8_t *val;
		size_t vlen;
		uint64_t v = 0;
		if (wt == WT_VARINT ? !pb_read_varint(&r, &v) : !pb_skip_value(&r, num, wt, &val, &vlen, 0))
			break;
		if (known_varint && num == known && wt == WT_VARINT) {
			*known_varint = (int32_t)v;
			continue;
		}
		if (nraw == cap) {
			int ncap = cap ? cap * 2 : 8;
			raw = xrealloc(raw, sizeof *raw * (size_t)cap, sizeof *raw * (size_t)ncap);
			cap = ncap;
		}
		raw[nraw] = (RawUnknown){num << 3 | (uint32_t)wt, nraw, {(const char *)start, (size_t)(r.p - start)}};
		nraw++;
	}
	if (!nraw)
		return NULL;
	qsort(raw, (size_t)nraw, sizeof *raw, raw_unknown_cmp);
	TpdUnknownField *u = xalloc(sizeof *u * (size_t)nraw);
	for (int i = 0, j; i < nraw; i = j) {
		for (j = i + 1; j < nraw && raw[j].tag == raw[i].tag; j++)
			;
		TpdUnknownField *f = &u[(*n)++];
		f->tag = raw[i].tag;
		f->nvalue = j - i;
		f->value = xalloc(sizeof(Str) * (size_t)(j - i));
		for (int k = i; k < j; k++)
			f->value[k - i] = raw[k].value;
	}
	return u;
}

static TpdField *field(const PbMsg *m)
{
	TpdField *f = NEW(TpdField);
	f->name = s(m, FD_NAME);
	f->number = i32(m, FD_NUMBER, 0);
	f->label = i32(m, FD_LABEL, 1);
	f->type = i32(m, FD_TYPE, 1);
	f->type_name = s(m, FD_TYPE_NAME);
	f->extendee = s(m, FD_EXTENDEE);
	f->default_value = s(m, FD_DEFAULT_VALUE);
	f->json_name = s(m, FD_JSON_NAME);
	f->oneof_index = i32(m, FD_ONEOF_INDEX, 0);
	f->has_oneof_index = pb_has(m, FD_ONEOF_INDEX);
	f->has_label = pb_has(m, FD_LABEL);
	f->has_type = pb_has(m, FD_TYPE);
	f->has_type_name = pb_has(m, FD_TYPE_NAME);
	f->has_json_name = pb_has(m, FD_JSON_NAME);
	f->proto3_optional = b(m, FD_PROTO3_OPTIONAL);
	if (pb_has(m, FD_OPTIONS)) {
		const PbMsg *o = pb_msg(m, FD_OPTIONS);
		f->options = NEW(TpdFieldOptions);
		f->options->packed = b(o, FO_PACKED);
		f->options->deprecated = b(o, FO_DEPRECATED);
		f->options->jstype = i32(o, FO_JSTYPE, 0);
	}
	return f;
}

static TpdField **fields(const PbMsg *m, uint32_t num, int *n)
{
	*n = pb_count(m, num);
	TpdField **v = xalloc(sizeof(TpdField *) * (size_t)(*n + 1));
	for (int i = 0; i < *n; i++)
		v[i] = field(pb_msg_at(m, num, i));
	return v;
}

static TpdEnum *enum_(const PbMsg *m)
{
	TpdEnum *e = NEW(TpdEnum);
	e->name = s(m, EDP_NAME);
	e->nvalue = pb_count(m, EDP_VALUE);
	e->value = xalloc(sizeof(TpdEnumValue *) * (size_t)(e->nvalue + 1));
	for (int i = 0; i < e->nvalue; i++) {
		const PbMsg *v = pb_msg_at(m, EDP_VALUE, i);
		TpdEnumValue *ev = NEW(TpdEnumValue);
		ev->name = s(v, EVDP_NAME);
		ev->number = i32(v, EVDP_NUMBER, 0);
		ev->options = options(v, EVDP_OPTIONS, 1);
		e->value[i] = ev;
	}
	e->options = options(m, EDP_OPTIONS, 3);
	return e;
}

static TpdEnum **enums(const PbMsg *m, uint32_t num, int *n)
{
	*n = pb_count(m, num);
	TpdEnum **v = xalloc(sizeof(TpdEnum *) * (size_t)(*n + 1));
	for (int i = 0; i < *n; i++)
		v[i] = enum_(pb_msg_at(m, num, i));
	return v;
}

static TpdMessage *message(const PbMsg *m, int depth)
{
	TpdMessage *d = NEW(TpdMessage);
	d->name = s(m, DP_NAME);
	d->field = fields(m, DP_FIELD, &d->nfield);
	d->nnested_type = pb_count(m, DP_NESTED_TYPE);
	d->nested_type = xalloc(sizeof(TpdMessage *) * (size_t)(d->nnested_type + 1));
	for (int i = 0; i < d->nnested_type; i++)
		d->nested_type[i] = message(pb_msg_at(m, DP_NESTED_TYPE, i), depth + 1);
	d->enum_type = enums(m, DP_ENUM_TYPE, &d->nenum_type);
	d->nextension_range = pb_count(m, DP_EXTENSION_RANGE);
	d->extension = fields(m, DP_EXTENSION, &d->nextension);
	d->noneof_decl = pb_count(m, DP_ONEOF_DECL);
	d->oneof_decl = xalloc(sizeof(TpdOneof *) * (size_t)(d->noneof_decl + 1));
	for (int i = 0; i < d->noneof_decl; i++) {
		d->oneof_decl[i] = NEW(TpdOneof);
		d->oneof_decl[i]->name = s(pb_msg_at(m, DP_ONEOF_DECL, i), ODP_NAME);
	}
	if (pb_has(m, DP_OPTIONS)) {
		const PbMsg *o = pb_msg(m, DP_OPTIONS);
		d->options = NEW(TpdMessageOptions);
		d->options->deprecated = b(o, 3);
		d->options->map_entry = b(o, 7);
	}
	return d;
}

static TpdFile *file(PbMsg *m)
{
	TpdFile *f = NEW(TpdFile);
	f->raw = m;
	f->name = s(m, FDP_NAME);
	f->package = s(m, FDP_PACKAGE);
	f->syntax = s(m, FDP_SYNTAX);
	f->edition = i32(m, FDP_EDITION, 0);
	f->ndependency = pb_count(m, FDP_DEPENDENCY);
	f->dependency = xalloc(sizeof(char *) * (size_t)(f->ndependency + 1));
	for (int i = 0; i < f->ndependency; i++)
		f->dependency[i] = pb_str_at(m, FDP_DEPENDENCY, i);
	f->nmessage_type = pb_count(m, FDP_MESSAGE_TYPE);
	f->message_type = xalloc(sizeof(TpdMessage *) * (size_t)(f->nmessage_type + 1));
	for (int i = 0; i < f->nmessage_type; i++)
		f->message_type[i] = message(pb_msg_at(m, FDP_MESSAGE_TYPE, i), 1);
	f->enum_type = enums(m, FDP_ENUM_TYPE, &f->nenum_type);
	f->nservice = pb_count(m, FDP_SERVICE);
	f->service = xalloc(sizeof(TpdService *) * (size_t)(f->nservice + 1));
	for (int i = 0; i < f->nservice; i++) {
		const PbMsg *sm = pb_msg_at(m, FDP_SERVICE, i);
		TpdService *sv = NEW(TpdService);
		sv->name = s(sm, SDP_NAME);
		sv->nmethod = pb_count(sm, SDP_METHOD);
		sv->method = xalloc(sizeof(TpdMethod *) * (size_t)(sv->nmethod + 1));
		for (int k = 0; k < sv->nmethod; k++) {
			const PbMsg *mm = pb_msg_at(sm, SDP_METHOD, k);
			TpdMethod *md = NEW(TpdMethod);
			md->name = s(mm, MDP_NAME);
			md->input_type = s(mm, MDP_INPUT_TYPE);
			md->output_type = s(mm, MDP_OUTPUT_TYPE);
			md->client_streaming = b(mm, MDP_CLIENT_STREAMING);
			md->server_streaming = b(mm, MDP_SERVER_STREAMING);
			if (pb_has(mm, MDP_OPTIONS)) {
				const PbMsg *o = pb_msg(mm, MDP_OPTIONS);
				md->options = NEW(TpdMethodOptions);
				md->options->deprecated = b(o, 33);
				md->options->idempotency_level = i32(o, 34, 0);
				md->options->unknown = unknown_fields(o, &md->options->nunknown, 34, &md->options->idempotency_level);
			}
			sv->method[k] = md;
		}
		sv->options = options(sm, 3, 33);
		f->service[i] = sv;
	}
	f->extension = fields(m, FDP_EXTENSION, &f->nextension);
	f->options = options(m, FDP_OPTIONS, 23);
	f->has_source_code_info = pb_has(m, FDP_SOURCE_CODE_INFO);
	if (f->has_source_code_info) {
		const PbMsg *sci = pb_msg(m, FDP_SOURCE_CODE_INFO);
		f->nlocation = pb_count(sci, SCI_LOCATION);
		f->location = xalloc(sizeof(TpdLocation *) * (size_t)(f->nlocation + 1));
		for (int i = 0; i < f->nlocation; i++) {
			const PbMsg *lm = pb_msg_at(sci, SCI_LOCATION, i);
			TpdLocation *l = NEW(TpdLocation);
			l->npath = pb_count(lm, LOC_PATH);
			int32_t *path = xalloc(sizeof(int32_t) * (size_t)(l->npath + 1));
			for (int k = 0; k < l->npath; k++)
				path[k] = (int32_t)pb_int_at(lm, LOC_PATH, k);
			l->path = path;
			l->leading_comments = pb_bytes(lm, LOC_LEADING);
			l->trailing_comments = pb_bytes(lm, LOC_TRAILING);
			l->nleading_detached_comments = pb_count(lm, LOC_LEADING_DETACHED);
			l->leading_detached_comments = xalloc(sizeof(Str) * (size_t)(l->nleading_detached_comments + 1));
			for (int k = 0; k < l->nleading_detached_comments; k++)
				l->leading_detached_comments[k] = pb_bytes_at(lm, LOC_LEADING_DETACHED, k);
			f->location[i] = l;
		}
	}
	return f;
}

TpdRequest *tpd_request(const PbMsg *req)
{
	TpdRequest *r = NEW(TpdRequest);
	r->nfile_to_generate = pb_count(req, CGR_FILE_TO_GENERATE);
	r->file_to_generate = xalloc(sizeof(char *) * (size_t)(r->nfile_to_generate + 1));
	for (int i = 0; i < r->nfile_to_generate; i++)
		r->file_to_generate[i] = pb_str_at(req, CGR_FILE_TO_GENERATE, i);
	r->parameter = pb_bytes(req, CGR_PARAMETER);
	r->nproto_file = pb_count(req, CGR_PROTO_FILE);
	r->proto_file = xalloc(sizeof(TpdFile *) * (size_t)(r->nproto_file + 1));
	for (int i = 0; i < r->nproto_file; i++)
		r->proto_file[i] = file(pb_msg_at(req, CGR_PROTO_FILE, i));
	r->has_compiler_version = pb_has(req, 3);
	if (r->has_compiler_version) {
		const PbMsg *v = pb_msg(req, 3);
		r->major = i32(v, 1, 0);
		r->minor = i32(v, 2, 0);
		r->patch = i32(v, 3, 0);
	}
	return r;
}
