/* Copyright Anthropic, PBC. Licensed under the Apache License, Version 2.0.
 *
 * anthropics/buffa v0.9.2: a port of protoc-gen-buffa 0.9.2 (src/main.rs,
 * https://github.com/anthropics/buffa/tree/v0.9.2/protoc-gen-buffa, buf.build/anthropics/buffa):
 * request decoding options, parse_config with its error texts and the response. Code generation is
 * buffa-codegen 0.9.2 in pkg/languages/rust/buffa-codegen.
 *
 * Unity build: everything but the generator below is static. */
#include "buffa_codegen.h"
#include "rust_std.h"
#include "uni.h"

#include <string.h>

enum { FEATURE_PROTO3_OPTIONAL = 1, FEATURE_SUPPORTS_EDITIONS = 2 };
enum { EDITION_PROTO2 = 998, EDITION_2024 = 1001 };

static char *str_dup(Str s) { return xstrndup(s.p, s.n); }

static const char *parse_bool(const char *key, Str value, bool *out)
{
	Str v = rust_trim(value);
	if (str_eqc(v, "true")) {
		*out = true;
		return NULL;
	}
	if (str_eqc(v, "false")) {
		*out = false;
		return NULL;
	}
	return xcat("invalid boolean value for '", key, "': '", str_dup(v), "', expected true or false");
}

static const char *normalize_proto_path(Str path, const char *label, char **out)
{
	if (!path.n)
		return xcat(label, " rules require a non-empty proto path; use '.' explicitly to match everything");
	if (str_eqc(path, ".")) {
		*out = ".";
		return NULL;
	}
	Buf b = {0};
	if (path.p[0] != '.')
		buf_addc(&b, '.');
	buf_add_str(&b, path);
	while (b.n && b.p[b.n - 1] == '.')
		b.n--;
	if (!b.n)
		return xcat(label, " rules require a non-empty proto path; use '.' explicitly to match everything");
	*out = xstrndup(b.p, b.n);
	return NULL;
}

/* parse_feature_override: only enum_type:OPEN (value case-insensitive). */
static const char *parse_feature_override(Str spec, const char **out)
{
	const char *colon = memchr(spec.p, ':', spec.n);
	if (!colon)
		return xcat("invalid feature override '", str_dup(spec),
			    "', expected '<feature>:<value>' (e.g. 'enum_type:OPEN')");
	Str feature = rust_trim((Str){spec.p, (size_t)(colon - spec.p)});
	Str value = rust_trim((Str){colon + 1, (size_t)(spec.p + spec.n - colon - 1)});
	bool open = value.n == 4;
	for (size_t i = 0; i < value.n && open; i++)
		open = (value.p[i] | 0x20) == "open"[i];
	if (str_eqc(feature, "enum_type") && open) {
		*out = "enum_type:OPEN";
		return NULL;
	}
	return xcat("unsupported feature override '", str_dup(spec), "'; supported overrides: enum_type:OPEN");
}

static BcPair *pair(const char *key, const char *value)
{
	BcPair *p = xalloc(sizeof *p);
	p->key = key;
	p->value = value;
	return p;
}

#define TRY(e)                                                                                     \
	do {                                                                                       \
		const char *err_ = (e);                                                            \
		if (err_)                                                                          \
			return err_;                                                               \
	} while (0)

