/* Copyright (c) 2021 PlanetScale Inc. All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file. */

/* community/planetscale-vtprotobuf v0.6.0: a port of protoc-gen-go-vtproto v0.6.0
 * (https://github.com/planetscale/vtprotobuf, buf.build/community/planetscale-vtprotobuf):
 * cmd/protoc-gen-go-vtproto/main.go and generator/{generator,features,helpers,generatedfile}.go
 * (below), generator/pattern/match.go (pattern.c) and the features (clone.c, equal.c, grpc.c,
 * marshal.c, pool.c, size.c, unmarshal.c). The plugin framework is compiler/protogen of
 * protobuf-go v1.31.0, the version pinned by vtprotobuf's go.mod: pkg/languages/go/protogen with
 * PgOptions.Version = &pg_v1_31_0.
 *
 * Unity build: this file includes the other .c files of the version directory, and everything
 * but the generator below is static.
 *
 * Argument evaluation order: Go evaluates the arguments of P left to right, and P qualifies
 * GoIdent arguments in order, which assigns import names (e.g. timestamppb, timestamppb1). C
 * leaves the order of initializers unspecified, so calls that qualify identifiers are made in
 * statements before the P that uses their result. */
#include "go_std.h"
#include "protogen_v1.31.0.h"
#include "genid.h"
#include "uni.h"

#include <stdlib.h>
#include <string.h>

char *go_itoa(int64_t v);

static char map_true;
#define MAP_TRUE ((void *)&map_true)

#define PROGRAM "protoc-gen-go-vtproto"
#define VT_VERSION "v0.6.0"
/* protoimpl.GenVersion of protobuf-go v1.31.0 */
#define PROTOIMPL_GEN_VERSION 20

/* vtproto.mempool (vtproto/ext.proto), linked into the plugin */
#define VTPROTO_MEMPOOL_FIELD_NUMBER 64101

#include "pattern.c"

/* ---- generator/generator.go: ObjectSet and Config ---- */

/* ObjectSet: a set of patterns. */
typedef struct {
	Vec patterns; /* Str * */
} ObjectSet;

static COLD bool object_set_contains(const ObjectSet *o, PgGoIdent g)
{
	/* Nothing allocated here is kept (see pattern.c). */
	ArenaMark mark = arena_mark();
	const char *object_path = xcat(g.GoImportPath, ".", g.GoName);
	bool found = false;
	for (int i = 0; i < o->patterns.n && !found; i++) {
		/* Ignore malformed pattern error because pattern already checked in Set */
		found = pat_match(*(Str *)o->patterns.v[i], str_c(object_path)) == 1;
	}
	arena_release(mark);
	return found;
}

/* ObjectSet.Set: path.ErrBadPattern ("syntax error in pattern") */
static COLD Str object_set_set(ObjectSet *o, Str s)
{
	if (!pat_validate(s))
		return STR_LIT("syntax error in pattern");
	Str *p = xalloc(sizeof *p);
	*p = s;
	vec_push(&o->patterns, p);
	return (Str){NULL, 0};
}

typedef struct {
	/* Poolable rules determines if pool feature generate for particular message */
	ObjectSet Poolable;
	/* PoolableExclude rules determines if pool feature disabled for particular message */
	ObjectSet PoolableExclude;
	bool Wrap;
	bool AllowEmpty;
	Str BuildTag;
} Config;

/* ---- generator/generatedfile.go ---- */

/* GeneratedFile: a protogen.GeneratedFile with the configuration. */
typedef struct {
	PgGeneratedFile *g;
	const Config *Config;
	const Map *LocalPackages; /* package -> MAP_TRUE */
} VtFile;

/* Ident: QualifiedGoIdent of path.ident */
static const char *vt_ident(VtFile *p, const char *path, const char *ident)
{
	return pg_qualified_go_ident(p->g, pg_ident(path, ident));
}

/* proto.GetExtension(message.Desc.Options(), vtproto.E_Mempool): the extension is linked into the
 * plugin, so protobuf-go parses it with the request (last value wins); it is not in the schema of
 * the request here and stays an unknown field. Absent, it is false. */
static bool vt_mempool_option(const PgMessage *message)
{
	bool v = false;
	PbReader r = pb_unknown_fields(message->Desc->d.options);
	PbRawField f;
	while (pb_next_field(&r, &f))
		if (f.num == VTPROTO_MEMPOOL_FIELD_NUMBER && f.wt == WT_VARINT)
			v = f.v != 0;
	return v;
}

static bool vt_should_pool(VtFile *p, const PgMessage *message)
{
	/* Do not generate pool if message is nil or message excluded by external rules */
	if (!message || object_set_contains(&p->Config->PoolableExclude, message->GoIdent))
		return false;
	if (object_set_contains(&p->Config->Poolable, message->GoIdent))
		return true;
	return vt_mempool_option(message);
}

static void vt_alloc(VtFile *p, const char *vname, const PgMessage *message, bool is_qualified_ident)
{
	const char *ident = message->GoIdent.GoName;
	if (is_qualified_ident)
		ident = pg_qualified_go_ident(p->g, message->GoIdent);
	if (vt_should_pool(p, message))
		P(p->g, vname, " := ", ident, "FromVTPool()");
	else
		P(p->g, vname, " := new(", ident, ")");
}

/* FieldGoType: the Go type of a field and whether it is a pointer. */
static const char *vt_field_go_type(VtFile *p, const PgField *field, bool *pointer)
{
	bool ptr = pg_v131_field_has_presence(field->Desc);
	const char *go_type = "";
	switch (field->Desc->kind) {
	case PT_BOOL: go_type = "bool"; break;
	case PT_ENUM: go_type = pg_qualified_go_ident(p->g, field->Enum->GoIdent); break;
	case PT_INT32:
	case PT_SINT32:
	case PT_SFIXED32: go_type = "int32"; break;
	case PT_UINT32:
	case PT_FIXED32: go_type = "uint32"; break;
	case PT_INT64:
	case PT_SINT64:
	case PT_SFIXED64: go_type = "int64"; break;
	case PT_UINT64:
	case PT_FIXED64: go_type = "uint64"; break;
	case PT_FLOAT: go_type = "float32"; break;
	case PT_DOUBLE: go_type = "float64"; break;
	case PT_STRING: go_type = "string"; break;
	case PT_BYTES:
		go_type = "[]byte";
		ptr = false; /* rely on nullability of slices for presence */
		break;
	case PT_MESSAGE:
	case PT_GROUP:
		go_type = xcat("*", pg_qualified_go_ident(p->g, field->Message->GoIdent));
		ptr = false; /* pointer captured as part of the type */
		break;
	}
	if (pointer)
		*pointer = false;
	if (go_field_is_list(field->Desc))
		return xcat("[]", go_type);
	if (go_field_is_map(field->Desc)) {
		const char *key_type = vt_field_go_type(p, field->Message->Fields[0], NULL);
		const char *val_type = vt_field_go_type(p, field->Message->Fields[1], NULL);
		return xcat("map[", key_type, "]", val_type);
	}
	if (pointer)
		*pointer = ptr;
	return go_type;
}

static bool vt_is_local_message(const VtFile *p, const PgMessage *message)
{
	if (!message)
		return false;
	return map_get(p->LocalPackages, message->Desc->d.file->d.full_name) != NULL;
}

static bool vt_is_local_field(const VtFile *p, const PgField *field)
{
	if (!field)
		return false;
	return map_get(p->LocalPackages, field->Desc->d.file->d.full_name) != NULL;
}

#define VT_HELPERS_PACKAGE "github.com/planetscale/vtprotobuf/protohelpers"

/* Helper: EncodeVarint, SizeOfVarint, SizeOfZigzag, Skip, ErrInvalidLength, ErrIntOverflow,
 * ErrUnexpectedEndOfGroup */
static PgGoIdent vt_helper(const char *name) { return pg_ident(VT_HELPERS_PACKAGE, name); }

#define VT_WELL_KNOWN_PACKAGE "github.com/planetscale/vtprotobuf/types/known/"