/* parse_config */
static const char *parse_config(Str params, BcConfig *c)
{
	bc_config_default(c);
	bool unbox_oneof = false;
	if (!params.n)
		return NULL;
	Str *parts;
	int n = str_split_s(params, ',', &parts);
	for (int i = 0; i < n; i++) {
		Str param = rust_trim(parts[i]);
		if (!param.n)
			continue;
		const char *eq = memchr(param.p, '=', param.n);
		if (!eq)
			return xcat("plugin option '", str_dup(param), "' must use key=value syntax");
		Str key_raw = {param.p, (size_t)(eq - param.p)};
		Str value = {eq + 1, (size_t)(param.p + param.n - eq - 1)};
		char *key = str_dup(rust_trim(key_raw));
		bool b;
		if (str_eq(key, "views")) {
			TRY(parse_bool("views", value, &c->generate_views));
		} else if (str_eq(key, "lazy_views")) {
			TRY(parse_bool("lazy_views", value, &c->lazy_views));
		} else if (str_eq(key, "unknown_fields")) {
			TRY(parse_bool("unknown_fields", value, &c->preserve_unknown_fields));
		} else if (str_eq(key, "json")) {
			TRY(parse_bool("json", value, &c->generate_json));
		} else if (str_eq(key, "text")) {
			TRY(parse_bool("text", value, &c->generate_text));
		} else if (str_eq(key, "arbitrary")) {
			TRY(parse_bool("arbitrary", value, &c->generate_arbitrary));
		} else if (str_eq(key, "gate_impls")) {
			TRY(parse_bool("gate_impls", value, &c->gate_impls_on_crate_features));
		} else if (str_eq(key, "json_feature") || str_eq(key, "views_feature") || str_eq(key, "text_feature") ||
			   str_eq(key, "reflect_feature")) {
			Str v = rust_trim(value);
			if (!v.n)
				return xcat("'", key, "' requires a non-empty feature name (an empty name would silently "
					    "disable the gated impls)");
			char *name = str_dup(v);
			if (key[0] == 'j')
				c->feature_gate_json = name;
			else if (key[0] == 'v')
				c->feature_gate_views = name;
			else if (key[0] == 't')
				c->feature_gate_text = name;
			else
				c->feature_gate_reflect = name;
		} else if (str_eq(key, "allow_message_set")) {
			TRY(parse_bool("allow_message_set", value, &c->allow_message_set));
		} else if (str_eq(key, "strict_utf8") || str_eq(key, "strict_utf8_mapping")) {
			TRY(parse_bool(key, value, &c->strict_utf8_mapping));
		} else if (str_eq(key, "register_types")) {
			TRY(parse_bool("register_types", value, &c->emit_register_fn));
		} else if (str_eq(key, "with_setters")) {
			TRY(parse_bool("with_setters", value, &c->generate_with_setters));
		} else if (str_eq(key, "reflection")) {
			TRY(parse_bool("reflection", value, &b));
			c->generate_reflection = c->generate_reflection_vtable = b;
		} else if (str_eq(key, "reflect_mode")) {
			Str v = rust_trim(value);
			if (str_eqc(v, "off"))
				c->generate_reflection = c->generate_reflection_vtable = false;
			else if (str_eqc(v, "bridge"))
				c->generate_reflection = true, c->generate_reflection_vtable = false;
			else if (str_eqc(v, "vtable"))
				c->generate_reflection = c->generate_reflection_vtable = true;
			else
				return xcat("invalid reflect_mode '", str_dup(v), "', expected off, bridge, or vtable");
		} else if (str_eq(key, "shared_descriptor_pool")) {
			TRY(parse_bool("shared_descriptor_pool", value, &c->shared_descriptor_pool));
		} else if (str_eq(key, "file_per_package")) {
			TRY(parse_bool("file_per_package", value, &c->file_per_package));
		} else if (str_eq(key, "idiomatic_imports")) {
			TRY(parse_bool("idiomatic_imports", value, &c->idiomatic_imports));
		} else if (str_eq(key, "idiomatic_field_names")) {
			TRY(parse_bool("idiomatic_field_names", value, &c->idiomatic_field_names));
		} else if (str_eq(key, "unbox_oneof")) {
			TRY(parse_bool("unbox_oneof", value, &unbox_oneof));
		} else if (str_eq(key, "unbox_oneof_in")) {
			char *path;
			TRY(normalize_proto_path(rust_trim(value), "unbox_oneof_in", &path));
			vec_push(&c->unboxed_oneof_fields, path);
		} else if (str_eq(key, "type_name_prefix")) {
			c->type_name_prefix = str_dup(value);
		} else if (str_eq(key, "override_feature_in")) {
			const char *eq2 = memchr(value.p, '=', value.n);
			if (!eq2)
				return xcat("invalid override_feature_in format '", str_dup(value),
					    "', expected 'override_feature_in=<proto_path>=<feature>:<value>' (e.g. "
					    "'override_feature_in=.my.pkg.E=enum_type:OPEN')");
			const char *feature;
			TRY(parse_feature_override(rust_trim((Str){eq2 + 1, (size_t)(value.p + value.n - eq2 - 1)}), &feature));
			char *path;
			TRY(normalize_proto_path(rust_trim((Str){value.p, (size_t)(eq2 - value.p)}), "feature override", &path));
			vec_push(&c->feature_overrides, pair(path, feature));
		} else if (str_eq(key, "open_enums_in")) {
			char *path;
			TRY(normalize_proto_path(rust_trim(value), "feature override", &path));
			vec_push(&c->feature_overrides, pair(path, "enum_type:OPEN"));
		} else if (str_eq(key, "exclude_package")) {
			const char *err = NULL;
			char *pkg = bc_normalize_exclude_package(str_dup(value), &err);
			if (!pkg)
				return err;
			vec_push(&c->exclude_packages, pkg);
		} else if (str_eq(key, "extern_path")) {
			const char *eq2 = memchr(value.p, '=', value.n);
			Str proto = {0}, rust = {0};
			if (eq2) {
				proto = rust_trim((Str){value.p, (size_t)(eq2 - value.p)});
				rust = rust_trim((Str){eq2 + 1, (size_t)(value.p + value.n - eq2 - 1)});
			}
			if (!proto.n || !rust.n)
				return xcat("invalid extern_path format '", str_dup(value),
					    "', expected 'extern_path=.proto.pkg=::rust::path' (or a type FQN, "
					    "'extern_path=.proto.pkg.Type=::rust::path::Type')");
			char *p = proto.p[0] == '.' ? str_dup(proto) : xcat(".", str_dup(proto));
			vec_push(&c->extern_paths, pair(p, str_dup(rust)));
		} else if (str_eq(key, "mod_file")) {
			return "the mod_file option was removed in 0.2; use protoc-gen-buffa-packaging instead. See "
			       "CHANGELOG for migration.";
		} else if (str_eq(key, BC_ELEMENT_MEMORY_LIMIT_OPT)) {
			/* consumed before the request was decoded */
		} else {
			return xcat("unknown plugin option '", key,
				    "'; see <https://github.com/anthropics/buffa/blob/main/docs/guide.md#plugin-options> "
				    "for the supported options");
		}
	}
	if (unbox_oneof)
		vec_push(&c->unboxed_oneof_fields, ".");
	if (c->shared_descriptor_pool && !c->generate_reflection)
		return "shared_descriptor_pool requires reflection to be enabled (pass reflection=true or "
		       "reflect_mode=bridge|vtable)";
	if (c->shared_descriptor_pool && c->file_per_package)
		return "shared_descriptor_pool is not supported together with file_per_package (the shared "
		       "__buffa_fds root module is emitted by protoc-gen-buffa-packaging's mod.rs, which the "
		       "file_per_package workflow does not use); drop one of the two options, or use buffa-build "
		       "for a shared pool without the packaging plugin";
	if (c->shared_descriptor_pool && bc_reflect_feature_gate(c))
		return "shared_descriptor_pool is not supported together with gate_impls=true on the protoc "
		       "plugin path (the packaging plugin cannot gate the shared module); use buffa-build for "
		       "gated shared pools, or drop the gate";
	if (c->shared_descriptor_pool && c->feature_overrides.n)
		return "shared_descriptor_pool is not supported together with feature overrides (open_enums_in / "
		       "override_feature_in) on the protoc plugin path (the packaging plugin cannot see the "
		       "overrides, so the shared descriptor pool would not match the generated code); use "
		       "buffa-build for shared pools with overrides, or drop shared_descriptor_pool";
	return NULL;
}