static const struct {
	const char *full_name, *go_name, *pkg;
} well_known_types[] = {
	{"google.protobuf.Any", "Any", "anypb"},
	{"google.protobuf.Duration", "Duration", "durationpb"},
	{"google.protobuf.Empty", "Empty", "emptypb"},
	{"google.protobuf.FieldMask", "FieldMask", "fieldmaskpb"},
	{"google.protobuf.Timestamp", "Timestamp", "timestamppb"},
	{"google.protobuf.DoubleValue", "DoubleValue", "wrapperspb"},
	{"google.protobuf.FloatValue", "FloatValue", "wrapperspb"},
	{"google.protobuf.Int64Value", "Int64Value", "wrapperspb"},
	{"google.protobuf.UInt64Value", "UInt64Value", "wrapperspb"},
	{"google.protobuf.Int32Value", "Int32Value", "wrapperspb"},
	{"google.protobuf.UInt32Value", "UInt32Value", "wrapperspb"},
	{"google.protobuf.BoolValue", "BoolValue", "wrapperspb"},
	{"google.protobuf.StringValue", "StringValue", "wrapperspb"},
	{"google.protobuf.BytesValue", "BytesValue", "wrapperspb"},
	{"google.protobuf.Struct", "Struct", "structpb"},
	{"google.protobuf.Value", "Value", "structpb"},
	{"google.protobuf.ListValue", "ListValue", "structpb"},
};

static const struct {
	const char *full_name, *go_name;
} well_known_fields[] = {
	{"google.protobuf.Value.null_value", "Value_NullValue"},
	{"google.protobuf.Value.number_value", "Value_NumberValue"},
	{"google.protobuf.Value.string_value", "Value_StringValue"},
	{"google.protobuf.Value.bool_value", "Value_BoolValue"},
	{"google.protobuf.Value.struct_value", "Value_StructValue"},
	{"google.protobuf.Value.list_value", "Value_ListValue"},
};

static int vt_well_known_type_index(const PgMessage *message)
{
	for (size_t i = 0; i < sizeof well_known_types / sizeof *well_known_types; i++)
		if (str_eq(message->Desc->d.full_name, well_known_types[i].full_name))
			return (int)i;
	return -1;
}

static bool vt_is_well_known_type(const PgMessage *message) { return message && vt_well_known_type_index(message) >= 0; }

static PgGoIdent vt_well_known_field_map(const VtFile *p, const PgField *field)
{
	if (!field)
		return pg_ident("", "");
	for (size_t i = 0; i < sizeof well_known_fields / sizeof *well_known_fields; i++) {
		if (!str_eq(field->Desc->d.full_name, well_known_fields[i].full_name))
			continue;
		PgGoIdent res = pg_ident(VT_WELL_KNOWN_PACKAGE "structpb", well_known_fields[i].go_name);
		if (vt_is_local_field(p, field))
			res.GoImportPath = "";
		return res;
	}
	fail("%s", field->Desc->d.full_name); /* panic(field.Desc.FullName()) */
}

static PgGoIdent vt_well_known_type_map(const VtFile *p, const PgMessage *message)
{
	if (!message)
		return pg_ident("", "");
	int i = vt_well_known_type_index(message);
	PgGoIdent res = i < 0 ? pg_ident("", "") : pg_ident(xcat(VT_WELL_KNOWN_PACKAGE, well_known_types[i].pkg), well_known_types[i].go_name);
	if (vt_is_local_message(p, message))
		res.GoImportPath = "";
	return res;
}

static bool vt_wrapper(const VtFile *p) { return p->Config->Wrap; }

/* ---- generator/helpers.go ---- */

#define PROTO_PKG "google.golang.org/protobuf/proto"

/* protowire.Type */
enum { WIRE_VARINT = 0, WIRE_FIXED64 = 1, WIRE_BYTES = 2, WIRE_START_GROUP = 3, WIRE_END_GROUP = 4, WIRE_FIXED32 = 5 };

static int key_size(int32_t field_number, int wire_type)
{
	uint32_t x = (uint32_t)field_number << 3 | (uint32_t)wire_type;
	int size = 0;
	for (size = 0; x > 127; size++)
		x >>= 7;
	size++;
	return size;
}

/* ProtoWireType: the wire type of a kind (0, VarintType, for an unknown kind). */
static int proto_wire_type(int kind)
{
	switch (kind) {
	case PT_SFIXED32:
	case PT_FIXED32:
	case PT_FLOAT: return WIRE_FIXED32;
	case PT_SFIXED64:
	case PT_FIXED64:
	case PT_DOUBLE: return WIRE_FIXED64;
	case PT_STRING:
	case PT_BYTES:
	case PT_MESSAGE: return WIRE_BYTES;
	case PT_GROUP: return WIRE_START_GROUP;
	default: return WIRE_VARINT;
	}
}

/* Shared descriptor predicates of the features. */
static bool is_real_oneof(const PgField *f) { return f->Oneof && !pg_v131_oneof_is_synthetic(f->Oneof->Desc); }
static bool is_repeated(const PgField *f) { return f->Desc->cardinality == GO_REPEATED; }

/* sort.Slice(message.Fields, by number) (field numbers are unique) */
static int cmp_field_number(const void *a, const void *b)
{
	int32_t x = (*(PgField *const *)a)->Desc->number, y = (*(PgField *const *)b)->Desc->number;
	return x < y ? -1 : x > y;
}

static void sort_fields_by_number(PgMessage *message)
{
	qsort(message->Fields, (size_t)message->nFields, sizeof *message->Fields, cmp_field_number);
}

/* isScalar of the clone and equal features */
static bool is_scalar(int kind)
{
	switch (kind) {
	case PT_BOOL:
	case PT_STRING:
	case PT_DOUBLE:
	case PT_FIXED64:
	case PT_SFIXED64:
	case PT_FLOAT:
	case PT_FIXED32:
	case PT_SFIXED32:
	case PT_INT64:
	case PT_UINT64:
	case PT_SINT64:
	case PT_INT32:
	case PT_UINT32:
	case PT_SINT32:
	case PT_ENUM: return true;
	}
	return false;
}

/* ---- features ---- */

#include "clone.c"
#include "equal.c"
#include "grpc.c"
#include "marshal.c"
#include "pool.c"
#include "size.c"
#include "unmarshal.c"

/* generator/features.go: the registered features, sorted by name (findFeatures sorts them). */
typedef bool (*FeatureFn)(VtFile *gen, PgFile *file);
static const struct {
	const char *name;
	FeatureFn fn;
} default_features[] = {
	{"clone", clone_generate_file},
	{"equal", equal_generate_file},
	{"grpc", grpc_generate_file},
	{"marshal", marshal_generate_file},
	{"marshal_strict", marshal_strict_generate_file},
	{"pool", pool_generate_file},
	{"size", size_generate_file},
	{"unmarshal", unmarshal_generate_file},
	{"unmarshal_unsafe", unmarshal_unsafe_generate_file},
};
#define N_FEATURES ((int)(sizeof default_features / sizeof *default_features))

/* findFeatures: the selected features (indexes into default_features) in name order, or the
 * error. */
static COLD Str find_features(Str feature_names, bool selected[N_FEATURES])
{
	Str *names;
	int n = str_split_s(feature_names, '+', &names);
	for (int i = 0; i < n; i++) {
		if (str_eqc(names[i], "all")) {
			for (int k = 0; k < N_FEATURES; k++)
				selected[k] = true;
			break;
		}
		int k = 0;
		while (k < N_FEATURES && !str_eqc(names[i], default_features[k].name))
			k++;
		if (k == N_FEATURES) {
			Buf b = {0};
			buf_adds(&b, "unknown feature: ");
			buf_adds(&b, quote_go(names[i]));
			return buf_view(&b);
		}
		selected[k] = true;
	}
	return (Str){NULL, 0};
}

typedef struct {
	PgPlugin *plugin;
	const Config *cfg;
	bool features[N_FEATURES];
	Map local;
} Generator;

#define SUPPORTED_FEATURES 1 /* CodeGeneratorResponse_FEATURE_PROTO3_OPTIONAL */

static COLD Str new_generator(Generator *gen, PgPlugin *plugin, Str feature_names, const Config *cfg)
{
	plugin->SupportedFeatures = SUPPORTED_FEATURES;
	Str err = find_features(feature_names, gen->features);
	if (err.p)
		return err;
	for (int i = 0; i < plugin->nFiles; i++)
		if (plugin->Files[i]->Generate)
			map_put(&gen->local, plugin->Files[i]->Desc->d.full_name, MAP_TRUE);
	gen->plugin = plugin;
	gen->cfg = cfg;
	return (Str){NULL, 0};
}