/* peek_request_parameter: the last parameter field of the raw request (pkg/pb parsed it, so the
 * wire format is valid). */
static Str raw_parameter(const PbMsg *req, Str raw)
{
	(void)raw;
	const PbSlot *s = pb_slot(req, CGR_PARAMETER);
	return s && s->n ? s->v[s->n - 1].s : (Str){"", 0};
}

typedef struct {
	PbMsg *req;
	Str raw;
	const char *error;
	Vec files;
} Run;

/* run() */
static void run(void *arg)
{
	Run *r = arg;
	r->error = bc_decode_request(r->raw, raw_parameter(r->req, r->raw));
	if (r->error)
		return;
	BcConfig config;
	r->error = parse_config(raw_parameter(r->req, r->raw), &config);
	if (r->error)
		return;
	r->error = bc_generate(&config, r->req, &r->files);
}

static void write_error(Buf *out, Str error)
{
	pb_put_bytes(out, CGRESP_ERROR, error);
	pb_put_tag(out, CGRESP_SUPPORTED_FEATURES, WT_VARINT);
	pb_put_varint(out, FEATURE_PROTO3_OPTIONAL | FEATURE_SUPPORTS_EDITIONS);
}

static const char *generate(PbMsg *req, Str raw, Buf *out)
{
	Run r = {0};
	r.req = req;
	r.raw = raw;
	if (try_run(run, &r)) {
		write_error(out, fail_message());
		return NULL;
	}
	if (r.error) {
		write_error(out, str_c(r.error));
		return NULL;
	}
	pb_put_tag(out, CGRESP_SUPPORTED_FEATURES, WT_VARINT);
	pb_put_varint(out, FEATURE_PROTO3_OPTIONAL | FEATURE_SUPPORTS_EDITIONS);
	pb_put_tag(out, CGRESP_MINIMUM_EDITION, WT_VARINT);
	pb_put_varint(out, EDITION_PROTO2);
	pb_put_tag(out, CGRESP_MAXIMUM_EDITION, WT_VARINT);
	pb_put_varint(out, EDITION_2024);
	for (int i = 0; i < r.files.n; i++) {
		const BcFile *f = r.files.v[i];
		Buf b = {0};
		pb_put_bytes(&b, CGRESP_FILE_NAME, str_c(f->name));
		pb_put_bytes(&b, CGRESP_FILE_CONTENT, f->content);
		pb_put_bytes(out, CGRESP_FILE, buf_view(&b));
	}
	return NULL;
}

const UniGenerator gen_anthropics_buffa_v0_9_2 = {
	.name = "anthropics/buffa",
	.version = "v0.9.2",
	.generate = generate,
};