static void generate_file(Generator *gen, PgGeneratedFile *gf, PgFile *file)
{
	VtFile f = {gf, gen->cfg, &gen->local};
	VtFile *p = &f;

	if (p->Config->BuildTag.n) {
		/* Support both forms of tags for maximum compatibility */
		P(p->g, "//go:build ", p->Config->BuildTag);
		P(p->g, "// +build ", p->Config->BuildTag);
	}
	P(p->g, "// Code generated by protoc-gen-go-vtproto. DO NOT EDIT.");
	P(p->g, "// protoc-gen-go-vtproto version: ", VT_VERSION);
	P(p->g, "// source: ", file->Desc->d.name);
	P(p->g, "");
	P(p->g, "package ", file->GoPackageName);
	P(p->g, "");

#define PROTOIMPL_PACKAGE "google.golang.org/protobuf/runtime/protoimpl"
	P(p->g, "const (");
	P(p->g, "// Verify that this generated code is sufficiently up-to-date.");
	P(p->g, "_ = ", pg_ident(PROTOIMPL_PACKAGE, "EnforceVersion"), "(", PROTOIMPL_GEN_VERSION, " - ",
	  pg_ident(PROTOIMPL_PACKAGE, "MinVersion"), ")");
	P(p->g, "// Verify that runtime/protoimpl is sufficiently up-to-date.");
	P(p->g, "_ = ", pg_ident(PROTOIMPL_PACKAGE, "EnforceVersion"), "(", pg_ident(PROTOIMPL_PACKAGE, "MaxVersion"), " - ",
	  PROTOIMPL_GEN_VERSION, ")");
	P(p->g, ")");
	P(p->g, "");

	if (vt_wrapper(p)) {
		for (int i = 0; i < file->nMessages; i++) {
			PgMessage *msg = file->Messages[i];
			P(p->g, "type ", msg->GoIdent.GoName, " ", msg->GoIdent);
			for (int k = 0; k < msg->nOneofs; k++) {
				PgOneof *one = msg->Oneofs[k];
				for (int j = 0; j < one->nFields; j++)
					P(p->g, "type ", one->Fields[j]->GoIdent.GoName, " ", one->Fields[j]->GoIdent);
			}
		}
	}

	bool generated = false;
	for (int k = 0; k < N_FEATURES; k++)
		if (gen->features[k] && default_features[k].fn(p, file))
			generated = true;

	if (!generated && !gen->cfg->AllowEmpty)
		pg_skip(gf);
}

static void generator_generate(Generator *gen)
{
	for (int i = 0; i < gen->plugin->nFiles; i++) {
		PgFile *file = gen->plugin->Files[i];
		if (!file->Generate)
			continue;
		const char *import_path = "";
		if (!gen->cfg->Wrap)
			import_path = file->GoImportPath;
		PgGeneratedFile *gf = pg_new_generated_file(gen->plugin, xcat(file->GeneratedFilenamePrefix, "_vtproto.pb.go"), import_path);
		generate_file(gen, gf, file);
	}
}

/* ---- cmd/protoc-gen-go-vtproto/main.go ---- */

typedef struct {
	Config cfg;
	Str features;
} Flags;

/* strconv.ParseBool */
static COLD bool parse_bool(Str s, bool *v)
{
	static const char *const t[] = {"1", "t", "T", "TRUE", "true", "True"};
	static const char *const f[] = {"0", "f", "F", "FALSE", "false", "False"};
	for (int i = 0; i < 6; i++) {
		if (str_eqc(s, t[i])) {
			*v = true;
			return true;
		}
		if (str_eqc(s, f[i])) {
			*v = false;
			return true;
		}
	}
	return false;
}


/* flag.FlagSet.Set of the flags of main.go (Go 1.21.6 flag package). */
static COLD Str set_flag(void *ctx, Str name, Str value)
{
	Flags *fl = ctx;
	bool *b = NULL;
	if (str_eqc(name, "allow-empty"))
		b = &fl->cfg.AllowEmpty;
	else if (str_eqc(name, "wrap"))
		b = &fl->cfg.Wrap;
	if (b) {
		/* boolValue.Set: the value is set even if it does not parse */
		bool v = false;
		bool ok = parse_bool(value, &v);
		*b = v;
		return ok ? (Str){NULL, 0} : STR_LIT("parse error");
	}
	if (str_eqc(name, "pool"))
		return object_set_set(&fl->cfg.Poolable, value);
	if (str_eqc(name, "pool-exclude"))
		return object_set_set(&fl->cfg.PoolableExclude, value);
	if (str_eqc(name, "features")) {
		fl->features = value;
		return (Str){NULL, 0};
	}
	if (str_eqc(name, "buildTag")) {
		fl->cfg.BuildTag = value;
		return (Str){NULL, 0};
	}
	Buf e = {0};
	buf_adds(&e, "no such flag -");
	buf_add_str(&e, name);
	return buf_view(&e);
}

typedef struct {
	PgPlugin *plugin;
	Flags *fl;
	Buf *out;
} RunCtx;

static void run(void *arg)
{
	RunCtx *c = arg;
	Generator gen = {0};
	Str err = new_generator(&gen, c->plugin, c->fl->features, &c->fl->cfg);
	if (err.p) {
		/* Errors from the plugin function are reported in the CodeGeneratorResponse. */
		pg_plugin_error(c->plugin, err);
	} else {
		generator_generate(&gen);
	}
	pg_plugin_response(c->plugin, c->out);
}

/* The extensions in protoregistry.GlobalTypes of protoc-gen-go-vtproto. */
static const char *const linked_extensions[] = {"vtproto.mempool", NULL};

/* An error of Options.New or a panic: upstream prints "protoc-gen-go-vtproto: <err>" to stderr and
 * exits with status 1 (a panic: a goroutine trace with status 2); the port reports the same text
 * in the CodeGeneratorResponse.error. */
static COLD void error_response(Buf *out, Str err)
{
	Buf b = {0};
	buf_adds(&b, PROGRAM ": ");
	buf_add_str(&b, err);
	out->n = 0;
	pb_put_bytes(out, CGRESP_ERROR, buf_view(&b));
}

static COLD const char *generate1(PbMsg *uni_req, Str raw, Buf *out)
{
	/* protogen.Options.Run: proto.Unmarshal of the request (protobuf-go semantics) and
	 * Options.New. The parameter is the one without the uni-* options. */
	const char *perr;
	PbMsg *req = pg_parse_request((const uint8_t *)raw.p, raw.n, &perr);
	if (!req && strcmp(perr, GOPB_ERR_TOO_DEEP) == 0) {
		/* a request upstream accepts and the port does not support */
		pb_put_bytes(out, CGRESP_ERROR, str_c(perr));
		return NULL;
	}
	if (!req)
		return xcat(PROGRAM ": ", perr); /* unparsable: stderr and exit status 1, as upstream */
	pb_set_bytes(req, CGR_PARAMETER, pb_bytes(uni_req, CGR_PARAMETER));
	if ((perr = pg_unsupported(req))) {
		pb_put_bytes(out, CGRESP_ERROR, str_c(perr));
		return NULL;
	}

	Flags fl = {0};
	fl.features = STR_LIT("all");
	fl.cfg.BuildTag = (Str){"", 0};
	PgOptions opts = {0};
	opts.ParamFunc = set_flag;
	opts.ParamCtx = &fl;
	opts.LinkedExtensions = linked_extensions;
	opts.Version = &pg_v1_31_0;
	Str err;
	PgPlugin *plugin = pg_options_new(&opts, req, &err);
	if (!plugin) {
		error_response(out, err);
		return NULL;
	}
	RunCtx c = {plugin, &fl, out};
	pat_work = 0;
	pat_unsupported = false;
	if (try_run(run, &c)) {
		if (pat_unsupported) {
			out->n = 0; /* a limit of the pattern matcher (pattern.c), not an upstream error */
			pb_put_bytes(out, CGRESP_ERROR, fail_message());
		} else {
			error_response(out, fail_message());
		}
	}
	return NULL;
}

static COLD const char *generate(PbMsg *uni_req, Str raw, Buf *out)
{
	/* internal/detrand.Bool() of the buf.build plugin binary: true ("proto:" with a no-break
	 * space in errors, two spaces between the fields of the .pb.go.meta files). */
	go_detrand = true;
	go_std_use(&go_std_go1_26_5); /* Go 1.21.6 prints this plugin's output like Go 1.26.5 */
	const char *err = generate1(uni_req, raw, out);
	go_std_use(NULL);
	go_detrand = false;
	return err;
}

/* parses_raw: generate parses raw with protobuf-go semantics (10000 levels) and takes only the
 * parameter from uni_req. */
const UniGenerator gen_community_planetscale_vtprotobuf_v0_6_0 = {
	.name = "community/planetscale-vtprotobuf",
	.version = VT_VERSION,
	.generate = generate,
	.parses_raw = true,
};
