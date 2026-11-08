// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* Port of src/google/protobuf/descriptor.cc: DescriptorPool::BuildFile with the DescriptorBuilder
 * (symbol tables, name resolution, cross-linking, feature resolution, the validation of
 * ValidateOptions and its error texts), the deferred feature-lifetime validation and CopyTo. The
 * descriptors, pool tables, predicates and feature merging are pkg/proto's (proto.h).
 *
 * The pool of a plugin is a plain DescriptorPool: dependencies are enforced, unknown dependencies
 * are not allowed, weak dependencies are not enforced, naming style, proto limits and symbol
 * visibility are not checked. Not ported: the OptionInterpreter (an uninterpreted_option fails
 * the build with an error that names it; protoc and buf send interpreted options), the
 * unused-import warnings and other warnings (upstream only logs them). Errors are collected like the default error collector logs them:
 *
 *	Invalid proto descriptor for file "<file>":
 *	  <element>: <message>
 */

/* ---- descriptor.h accessors ---- */

static int cpp_type_of(int type)
{
	switch (type) {
	case PT_DOUBLE: return LP_CPPTYPE_DOUBLE;
	case PT_FLOAT: return LP_CPPTYPE_FLOAT;
	case PT_INT64: case PT_SFIXED64: case PT_SINT64: return LP_CPPTYPE_INT64;
	case PT_UINT64: case PT_FIXED64: return LP_CPPTYPE_UINT64;
	case PT_INT32: case PT_SFIXED32: case PT_SINT32: return LP_CPPTYPE_INT32;
	case PT_UINT32: case PT_FIXED32: return LP_CPPTYPE_UINT32;
	case PT_BOOL: return LP_CPPTYPE_BOOL;
	case PT_STRING: case PT_BYTES: return LP_CPPTYPE_STRING;
	case PT_GROUP: case PT_MESSAGE: return LP_CPPTYPE_MESSAGE;
	case PT_ENUM: return LP_CPPTYPE_ENUM;
	default: return 0;
	}
}

int lp_field_cpp_type(const LpField *f) { return cpp_type_of(f->type); }

/* The predicates of FieldDescriptor & co. are pkg/proto's (proto_field_has_presence, ...). */

const char *lp_edition_name(int edition)
{
	switch (edition) {
	case 0: return "UNKNOWN";
	case 900: return "LEGACY";
	case 998: return "PROTO2";
	case 999: return "PROTO3";
	case 1000: return "2023";
	case 1001: return "2024";
	case 1002: return "2026";
	case 9999: return "UNSTABLE";
	case 1: return "1_TEST_ONLY";
	case 2: return "2_TEST_ONLY";
	case 99997: return "99997_TEST_ONLY";
	case 99998: return "99998_TEST_ONLY";
	case 99999: return "99999_TEST_ONLY";
	case 0x7FFFFFFF: return "MAX";
	default: return "";
	}
}

static bool is_legacy_edition(int edition) { return edition < LP_EDITION_2023; }

char *lp_field_default_value_as_string(const LpField *f, bool quote_string_type)
{
	switch (cpp_type_of(f->type)) {
	case LP_CPPTYPE_INT32:
	case LP_CPPTYPE_INT64: return lp_itoa(f->default_value.i);
	case LP_CPPTYPE_UINT32:
	case LP_CPPTYPE_UINT64: return lp_utoa(f->default_value.u);
	case LP_CPPTYPE_FLOAT: return lp_simple_ftoa(f->default_value.f);
	case LP_CPPTYPE_DOUBLE: return lp_simple_dtoa(f->default_value.d);
	case LP_CPPTYPE_BOOL: return xstrdup(f->default_value.b ? "true" : "false");
	case LP_CPPTYPE_STRING:
		if (quote_string_type)
			return xcat("\"", lp_cescape(f->default_value_string), "\"");
		if (f->type == PT_BYTES)
			return lp_cescape(f->default_value_string);
		return xstrndup(f->default_value_string.p, f->default_value_string.n);
	case LP_CPPTYPE_ENUM: return xstrdup(f->default_value_enum ? f->default_value_enum->name : "");
	default: return xstrdup("");
	}
}

char *lp_field_camelcase_name(const LpField *f) { return proto_to_camel_case(f->name, true); }

/* internal::cpp::IsGroupLike */
bool lp_field_is_group_like(const LpField *f)
{
	if (f->type != PT_GROUP || !f->message_type)
		return false;
	const char *n = f->name, *t = f->message_type->name;
	for (; *n && *t; n++, t++)
		if (*n != ascii_tolower(*t))
			return false;
	if (*n || *t)
		return false;
	if (f->message_type->file != f->file)
		return false;
	return f->is_extension ? f->message_type->containing_type == f->extension_scope
			       : f->message_type->containing_type == f->containing_type;
}

static char *enum_value_to_pascal_case(const char *in)
{
	Buf b = {0};
	bool next_upper = true;
	for (const char *p = in; *p; p++) {
		if (*p == '_')
			next_upper = true;
		else {
			buf_addc(&b, next_upper ? ascii_toupper(*p) : ascii_tolower(*p));
			next_upper = false;
		}
	}
	return (char *)buf_str(&b);
}

/* PrefixRemover(prefix).MaybeRemove(str) */
static const char *prefix_remover_maybe_remove(const char *prefix_in, const char *str)
{
	Buf pb = {0};
	for (const char *p = prefix_in; *p; p++)
		if (*p != '_')
			buf_addc(&pb, ascii_tolower(*p));
	const char *prefix = buf_str(&pb);
	size_t i, j, n = strlen(str), pn = strlen(prefix);
	for (i = 0, j = 0; i < n && j < pn; i++) {
		if (str[i] == '_')
			continue;
		if (ascii_tolower(str[i]) != prefix[j++])
			return str;
	}
	if (j < pn)
		return str;
	while (i < n && str[i] == '_')
		i++;
	if (i == n)
		return str;
	return str + i;
}

/* io::Tokenizer::IsIdentifier */
static bool is_identifier(const char *s)
{
	if (!*s || !(ascii_isalpha((unsigned char)*s) || *s == '_'))
		return false;
	for (s++; *s; s++)
		if (!(ascii_isalnum((unsigned char)*s) || *s == '_'))
			return false;
	return true;
}

static bool str_has_nul(Str s) { return memchr(s.p, 0, s.n) != NULL; }

/* ---- pool tables ---- */

static LpPool *lp_pool_new(LpPool *builtin, PbMsg *defaults, const PbMsgDef *feature_set_def)
{
	LpPool *p = xalloc(sizeof *p);
	p->builtin = builtin;
	p->version = builtin ? builtin->version : NULL;
	p->feature_set_defaults = defaults;
	p->feature_set_def = feature_set_def;
	p->empty_features = pb_new(feature_set_def);
	return p;
}

/* LpVersion.legacy_descriptor_builder */
static bool pool_legacy_builder(const LpPool *pool) { return pool->version && pool->version->legacy_descriptor_builder; }

static LpFile *lp_find_file(const LpPool *pool, const char *name) { return proto_pool_find_file(&pool->base, name); }
static LpSymbol *lp_find_symbol(const LpPool *pool, const char *name) { return proto_pool_find_symbol(&pool->base, name); }
static const LpMessage *lp_find_message(const LpPool *pool, const char *name) { return proto_pool_find_message(&pool->base, name); }

/* The legacy FieldOptions.ctype that PostProcessFieldFeatures moves out of the options, kept in
 * the field's `user` as ctype + 1 (0: not set). */
static int field_legacy_ctype(const LpField *f) { return (int)(intptr_t)f->user - 1; }

/* ---- the builder ---- */

typedef struct {
	int fields_to_suggest;
	bool has_reason;
} MessageHints;

typedef struct {
	LpPool *pool;
	LpFile *file;
	const char *filename;
	bool had_errors;
	Buf *log;
	LpFileSet *dependencies;        /* dependencies_ */
	LpFileSet *option_dependencies; /* option_dependencies_ */
	/* The sets hold the imports; the files they reach through public imports are added when
	 * find_symbol first needs them (upstream records them all before; only find_symbol reads the
	 * sets, and a chain of n files that publicly import the previous one takes n^2 / 2 steps). */
	bool public_dependencies_recorded;
	LpFile *possible_undeclared_dependency;
	const char *possible_undeclared_dependency_name;
	const char *undefine_resolved_name;
	int recursion_depth;
	Map message_hints;       /* message id -> MessageHints* */
	Map by_parent;           /* "<parent id>:<name>" -> LpSymbol* (symbols_by_parent_) */
	Map fields_by_number;    /* "<containing id>:<number>" -> LpField* */
	LpFeatureResolver resolver;
	bool has_resolver;
	Vec lifetimes;           /* LifetimesInfo* for the deferred validation */
} Builder;

typedef struct {
	const PbMsg *option;
	const char *full_name;
} LifetimesInfo;

static void add_error(Builder *b, const char *element, const char *error)
{
	const LpVersion *v = b->pool->version;
	if (!b->had_errors)
		buf_fmt(b->log, "%sInvalid proto descriptor for file \"%s\":\n", v && v->log_file_prefix ? v->log_file_prefix : "",
			b->filename);
	buf_fmt(b->log, "%s  %s: %s\n", v && v->log_error_prefix ? v->log_error_prefix : "", element, error);
	b->had_errors = true;
}

/* AddWarning: logged with the version's prefix (LpVersion.log_warning_prefix) */
static void add_warning(Builder *b, const char *element, const char *warning)
{
	const LpVersion *v = b->pool->version;
	if (v && v->log_warning_prefix)
		buf_fmt(b->log, "%s%s %s: %s\n", v->log_warning_prefix, b->filename, element, warning);
}

/* AddNotDefinedError */
static void add_not_defined_error(Builder *b, const char *element, const char *undefined_symbol)
{
	if (!b->possible_undeclared_dependency && !*b->undefine_resolved_name) {
		add_error(b, element, xcat("\"", undefined_symbol, "\" is not defined."));
		return;
	}
	if (b->possible_undeclared_dependency)
		add_error(b, element,
			  xcat("\"", b->possible_undeclared_dependency_name, "\" seems to be defined in \"",
			       b->possible_undeclared_dependency->name, "\", which is not imported by \"", b->filename,
			       "\".  To use it here, please add the necessary import."));
	if (*b->undefine_resolved_name)
		add_error(b, element,
			  xcat("\"", undefined_symbol, "\" is resolved to \"", b->undefine_resolved_name,
			       "\", which is not defined. The innermost scope is searched first in name resolution. "
			       "Consider using a leading '.'(i.e., \".",
			       undefined_symbol, "\") to start from the outermost scope."));
}

static int parent_id(Builder *b, const void *parent_msg)
{
	return parent_msg ? ((const LpMessage *)parent_msg)->id : b->file->id;
}

typedef struct {
	LpFile *file;
	unsigned stamp;
} LpFileMark;

static void file_set_clear(LpFileSet *set)
{
	set->stamp++;
	set->files.n = 0;
}

/* The file of the set with this name, or NULL. */
static LpFile *file_set_get(const LpFileSet *set, const char *name)
{
	LpFileMark *mark = map_get(&set->marks, name);
	return mark && mark->stamp == set->stamp ? mark->file : NULL;
}

static void file_set_add(LpFileSet *set, LpFile *file)
{
	LpFileMark *mark = map_get(&set->marks, file->name);
	if (!mark) {
		mark = xalloc(sizeof *mark);
		map_put(&set->marks, file->name, mark);
	}
	mark->file = file;
	mark->stamp = set->stamp;
	vec_push(&set->files, file);
}

/* Upstream's sets hold pointers; these are keyed by name and leave out the placeholder files
 * (imports not in the pool), which have no public dependencies and no symbols: a placeholder
 * would hide the file of the pool with its name, loaded after it. */
static void record_dependency(LpFileSet *set, LpFile *file)
{
	if (file && !file_set_get(set, file->name) && lp_find_file(lp_file_pool(file), file->name) == file)
		file_set_add(set, file);
}

/* RecordPublicDependencies of the files of the set, iterative: files grows with the files reached
 * through public imports (the order of the set does not show). */
static void record_public_dependencies(LpFileSet *set)
{
	for (int i = 0; i < set->files.n; i++) {
		LpFile *f = VEC_AT(set->files, LpFile, i);
		for (int j = 0; j < f->n_public_dependencies; j++)
			record_dependency(set, f->dependencies[f->public_dependencies[j]]);
	}
}

/* Records the public dependencies of the imports once; true if it did now. */
static bool record_all_public_dependencies(Builder *b)
{
	if (b->public_dependencies_recorded)
		return false;
	b->public_dependencies_recorded = true;
	record_public_dependencies(b->dependencies);
	record_public_dependencies(b->option_dependencies);
	return true;
}

static bool in_dependencies(Builder *b, const LpFile *file, int kind)
{
	return file_set_get(b->dependencies, file->name) == file ||
	       (file_set_get(b->option_dependencies, file->name) == file && kind == PROTO_FIELD);
}

/* FindSymbol (with dependency enforcement) */
static LpSymbol *find_symbol(Builder *b, const char *name)
{
	LpSymbol *result = lp_find_symbol(b->pool, name);
	if (!result)
		return NULL;
	LpFile *file = result->file;
	if (file == b->file || in_dependencies(b, file, result->kind))
		return result;
	if (record_all_public_dependencies(b) && in_dependencies(b, file, result->kind))
		return result;
	if (proto_symbol_is_package(result)) {
		if (proto_file_in_package(b->file, name))
			return result;
		for (int i = 0; i < b->dependencies->files.n; i++)
			if (proto_file_in_package(VEC_AT(b->dependencies->files, LpFile, i), name))
				return result;
		for (int i = 0; i < b->option_dependencies->files.n; i++)
			if (proto_file_in_package(VEC_AT(b->option_dependencies->files, LpFile, i), name))
				return result;
	}
	b->possible_undeclared_dependency = file;
	b->possible_undeclared_dependency_name = xstrdup(name);
	return NULL;
}

enum { LOOKUP_ALL, LOOKUP_TYPES };

/* LookupSymbolNoPlaceholder (the pool does not allow unknown dependencies) */
static LpSymbol *lookup_symbol(Builder *b, const char *name, const char *relative_to, int resolve_mode)
{
	b->possible_undeclared_dependency = NULL;
	b->undefine_resolved_name = "";
	if (name[0] == '.')
		return find_symbol(b, name + 1);
	const char *dot = strchr(name, '.');
	size_t first_len = dot ? (size_t)(dot - name) : strlen(name);
	char *first_part = xstrndup(name, first_len);
	Buf scope = {0};
	buf_adds(&scope, relative_to);
	for (;;) {
		char *s = (char *)buf_str(&scope);
		char *last_dot = strrchr(s, '.');
		if (!last_dot)
			return find_symbol(b, name);
		scope.n = (size_t)(last_dot - s);
		size_t old_size = scope.n;
		buf_addc(&scope, '.');
		buf_adds(&scope, first_part);
		LpSymbol *result = find_symbol(b, buf_str(&scope));
		if (result) {
			if (first_len < strlen(name)) {
				if (proto_symbol_is_aggregate(result)) {
					buf_adds(&scope, name + first_len);
					result = find_symbol(b, buf_str(&scope));
					if (!result)
						b->undefine_resolved_name = xstrdup(buf_str(&scope));
					return result;
				}
			} else if (resolve_mode != LOOKUP_TYPES || proto_symbol_is_type(result)) {
				return result;
			}
		}
		scope.n = old_size;
		buf_str(&scope);
		scope.p[scope.n] = 0;
	}
}

static LpFile *sym_get_file(const LpSymbol *s) { return s ? s->file : NULL; }

/* AddSymbol */
static bool add_symbol(Builder *b, const char *full_name, bool name_has_nul, const void *parent, const char *name,
		       LpSymbol *symbol)
{
	if (name_has_nul) {
		add_error(b, full_name, xcat("\"", full_name, "\" contains null character."));
		return false;
	}
	if (!map_has(&b->pool->base.symbols, full_name)) {
		map_put(&b->pool->base.symbols, full_name, symbol);
		char *key = xfmt("%d:%s", parent_id(b, parent), name);
		if (map_has(&b->by_parent, key))
			return false;
		map_put(&b->by_parent, key, symbol);
		return true;
	}
	LpFile *other_file = sym_get_file(lp_find_symbol(b->pool, full_name));
	if (other_file == b->file) {
		const char *dot = strrchr(full_name, '.');
		if (!dot)
			add_error(b, full_name, xcat("\"", full_name, "\" is already defined."));
		else
			add_error(b, full_name,
				  xcat("\"", dot + 1, "\" is already defined in \"",
				       xstrndup(full_name, (size_t)(dot - full_name)), "\"."));
	} else {
		add_error(b, full_name,
			  xcat("\"", full_name, "\" is already defined in file \"",
			       other_file ? other_file->name : "null", "\"."));
	}
	return false;
}

/* ValidateSymbolName */
static void validate_symbol_name(Builder *b, const char *name, const char *full_name)
{
	if (!*name) {
		add_error(b, full_name, "Missing name.");
		return;
	}
	for (const char *p = name; *p; p++) {
		char c = *p;
		if ((c < 'a' || 'z' < c) && (c < 'A' || 'Z' < c) && (c < '0' || '9' < c) && c != '_') {
			add_error(b, full_name, xcat("\"", name, "\" is not a valid identifier."));
			return;
		}
	}
}

/* Without the package limits of later versions (legacy_descriptor_builder), the most bytes the
 * prefixes of a package may take (see BuildFileImpl). */
enum { LP_MAX_PACKAGE_PREFIX_BYTES = 1 << 20 };

/* AddPackage. Upstream recurses once per '.' of the name, from the full name to its first
 * component: it adds the symbols on the way down, stops at the first prefix that exists (an
 * error if it is not a package), and validates the last component of each added name on the way
 * back up. This loop does the same in the same order without the recursion (3.19.1 has no limit on
 * the depth of a package). */
static void add_package(Builder *b, const char *name, bool has_nul, LpFile *file, bool toplevel)
{
	if (has_nul) {
		add_error(b, name, xcat("\"", name, "\" contains null character."));
		return;
	}
	size_t len = strlen(name), depth = 1;
	for (size_t i = 0; i < len; i++)
		depth += name[i] == '.';
	const char **added = xalloc(sizeof *added * depth);
	size_t *last_dot = xalloc(sizeof *last_dot * depth); /* len of the parent, or SIZE_MAX */
	size_t n = 0;
	const char *cur = name;
	for (;;) {
		LpSymbol *existing = lp_find_symbol(b->pool, cur);
		if (existing) {
			if (!proto_symbol_is_package(existing)) {
				LpFile *other_file = existing->file;
				add_error(b, cur,
					  xcat("\"", cur, "\" is already defined (as something other than a package) in file \"",
					       other_file ? other_file->name : "null", "\"."));
			}
			break;
		}
		if (toplevel && n == 0)
			map_put(&b->pool->base.symbols, file->package, proto_symbol_new(PROTO_SYM_PACKAGE, file, file, file->package));
		else
			map_put(&b->pool->base.symbols, cur, proto_symbol_new(PROTO_SYM_SUB_PACKAGE, NULL, file, cur));
		size_t dot = len;
		while (dot > 0 && name[dot - 1] != '.')
			dot--;
		added[n] = cur;
		last_dot[n++] = dot ? dot - 1 : SIZE_MAX;
		if (!dot)
			break;
		len = dot - 1;
		cur = xstrndup(name, len);
	}
	while (n--)
		validate_symbol_name(b, last_dot[n] == SIZE_MAX ? added[n] : added[n] + last_dot[n] + 1, added[n]);
}

static MessageHints *message_hints(Builder *b, const LpMessage *m)
{
	char *key = xfmt("%d", m ? m->id : -1);
	MessageHints *h = map_get(&b->message_hints, key);
	if (!h) {
		h = xalloc(sizeof *h);
		map_put(&b->message_hints, key, h);
	}
	return h;
}

static int fit_number(int64_t v) { return v < 0 ? 0 : v > 536870911 ? 536870911 : (int)v; }

/* MessageHints::RequestHintOnFieldNumbers */
static void request_hint(Builder *b, const LpMessage *m, int64_t range_start, int64_t range_end)
{
	MessageHints *h = message_hints(b, m);
	h->fields_to_suggest = fit_number((int64_t)h->fields_to_suggest +
					  fit_number((int64_t)fit_number(range_end) - fit_number(range_start)));
	h->has_reason = true;
}

/* AllocateNameStrings: scope.name, or "unknown" with an error if a name exceeds 65535 bytes. */
static void allocate_names(Builder *b, const char *scope, const char *name, const char **out_name,
			   const char **out_full)
{
	const char *full = *scope ? xcat(scope, ".", name) : name;
	if (strlen(name) > 65535 || strlen(full) > 65535) {
		add_error(b, full, "Name too long.");
		*out_name = *out_full = "unknown";
		return;
	}
	*out_name = name;
	*out_full = full;
}

/* AllocateOptions: a copy of the options (NULL: the default instance). */
static PbMsg *allocate_options(const PbMsg *proto, uint32_t options_field)
{
	if (!pb_has(proto, options_field))
		return NULL;
	return proto_msg_clone(pb_msg(proto, options_field));
}

/* ---- BuildFile ---- */

static void build_message(Builder *b, PbMsg *proto, LpMessage *parent, LpMessage *result, int index);
static void build_enum(Builder *b, PbMsg *proto, LpMessage *parent, LpEnum *result, int index);
static void build_field_or_extension(Builder *b, PbMsg *proto, LpMessage *parent, LpField *result, bool is_extension,
				     int index);

static void build_extension_range(Builder *b, PbMsg *proto, LpMessage *parent, LpExtensionRange *result, int index)
{
	result->kind = PROTO_EXTENSION_RANGE;
	result->start = (int)pb_int(proto, ER_START, 0);
	result->end = (int)pb_int(proto, ER_END, 0);
	result->containing_type = parent;
	result->index = index;
	result->proto = proto;
	if (result->start <= 0) {
		request_hint(b, parent, result->start, result->end);
		add_error(b, parent->full_name, "Extension numbers must be positive integers.");
	}
	if (result->start >= result->end)
		add_error(b, parent->full_name, "Extension range end number must be greater than start number.");
	result->options = allocate_options(proto, ER_OPTIONS);
}

static void build_reserved_range(Builder *b, PbMsg *proto, LpMessage *parent, LpReservedRange *result)
{
	result->start = (int)pb_int(proto, 1, 0);
	result->end = (int)pb_int(proto, 2, 0);
	if (result->start <= 0) {
		request_hint(b, parent, result->start, result->end);
		add_error(b, parent->full_name, "Reserved numbers must be positive integers.");
	}
	if (result->start >= result->end)
		add_error(b, parent->full_name, "Reserved range end number must be greater than start number.");
}

static void build_oneof(Builder *b, PbMsg *proto, LpMessage *parent, LpOneof *result, int index)
{
	result->kind = PROTO_ONEOF;
	allocate_names(b, parent->full_name, pb_str(proto, ODP_NAME), &result->name, &result->full_name);
	validate_symbol_name(b, pb_str(proto, ODP_NAME), result->full_name);
	result->containing_type = parent;
	result->index = index;
	result->proto = proto;
	result->options = allocate_options(proto, ODP_OPTIONS);
	add_symbol(b, result->full_name, str_has_nul(pb_bytes(proto, ODP_NAME)), parent, result->name,
		   proto_symbol_new(PROTO_ONEOF, result, b->file, result->full_name));
}

static void build_message(Builder *b, PbMsg *proto, LpMessage *parent, LpMessage *result, int index);

static void build_nested_messages(Builder *b, PbMsg *proto, LpMessage *result)
{
	result->n_nested = pb_count(proto, DP_NESTED_TYPE);
	result->nested = xalloc(sizeof(LpMessage) * (size_t)(result->n_nested + 1));
	for (int i = 0; i < result->n_nested; i++)
		build_message(b, pb_msg_at(proto, DP_NESTED_TYPE, i), result, &result->nested[i], i);
}

static void build_message(Builder *b, PbMsg *proto, LpMessage *parent, LpMessage *result, int index)
{
	const char *scope = parent ? parent->full_name : b->file->package;
	const char *pname = pb_str(proto, DP_NAME);
	result->kind = PROTO_MESSAGE;
	allocate_names(b, scope, pname, &result->name, &result->full_name);
	validate_symbol_name(b, pname, result->full_name);
	result->file = b->file;
	result->containing_type = parent;
	result->index = index;
	result->proto = proto;
	result->visibility = (int)pb_int(proto, DP_VISIBILITY, 0);
	result->id = b->pool->base.next_id++;

	result->n_oneofs = pb_count(proto, DP_ONEOF_DECL);
	result->oneofs = xalloc(sizeof(LpOneof) * (size_t)(result->n_oneofs + 1));
	for (int i = 0; i < result->n_oneofs; i++)
		build_oneof(b, pb_msg_at(proto, DP_ONEOF_DECL, i), result, &result->oneofs[i], i);
	result->n_fields = pb_count(proto, DP_FIELD);
	result->fields = xalloc(sizeof(LpField) * (size_t)(result->n_fields + 1));
	for (int i = 0; i < result->n_fields; i++)
		build_field_or_extension(b, pb_msg_at(proto, DP_FIELD, i), result, &result->fields[i], false, i);
	bool legacy = pool_legacy_builder(b->pool);
	if (legacy) /* nested types right after the fields, without a recursion limit */
		build_nested_messages(b, proto, result);
	result->n_enums = pb_count(proto, DP_ENUM_TYPE);
	result->enums = xalloc(sizeof(LpEnum) * (size_t)(result->n_enums + 1));
	for (int i = 0; i < result->n_enums; i++)
		build_enum(b, pb_msg_at(proto, DP_ENUM_TYPE, i), result, &result->enums[i], i);
	result->n_extension_ranges = pb_count(proto, DP_EXTENSION_RANGE);
	result->extension_ranges = xalloc(sizeof(LpExtensionRange) * (size_t)(result->n_extension_ranges + 1));
	for (int i = 0; i < result->n_extension_ranges; i++)
		build_extension_range(b, pb_msg_at(proto, DP_EXTENSION_RANGE, i), result, &result->extension_ranges[i], i);
	result->n_extensions = pb_count(proto, DP_EXTENSION);
	result->extensions = xalloc(sizeof(LpField) * (size_t)(result->n_extensions + 1));
	for (int i = 0; i < result->n_extensions; i++)
		build_field_or_extension(b, pb_msg_at(proto, DP_EXTENSION, i), result, &result->extensions[i], true, i);
	result->n_reserved_ranges = pb_count(proto, DP_RESERVED_RANGE);
	result->reserved_ranges = xalloc(sizeof(LpReservedRange) * (size_t)(result->n_reserved_ranges + 1));
	for (int i = 0; i < result->n_reserved_ranges; i++)
		build_reserved_range(b, pb_msg_at(proto, DP_RESERVED_RANGE, i), result, &result->reserved_ranges[i]);

	result->options = allocate_options(proto, DP_OPTIONS);

	/* Before building submessages, check recursion limit. */
	if (!legacy && --b->recursion_depth <= 0) {
		add_error(b, result->full_name, "Reached maximum recursion limit for nested messages.");
		result->nested = NULL;
		result->n_nested = 0;
		b->recursion_depth++;
		return;
	}
	if (!legacy) {
		build_nested_messages(b, proto, result);
		b->recursion_depth++;
	}

	result->n_reserved_names = pb_count(proto, DP_RESERVED_NAME);
	result->reserved_names = xalloc(sizeof(char *) * (size_t)(result->n_reserved_names + 1));
	for (int i = 0; i < result->n_reserved_names; i++) {
		if (pb_bytes_at(proto, DP_RESERVED_NAME, i).n > 65535)
			add_error(b, result->full_name, "Reserved name too long.");
		result->reserved_names[i] = pb_str_at(proto, DP_RESERVED_NAME, i);
	}

	add_symbol(b, result->full_name, str_has_nul(pb_bytes(proto, DP_NAME)) || (!parent && str_has_nul(pb_bytes(b->file->proto, FDP_PACKAGE))),
		   parent, result->name, proto_symbol_new(PROTO_MESSAGE, result, b->file, result->full_name));

	for (int i = 0; i < result->n_reserved_ranges; i++) {
		PbMsg *r1 = pb_msg_at(proto, DP_RESERVED_RANGE, i);
		int64_t s1 = pb_int(r1, 1, 0), e1 = pb_int(r1, 2, 0);
		for (int j = i + 1; j < result->n_reserved_ranges; j++) {
			PbMsg *r2 = pb_msg_at(proto, DP_RESERVED_RANGE, j);
			int64_t s2 = pb_int(r2, 1, 0), e2 = pb_int(r2, 2, 0);
			if (e1 > s2 && e2 > s1)
				add_error(b, result->full_name,
					  xfmt("Reserved range %lld to %lld overlaps with already-defined range %lld to %lld.",
					       (long long)s2, (long long)(int32_t)(e2 - 1), (long long)s1,
					       (long long)(int32_t)(e1 - 1)));
		}
	}

	Map reserved_name_set = {0};
	for (int i = 0; i < result->n_reserved_names; i++) {
		const char *name = result->reserved_names[i];
		if (map_has(&reserved_name_set, name))
			add_error(b, name, xcat("Field name \"", name, "\" is reserved multiple times."));
		else
			map_put(&reserved_name_set, name, (void *)name);
	}

	if (result->n_fields > 65535)
		add_error(b, result->full_name,
			  xfmt("%d fields in %s exceeds the limit of 65535", result->n_fields, result->full_name));

	for (int i = 0; i < result->n_fields; i++) {
		const LpField *field = &result->fields[i];
		for (int j = 0; j < result->n_extension_ranges; j++) {
			const LpExtensionRange *range = &result->extension_ranges[j];
			if (range->start <= field->number && field->number < range->end) {
				request_hint(b, result, 0, 1);
				add_error(b, field->full_name,
					  xfmt("Extension range %d to %d includes field \"%s\" (%d).", range->start,
					       (int)(int32_t)((uint32_t)range->end - 1), field->name, field->number));
			}
		}
		for (int j = 0; j < result->n_reserved_ranges; j++) {
			const LpReservedRange *range = &result->reserved_ranges[j];
			if (range->start <= field->number && field->number < range->end) {
				request_hint(b, result, 0, 1);
				add_error(b, field->full_name,
					  xfmt("Field \"%s\" uses reserved number %d.", field->name, field->number));
			}
		}
		if (map_has(&reserved_name_set, field->name))
			add_error(b, field->full_name, xcat("Field name \"", field->name, "\" is reserved."));
	}

	for (int i = 0; i < result->n_extension_ranges; i++) {
		const LpExtensionRange *range1 = &result->extension_ranges[i];
		for (int j = 0; j < result->n_reserved_ranges; j++) {
			const LpReservedRange *range2 = &result->reserved_ranges[j];
			if (range1->end > range2->start && range2->end > range1->start)
				add_error(b, result->full_name,
					  xfmt("Extension range %d to %d overlaps with reserved range %d to %d.", range1->start,
					       (int)(int32_t)((uint32_t)range1->end - 1), range2->start,
					       (int)(int32_t)((uint32_t)range2->end - 1)));
		}
		for (int j = i + 1; j < result->n_extension_ranges; j++) {
			const LpExtensionRange *range2 = &result->extension_ranges[j];
			if (range1->end > range2->start && range2->end > range1->start)
				add_error(b, result->full_name,
					  xfmt("Extension range %d to %d overlaps with already-defined range %d to %d.",
					       range2->start, (int)(int32_t)((uint32_t)range2->end - 1), range1->start,
					       (int)(int32_t)((uint32_t)range1->end - 1)));
		}
	}
}

/* strtol & co. with base 0 on the NUL-terminated default value: returns the end. */
static const char *parse_default_int(const char *s, int cpp_type, LpField *result)
{
	char *end = NULL;
	switch (cpp_type) {
	case LP_CPPTYPE_INT32: result->default_value.i = (int32_t)strtoll(s, &end, 0); break;
	case LP_CPPTYPE_INT64: result->default_value.i = strtoll(s, &end, 0); break;
	case LP_CPPTYPE_UINT32: result->default_value.u = (uint32_t)strtoull(s, &end, 0); break;
	case LP_CPPTYPE_UINT64: result->default_value.u = strtoull(s, &end, 0); break;
	}
	return end;
}

static void build_field_or_extension(Builder *b, PbMsg *proto, LpMessage *parent, LpField *result, bool is_extension,
				     int index)
{
	const char *scope = parent ? parent->full_name : b->file->package;
	const char *pname = pb_str(proto, FD_NAME);
	const char *full = *scope ? xcat(scope, ".", pname) : pname;
	result->kind = PROTO_FIELD;
	/* camelcase_name and lowercase_name are not kept (no generator reads them); their lengths are
	 * checked like upstream (lowercase_name has the length of name) */
	char *camel = proto_to_camel_case(pname, true);
	const char *json = pb_has(proto, FD_JSON_NAME) ? pb_str(proto, FD_JSON_NAME) : proto_json_name(pname);
	if (strlen(pname) > 65535 || strlen(full) > 65535 || strlen(camel) > 65535 || strlen(json) > 65535) {
		add_error(b, full, "Name too long.");
		result->name = result->full_name = result->json_name = "unknown";
	} else {
		result->name = pname;
		result->full_name = full;
		result->json_name = json;
	}
	validate_symbol_name(b, pname, result->full_name);

	result->file = b->file;
	result->index = index;
	result->proto = proto;
	result->number = (int)pb_int(proto, FD_NUMBER, 0);
	result->is_extension = is_extension;
	result->proto3_optional = pb_int(proto, FD_PROTO3_OPTIONAL, 0) != 0;

	if (result->proto3_optional && b->file->edition != LP_EDITION_PROTO3)
		add_error(b, result->full_name,
			  xcat("The [proto3_optional=true] option may only be set on proto3fields, not ", result->full_name));

	result->has_json_name = pb_has(proto, FD_JSON_NAME);
	result->type = (int)pb_int(proto, FD_TYPE, PT_DOUBLE);
	result->label = (int)pb_int(proto, FD_LABEL, LABEL_OPTIONAL);

	if (result->label == LABEL_REQUIRED && result->is_extension)
		add_error(b, result->full_name, xcat("The extension ", result->full_name, " cannot be required."));

	result->has_default_value = pb_has(proto, FD_DEFAULT_VALUE);
	if (result->has_default_value && proto_field_is_repeated(result))
		add_error(b, result->full_name, "Repeated fields can't have default values.");

	if (pb_has(proto, FD_TYPE)) {
		const char *dv = pb_str(proto, FD_DEFAULT_VALUE);
		if (result->has_default_value) {
			const char *end_pos = NULL;
			switch (cpp_type_of(result->type)) {
			case LP_CPPTYPE_INT32:
			case LP_CPPTYPE_INT64:
			case LP_CPPTYPE_UINT32:
			case LP_CPPTYPE_UINT64:
				end_pos = parse_default_int(dv, cpp_type_of(result->type), result);
				break;
			case LP_CPPTYPE_FLOAT:
				if (str_eq(dv, "inf"))
					result->default_value.f = INFINITY;
				else if (str_eq(dv, "-inf"))
					result->default_value.f = -INFINITY;
				else if (str_eq(dv, "nan"))
					result->default_value.f = NAN;
				else
					result->default_value.f = safe_double_to_float(lp_no_locale_strtod(dv, &end_pos));
				break;
			case LP_CPPTYPE_DOUBLE:
				if (str_eq(dv, "inf"))
					result->default_value.d = INFINITY;
				else if (str_eq(dv, "-inf"))
					result->default_value.d = -INFINITY;
				else if (str_eq(dv, "nan"))
					result->default_value.d = NAN;
				else
					result->default_value.d = lp_no_locale_strtod(dv, &end_pos);
				break;
			case LP_CPPTYPE_BOOL:
				if (str_eq(dv, "true"))
					result->default_value.b = true;
				else if (str_eq(dv, "false"))
					result->default_value.b = false;
				else
					add_error(b, result->full_name, "Boolean default must be true or false.");
				break;
			case LP_CPPTYPE_ENUM:
				result->default_value_enum = NULL;
				break;
			case LP_CPPTYPE_STRING:
				if (result->type == PT_BYTES) {
					if (!lp_cunescape(pb_bytes(proto, FD_DEFAULT_VALUE), &result->default_value_string))
						add_error(b, result->full_name, "Invalid escaping in default value.");
				} else {
					result->default_value_string = pb_bytes(proto, FD_DEFAULT_VALUE);
				}
				break;
			case LP_CPPTYPE_MESSAGE:
				add_error(b, result->full_name, "Messages can't have default values.");
				result->has_default_value = false;
				break;
			}
			if (end_pos != NULL && (!*dv || *end_pos != 0))
				add_error(b, result->full_name, xcat("Couldn't parse default value \"", dv, "\"."));
		} else {
			result->default_value_string = (Str){"", 0};
		}
	}

	if (result->number <= 0) {
		request_hint(b, parent, 0, 1);
		add_error(b, result->full_name, "Field numbers must be positive integers.");
	} else if (!is_extension && result->number > 536870911) {
		request_hint(b, parent, 0, 1);
		add_error(b, result->full_name, "Field numbers cannot be greater than 536870911.");
	}

	if (is_extension) {
		if (!pb_has(proto, FD_EXTENDEE))
			add_error(b, result->full_name, "FieldDescriptorProto.extendee not set for extension field.");
		result->extension_scope = parent;
		if (pb_has(proto, FD_ONEOF_INDEX))
			add_error(b, result->full_name, "FieldDescriptorProto.oneof_index should not be set for extensions.");
	} else {
		if (pb_has(proto, FD_EXTENDEE))
			add_error(b, result->full_name, "FieldDescriptorProto.extendee set for non-extension field.");
		result->containing_type = parent;
		if (pb_has(proto, FD_ONEOF_INDEX)) {
			int64_t oi = pb_int(proto, FD_ONEOF_INDEX, 0);
			if (oi < 0 || oi >= parent->n_oneofs) {
				add_error(b, result->full_name,
					  xfmt("FieldDescriptorProto.oneof_index %d is out of range for type \"%s\".", (int)oi,
					       parent->name));
			} else {
				result->containing_oneof = &parent->oneofs[oi];
			}
		}
	}

	result->options = allocate_options(proto, FD_OPTIONS);
	bool has_nul = str_has_nul(pb_bytes(proto, FD_NAME)) ||
		       (!parent && str_has_nul(pb_bytes(b->file->proto, FDP_PACKAGE)));
	add_symbol(b, result->full_name, has_nul, parent, result->name,
		   proto_symbol_new(PROTO_FIELD, result, b->file, result->full_name));
}

static void build_enum_value(Builder *b, PbMsg *proto, LpEnum *parent, LpEnumValue *result, int index)
{
	const char *pname = pb_str(proto, EVDP_NAME);
	result->kind = PROTO_ENUM_VALUE;
	size_t scope_len = strlen(parent->full_name) - strlen(parent->name);
	char *full_name = xcat(xstrndup(parent->full_name, scope_len), pname);
	if (strlen(full_name) > 65535)
		add_error(b, full_name, "Name too long.");
	result->name = pname;
	result->full_name = full_name;
	result->number = (int)pb_int(proto, EVDP_NUMBER, 0);
	result->type = parent;
	result->index = index;
	result->proto = proto;
	validate_symbol_name(b, pname, result->full_name);
	result->options = allocate_options(proto, EVDP_OPTIONS);

	bool has_nul = str_has_nul(pb_bytes(proto, EVDP_NAME)) ||
		       (!parent->containing_type && str_has_nul(pb_bytes(b->file->proto, FDP_PACKAGE)));
	bool added_to_outer_scope = add_symbol(b, result->full_name, has_nul, parent->containing_type, result->name,
					       proto_symbol_new(PROTO_ENUM_VALUE, result, b->file, result->full_name));
	char *key = xfmt("%d:%s", parent->id, result->name);
	bool added_to_inner_scope = !map_has(&b->by_parent, key);
	if (added_to_inner_scope)
		map_put(&b->by_parent, key, proto_symbol_new(PROTO_ENUM_VALUE, result, b->file, result->full_name));
	if (added_to_inner_scope && !added_to_outer_scope) {
		const char *outer_scope = parent->containing_type ? parent->containing_type->full_name : b->file->package;
		outer_scope = *outer_scope ? xcat("\"", outer_scope, "\"") : "the global scope";
		add_error(b, result->full_name,
			  xcat("Note that enum values use C++ scoping rules, meaning that enum values are siblings of their "
			       "type, not children of it.  Therefore, \"",
			       result->name, "\" must be unique within ", outer_scope, ", not just within \"", parent->name,
			       "\"."));
	}
}

static void check_enum_value_uniqueness(Builder *b, const LpEnum *enm);

static void build_enum(Builder *b, PbMsg *proto, LpMessage *parent, LpEnum *result, int index)
{
	const char *scope = parent ? parent->full_name : b->file->package;
	const char *pname = pb_str(proto, EDP_NAME);
	result->kind = PROTO_ENUM;
	allocate_names(b, scope, pname, &result->name, &result->full_name);
	validate_symbol_name(b, pname, result->full_name);
	result->file = b->file;
	result->containing_type = parent;
	result->index = index;
	result->proto = proto;
	result->visibility = (int)pb_int(proto, EDP_VISIBILITY, 0);
	result->id = b->pool->base.next_id++;

	if (pb_count(proto, EDP_VALUE) == 0)
		add_error(b, result->full_name, "Enums must contain at least one value.");

	result->n_values = pb_count(proto, EDP_VALUE);
	result->values = xalloc(sizeof(LpEnumValue) * (size_t)(result->n_values + 1));
	for (int i = 0; i < result->n_values; i++)
		build_enum_value(b, pb_msg_at(proto, EDP_VALUE, i), result, &result->values[i], i);
	result->n_reserved_ranges = pb_count(proto, EDP_RESERVED_RANGE);
	result->reserved_ranges = xalloc(sizeof(LpReservedRange) * (size_t)(result->n_reserved_ranges + 1));
	for (int i = 0; i < result->n_reserved_ranges; i++) {
		PbMsg *r = pb_msg_at(proto, EDP_RESERVED_RANGE, i);
		result->reserved_ranges[i].start = (int)pb_int(r, 1, 0);
		result->reserved_ranges[i].end = (int)pb_int(r, 2, 0);
		if (result->reserved_ranges[i].start > result->reserved_ranges[i].end)
			add_error(b, result->full_name, "Reserved range end number must be greater than start number.");
	}
	result->n_reserved_names = pb_count(proto, EDP_RESERVED_NAME);
	result->reserved_names = xalloc(sizeof(char *) * (size_t)(result->n_reserved_names + 1));
	for (int i = 0; i < result->n_reserved_names; i++) {
		if (pb_bytes_at(proto, EDP_RESERVED_NAME, i).n > 65535)
			add_error(b, result->full_name, "Reserved name too long.");
		result->reserved_names[i] = pb_str_at(proto, EDP_RESERVED_NAME, i);
	}
	if (b->pool->version && b->pool->version->enum_value_name_error)
		check_enum_value_uniqueness(b, result);

	result->options = allocate_options(proto, EDP_OPTIONS);
	bool has_nul = str_has_nul(pb_bytes(proto, EDP_NAME)) ||
		       (!parent && str_has_nul(pb_bytes(b->file->proto, FDP_PACKAGE)));
	add_symbol(b, result->full_name, has_nul, parent, result->name,
		   proto_symbol_new(PROTO_ENUM, result, b->file, result->full_name));

	for (int i = 0; i < result->n_reserved_ranges; i++) {
		const LpReservedRange *r1 = &result->reserved_ranges[i];
		for (int j = i + 1; j < result->n_reserved_ranges; j++) {
			const LpReservedRange *r2 = &result->reserved_ranges[j];
			if (r1->end >= r2->start && r2->end >= r1->start)
				add_error(b, result->full_name,
					  xfmt("Reserved range %d to %d overlaps with already-defined range %d to %d.", r2->start,
					       r2->end, r1->start, r1->end));
		}
	}

	Map reserved_name_set = {0};
	for (int i = 0; i < result->n_reserved_names; i++) {
		const char *name = result->reserved_names[i];
		if (map_has(&reserved_name_set, name))
			add_error(b, name, xcat("Enum value \"", name, "\" is reserved multiple times."));
		else
			map_put(&reserved_name_set, name, (void *)name);
	}

	for (int i = 0; i < result->n_values; i++) {
		const LpEnumValue *value = &result->values[i];
		for (int j = 0; j < result->n_reserved_ranges; j++) {
			const LpReservedRange *range = &result->reserved_ranges[j];
			if (range->start <= value->number && value->number <= range->end)
				add_error(b, value->full_name,
					  xfmt("Enum value \"%s\" uses reserved number %d.", value->name, value->number));
		}
		if (map_has(&reserved_name_set, value->name))
			add_error(b, value->full_name, xcat("Enum value \"", value->name, "\" is reserved."));
	}
}

static void build_method(Builder *b, PbMsg *proto, LpService *parent, LpMethod *result, int index)
{
	result->kind = PROTO_METHOD;
	result->service = parent;
	allocate_names(b, parent->full_name, pb_str(proto, MDP_NAME), &result->name, &result->full_name);
	validate_symbol_name(b, pb_str(proto, MDP_NAME), result->full_name);
	result->index = index;
	result->proto = proto;
	result->options = allocate_options(proto, MDP_OPTIONS);
	result->client_streaming = pb_int(proto, MDP_CLIENT_STREAMING, 0) != 0;
	result->server_streaming = pb_int(proto, MDP_SERVER_STREAMING, 0) != 0;
	char *key = xfmt("%d:%s", parent->id, result->name);
	if (str_has_nul(pb_bytes(proto, MDP_NAME)) || str_has_nul(pb_bytes(parent->proto, SDP_NAME)) ||
	    str_has_nul(pb_bytes(b->file->proto, FDP_PACKAGE))) {
		add_error(b, result->full_name, xcat("\"", result->full_name, "\" contains null character."));
		return;
	}
	LpSymbol *sym = proto_symbol_new(PROTO_METHOD, result, b->file, result->full_name);
	if (!map_has(&b->pool->base.symbols, result->full_name)) {
		map_put(&b->pool->base.symbols, result->full_name, sym);
		if (!map_has(&b->by_parent, key))
			map_put(&b->by_parent, key, sym);
		return;
	}
	add_symbol(b, result->full_name, false, NULL, result->name, sym);
}

static void build_service(Builder *b, PbMsg *proto, LpService *result, int index)
{
	result->kind = PROTO_SERVICE;
	allocate_names(b, b->file->package, pb_str(proto, SDP_NAME), &result->name, &result->full_name);
	result->file = b->file;
	result->index = index;
	result->proto = proto;
	result->id = b->pool->base.next_id++;
	validate_symbol_name(b, pb_str(proto, SDP_NAME), result->full_name);
	result->n_methods = pb_count(proto, SDP_METHOD);
	result->methods = xalloc(sizeof(LpMethod) * (size_t)(result->n_methods + 1));
	for (int i = 0; i < result->n_methods; i++)
		build_method(b, pb_msg_at(proto, SDP_METHOD, i), result, &result->methods[i], i);
	result->options = allocate_options(proto, SDP_OPTIONS);
	bool has_nul = str_has_nul(pb_bytes(proto, SDP_NAME)) || str_has_nul(pb_bytes(b->file->proto, FDP_PACKAGE));
	add_symbol(b, result->full_name, has_nul, NULL, result->name,
		   proto_symbol_new(PROTO_SERVICE, result, b->file, result->full_name));
}

/* ---- cross-linking ---- */

static int cmp_range64(const void *a, const void *b)
{
	const int64_t *x = a, *y = b;
	return x[0] != y[0] ? (x[0] < y[0] ? -1 : 1) : (x[1] < y[1] ? -1 : x[1] > y[1]);
}

static int cmp_range(const void *a, const void *b)
{
	const int *x = a, *y = b;
	return x[0] != y[0] ? (x[0] < y[0] ? -1 : 1) : (x[1] < y[1] ? -1 : x[1] > y[1]);
}

static void cross_link_field(Builder *b, LpField *field, PbMsg *proto)
{
	if (pb_has(proto, FD_EXTENDEE) && field->is_extension) {
		const char *extendee_name = pb_str(proto, FD_EXTENDEE);
		LpSymbol *extendee = lookup_symbol(b, extendee_name, field->full_name, LOOKUP_ALL);
		if (!extendee) {
			add_not_defined_error(b, field->full_name, extendee_name);
			return;
		} else if (extendee->kind != PROTO_MESSAGE) {
			add_error(b, field->full_name, xcat("\"", extendee_name, "\" is not a message type."));
			return;
		}
		field->containing_type = extendee->desc;
		if (!proto_message_extension_range(field->containing_type, field->number))
			add_error(b, field->full_name,
				  xfmt("\"%s\" does not declare %d as an extension number.", field->containing_type->full_name,
				       field->number));
	}

	if (field->containing_oneof && field->label != LABEL_OPTIONAL)
		add_error(b, field->full_name, "Fields of oneofs must themselves have label LABEL_OPTIONAL.");

	if (pb_has(proto, FD_TYPE_NAME)) {
		const char *type_name = pb_str(proto, FD_TYPE_NAME);
		bool is_weak = pb_int(pb_msg(proto, FD_OPTIONS), FOPT_WEAK, 0) != 0;
		LpSymbol *type = lookup_symbol(b, type_name, field->full_name, LOOKUP_TYPES);
		if (!type) {
			if (is_weak)
				type = find_symbol(b, "google.protobuf.Empty");
			if (!type) {
				add_not_defined_error(b, field->full_name, type_name);
				return;
			}
		}
		if (!pb_has(proto, FD_TYPE)) {
			if (type->kind == PROTO_MESSAGE)
				field->type = PT_MESSAGE;
			else if (type->kind == PROTO_ENUM)
				field->type = PT_ENUM;
			else {
				add_error(b, field->full_name, xcat("\"", type_name, "\" is not a type."));
				return;
			}
		}
		int ct = cpp_type_of(field->type);
		if (ct == LP_CPPTYPE_MESSAGE) {
			if (type->kind != PROTO_MESSAGE) {
				add_error(b, field->full_name, xcat("\"", type_name, "\" is not a message type."));
				return;
			}
			field->message_type = type->desc;
			if (field->has_default_value)
				add_error(b, field->full_name, "Messages can't have default values.");
		} else if (ct == LP_CPPTYPE_ENUM) {
			if (type->kind != PROTO_ENUM) {
				add_error(b, field->full_name, xcat("\"", type_name, "\" is not an enum type."));
				return;
			}
			field->enum_type = type->desc;
			if (field->has_default_value) {
				const char *dv = pb_str(proto, FD_DEFAULT_VALUE);
				if (!is_identifier(dv)) {
					add_error(b, field->full_name, "Default value for an enum field must be an identifier.");
				} else {
					LpSymbol *s = map_get(&b->by_parent, xfmt("%d:%s", field->enum_type->id, dv));
					if (!s && field->enum_type->file != b->file) {
						/* the enum's own file tables */
						for (int i = 0; i < field->enum_type->n_values; i++)
							if (str_eq(field->enum_type->values[i].name, dv)) {
								field->default_value_enum = &field->enum_type->values[i];
								break;
							}
					} else if (s && s->kind == PROTO_ENUM_VALUE &&
						   ((LpEnumValue *)s->desc)->type == field->enum_type) {
						field->default_value_enum = s->desc;
					}
					if (!field->default_value_enum)
						add_error(b, field->full_name,
							  xcat("Enum type \"", field->enum_type->full_name, "\" has no value named \"",
							       dv, "\"."));
				}
			} else if (field->enum_type->n_values > 0) {
				field->default_value_enum = &field->enum_type->values[0];
			}
		} else {
			add_error(b, field->full_name, "Field with primitive type has type_name.");
		}
	} else {
		int ct = cpp_type_of(field->type);
		if (ct == LP_CPPTYPE_MESSAGE || ct == LP_CPPTYPE_ENUM)
			add_error(b, field->full_name, "Field with message or enum type missing type_name.");
	}

	/* fields_by_number_ of the file tables */
	char *key = xfmt("%d:%d", field->containing_type ? field->containing_type->id : -1, field->number);
	LpField *conflicting = map_get(&b->fields_by_number, key);
	if (conflicting) {
		const char *containing_type_name = field->containing_type ? field->containing_type->full_name : "unknown";
		if (field->is_extension) {
			add_error(b, field->full_name,
				  xfmt("Extension number %d has already been used in \"%s\" by extension \"%s\".",
				       field->number, containing_type_name, conflicting->full_name));
		} else if (b->pool->version && b->pool->version->legacy_field_number_error) {
			add_error(b, field->full_name,
				  xfmt("Field number %d has already been used in \"%s\" by field \"%s\".", field->number,
				       containing_type_name, conflicting->name));
		} else {
			/* the next free number after the fields, extension ranges and reserved ranges */
			const LpMessage *parent = field->containing_type;
			int n = parent->n_fields + parent->n_extension_ranges + parent->n_reserved_ranges;
			int64_t(*used)[2] = xalloc(sizeof(int64_t[2]) * (size_t)(n + 1));
			int k = 0;
			for (int i = 0; i < parent->n_fields; i++)
				used[k][0] = used[k][1] = parent->fields[i].number, k++;
			for (int i = 0; i < parent->n_extension_ranges; i++)
				used[k][0] = parent->extension_ranges[i].start,
				used[k][1] = (int64_t)parent->extension_ranges[i].end - 1, k++;
			for (int i = 0; i < parent->n_reserved_ranges; i++)
				used[k][0] = parent->reserved_ranges[i].start,
				used[k][1] = (int64_t)parent->reserved_ranges[i].end - 1, k++;
			/* btree_set: sorted, without duplicates */
			qsort(used, (size_t)k, sizeof *used, cmp_range64);
			int u = 0;
			for (int i = 0; i < k; i++)
				if (u == 0 || used[u - 1][0] != used[i][0] || used[u - 1][1] != used[i][1])
					used[u][0] = used[i][0], used[u][1] = used[i][1], u++;
			k = u;
			int64_t proposed = 1;
			for (int i = 0; i < k; i++) {
				if (used[i][0] <= proposed && proposed <= used[i][1])
					proposed = used[i][1] + 1;
				else
					break;
			}
			const char *proposed_message = proposed <= 536870911
							       ? xfmt("Next available field number is %lld", (long long)proposed)
							       : "There are no available field numbers";
			add_error(b, field->full_name,
				  xfmt("Field number %d has already been used in \"%s\" by field \"%s\". %s.", field->number,
				       containing_type_name, conflicting->name, proposed_message));
		}
	} else {
		map_put(&b->fields_by_number, key, field);
		if (field->is_extension && !proto_pool_add_extension(&b->pool->base, field)) {
			/* taken: upstream only warns */
			const LpField *other = proto_pool_find_extension(&b->pool->base, field->containing_type, field->number);
			add_warning(b, field->full_name,
				    xfmt("Extension number %d has already been used in \"%s\" by extension \"%s\" defined in "
					 "%s.",
					 field->number, field->containing_type ? field->containing_type->full_name : "unknown",
					 other ? other->full_name : "", other ? other->file->name : ""));
		}
	}
}

static void cross_link_message(Builder *b, LpMessage *message, PbMsg *proto)
{
	for (int i = 0; i < message->n_nested; i++)
		cross_link_message(b, &message->nested[i], pb_msg_at(proto, DP_NESTED_TYPE, i));
	for (int i = 0; i < message->n_fields; i++)
		cross_link_field(b, &message->fields[i], pb_msg_at(proto, DP_FIELD, i));
	for (int i = 0; i < message->n_extensions; i++)
		cross_link_field(b, &message->extensions[i], pb_msg_at(proto, DP_EXTENSION, i));

	for (int i = 0; i < message->n_fields; i++) {
		LpOneof *oneof_decl = message->fields[i].containing_oneof;
		if (!oneof_decl)
			continue;
		if (oneof_decl->n_fields > 0 && message->fields[i - 1].containing_oneof != oneof_decl)
			add_error(b, xcat(message->full_name, ".", message->fields[i - 1].name),
				  xcat("Fields in the same oneof must be defined consecutively. \"", message->fields[i - 1].name,
				       "\" cannot be defined before the completion of the \"", oneof_decl->name,
				       "\" oneof definition."));
		if (oneof_decl->n_fields == 0)
			oneof_decl->first_field = i;
		oneof_decl->n_fields++;
	}
	for (int i = 0; i < message->n_oneofs; i++)
		if (message->oneofs[i].n_fields == 0)
			add_error(b, xcat(message->full_name, ".", message->oneofs[i].name), "Oneof must have at least one field.");
	for (int i = 0; i < message->n_fields; i++) {
		const LpField *field = &message->fields[i];
		if (field->proto3_optional && (!field->containing_oneof || !proto_oneof_is_synthetic(field->containing_oneof)))
			add_error(b, message->full_name,
				  "Fields with proto3_optional set must be a member of a one-field oneof");
	}
	int first_synthetic = -1;
	for (int i = 0; i < message->n_oneofs; i++) {
		if (message->oneofs[i].n_fields > 0 && proto_oneof_is_synthetic(&message->oneofs[i])) {
			if (first_synthetic == -1)
				first_synthetic = i;
		} else if (first_synthetic != -1) {
			add_error(b, message->full_name, "Synthetic oneofs must be after all other oneofs");
		}
	}
	message->n_real_oneofs = first_synthetic == -1 ? message->n_oneofs : first_synthetic;
}

static void cross_link_method(Builder *b, LpMethod *method, PbMsg *proto)
{
	const uint32_t nums[2] = {MDP_INPUT_TYPE, MDP_OUTPUT_TYPE};
	for (int k = 0; k < 2; k++) {
		const char *name = pb_str(proto, nums[k]);
		LpSymbol *t = lookup_symbol(b, name, method->full_name, LOOKUP_ALL);
		if (!t)
			add_not_defined_error(b, method->full_name, name);
		else if (t->kind != PROTO_MESSAGE)
			add_error(b, method->full_name, xcat("\"", name, "\" is not a message type."));
		else if (k == 0)
			method->input_type = t->desc;
		else
			method->output_type = t->desc;
	}
}

static void cross_link_file(Builder *b, LpFile *file, PbMsg *proto)
{
	for (int i = 0; i < file->n_messages; i++)
		cross_link_message(b, &file->messages[i], pb_msg_at(proto, FDP_MESSAGE_TYPE, i));
	for (int i = 0; i < file->n_extensions; i++)
		cross_link_field(b, &file->extensions[i], pb_msg_at(proto, FDP_EXTENSION, i));
	for (int i = 0; i < file->n_services; i++)
		for (int j = 0; j < file->services[i].n_methods; j++)
			cross_link_method(b, &file->services[i].methods[j], pb_msg_at(pb_msg_at(proto, FDP_SERVICE, i), SDP_METHOD, j));
}

/* SuggestFieldNumbers */
static void suggest_field_numbers(Builder *b, LpFile *file)
{
	for (int mi = 0; mi < file->n_messages; mi++) {
		const LpMessage *message = &file->messages[mi];
		MessageHints *hints = map_get(&b->message_hints, xfmt("%d", message->id));
		if (!hints)
			continue;
		int fields_to_suggest = hints->fields_to_suggest < 3 ? hints->fields_to_suggest : 3;
		if (fields_to_suggest <= 0)
			continue;
		int n = message->n_fields + message->n_extensions + message->n_reserved_ranges + message->n_extension_ranges + 2;
		int(*used)[2] = xalloc(sizeof(int[2]) * (size_t)n);
		int k = 0;
#define ADD_ORDINAL(o)                                                                                     \
	do {                                                                                               \
		int ord_ = (o);                                                                            \
		if (ord_ <= 0 || ord_ > 536870911)                                                         \
			break;                                                                             \
		if (k && ord_ == used[k - 1][1])                                                           \
			used[k - 1][1] = ord_ + 1;                                                         \
		else                                                                                       \
			used[k][0] = ord_, used[k][1] = ord_ + 1, k++;                                     \
	} while (0)
#define ADD_RANGE(f, t)                                                                                    \
	do {                                                                                               \
		int from_ = (f) < 0 ? 0 : (f) > 536870912 ? 536870912 : (f);                               \
		int to_ = (t) < 0 ? 0 : (t) > 536870912 ? 536870912 : (t);                                 \
		if (from_ >= to_)                                                                          \
			break;                                                                             \
		used[k][0] = from_, used[k][1] = to_, k++;                                                 \
	} while (0)
		for (int i = 0; i < message->n_fields; i++)
			ADD_ORDINAL(message->fields[i].number);
		for (int i = 0; i < message->n_extensions; i++)
			ADD_ORDINAL(message->extensions[i].number);
		for (int i = 0; i < message->n_reserved_ranges; i++)
			ADD_RANGE(message->reserved_ranges[i].start, message->reserved_ranges[i].end);
		for (int i = 0; i < message->n_extension_ranges; i++)
			ADD_RANGE(message->extension_ranges[i].start, message->extension_ranges[i].end);
#undef ADD_ORDINAL
#undef ADD_RANGE
		used[k][0] = 536870911, used[k][1] = 536870912, k++;
		used[k][0] = 19000, used[k][1] = 19999, k++;
		qsort(used, (size_t)k, sizeof *used, cmp_range);
		int current = 1;
		if (hints->has_reason) {
			Buf id_list = {0};
			buf_fmt(&id_list, "Suggested field numbers for %s: ", message->full_name);
			const char *separator = "";
			for (int i = 0; i < k; i++) {
				while (current < used[i][0] && fields_to_suggest > 0) {
					buf_fmt(&id_list, "%s%d", separator, current++);
					separator = ", ";
					fields_to_suggest--;
				}
				if (fields_to_suggest == 0)
					break;
				if (used[i][1] > current)
					current = used[i][1];
			}
			add_error(b, message->full_name, buf_str(&id_list));
		}
	}
}

/* ---- features ---- */

/* InferLegacyProtoFeatures for fields: the legacy ctype as pb.cpp string_type, then the
 * proto2/proto3 rules of pkg/proto */
static void infer_legacy_field_features(const PbMsg *proto, const PbMsg *options, int edition, PbMsg *features)
{
	int cpp_i = proto_msg_field_index(features->def, FEATURES_PB_CPP);
	if (cpp_i >= 0) {
		PbMsg *cpp = pb_msg(features, FEATURES_PB_CPP);
		if (!pb_has(cpp, CPPF_STRING_TYPE) && pb_int(options, FOPT_CTYPE, 0) == FO_CTYPE_CORD)
			proto_msg_set_u(proto_msg_mutable(features, FEATURES_PB_CPP), CPPF_STRING_TYPE, CPP_STRING_TYPE_CORD);
	}
	proto_infer_legacy_field_features(features, proto, options, edition);
}

/* ResolveFeaturesImpl */
static void resolve_features(Builder *b, int edition, const char *name, const PbMsg *field_proto, PbMsg *options,
			     uint32_t features_field, const PbMsg *parent_features, PbMsg **proto_features,
			     PbMsg **merged_features, bool force_merge)
{
	*proto_features = NULL;
	*merged_features = b->pool->empty_features;
	if (options && pb_has(options, features_field)) {
		*proto_features = pb_msg(options, features_field);
		proto_msg_clear(options, features_field);
	}
	PbMsg *base = *proto_features ? proto_msg_clone(*proto_features) : pb_new(b->pool->feature_set_def);
	if (is_legacy_edition(edition) && *proto_features)
		add_error(b, name, "Features are only valid under editions.");
	if (field_proto)
		infer_legacy_field_features(field_proto, options, edition, base);
	if (proto_msg_is_empty(base) && !force_merge) {
		*merged_features = (PbMsg *)parent_features;
		return;
	}
	PbMsg *merged;
	const char *err = lp_merge_features(&b->resolver, parent_features, base, &merged);
	if (err) {
		add_error(b, name, err);
		return;
	}
	*merged_features = merged;
}

static void resolve_message_features(Builder *b, LpMessage *m, const PbMsg *parent);

static void resolve_enum_features(Builder *b, LpEnum *e, const PbMsg *parent)
{
	int ed = b->file->edition;
	resolve_features(b, ed, e->name, NULL, e->options, ENUMOPT_FEATURES, parent, &e->proto_features,
			 &e->merged_features, false);
	for (int i = 0; i < e->n_values; i++) {
		LpEnumValue *v = &e->values[i];
		resolve_features(b, ed, v->name, NULL, v->options, EVOPT_FEATURES, e->merged_features, &v->proto_features,
				 &v->merged_features, false);
	}
}

static void resolve_field_features(Builder *b, LpField *f)
{
	resolve_features(b, b->file->edition, f->name, f->proto, f->options, FOPT_FEATURES, proto_field_parent_features(f),
			 &f->proto_features, &f->merged_features, false);
}

static void resolve_message_features(Builder *b, LpMessage *m, const PbMsg *parent)
{
	int ed = b->file->edition;
	resolve_features(b, ed, m->name, NULL, m->options, MSGOPT_FEATURES, parent, &m->proto_features,
			 &m->merged_features, false);
	for (int i = 0; i < m->n_enums; i++)
		resolve_enum_features(b, &m->enums[i], m->merged_features);
	for (int i = 0; i < m->n_oneofs; i++) {
		LpOneof *o = &m->oneofs[i];
		resolve_features(b, ed, o->name, NULL, o->options, ONEOFOPT_FEATURES, m->merged_features,
				 &o->proto_features, &o->merged_features, false);
	}
	for (int i = 0; i < m->n_fields; i++)
		resolve_field_features(b, &m->fields[i]);
	for (int i = 0; i < m->n_nested; i++)
		resolve_message_features(b, &m->nested[i], m->merged_features);
	for (int i = 0; i < m->n_extensions; i++)
		resolve_field_features(b, &m->extensions[i]);
	for (int i = 0; i < m->n_extension_ranges; i++) {
		LpExtensionRange *r = &m->extension_ranges[i];
		resolve_features(b, ed, m->name, NULL, r->options, EROPT_FEATURES, m->merged_features, &r->proto_features,
				 &r->merged_features, false);
	}
}

static void resolve_file_features(Builder *b, LpFile *file)
{
	int ed = file->edition;
	resolve_features(b, ed, file->name, NULL, file->options, FILEOPT_FEATURES, b->pool->empty_features,
			 &file->proto_features, &file->merged_features, true);
	for (int i = 0; i < file->n_messages; i++)
		resolve_message_features(b, &file->messages[i], file->merged_features);
	for (int i = 0; i < file->n_enums; i++)
		resolve_enum_features(b, &file->enums[i], file->merged_features);
	for (int i = 0; i < file->n_extensions; i++)
		resolve_field_features(b, &file->extensions[i]);
	for (int i = 0; i < file->n_services; i++) {
		LpService *s = &file->services[i];
		resolve_features(b, ed, s->name, NULL, s->options, SVCOPT_FEATURES, file->merged_features,
				 &s->proto_features, &s->merged_features, false);
		for (int j = 0; j < s->n_methods; j++) {
			LpMethod *m = &s->methods[j];
			resolve_features(b, ed, m->name, NULL, m->options, METHOPT_FEATURES, s->merged_features,
					 &m->proto_features, &m->merged_features, false);
		}
	}
}

/* PostProcessFieldFeatures */
static void post_process_field(Builder *b, LpField *field)
{
	if (proto_feature(field->merged_features, PROTO_FEATURE_FIELD_PRESENCE) == PROTO_PRESENCE_LEGACY_REQUIRED &&
	    field->label == LABEL_OPTIONAL)
		field->label = LABEL_REQUIRED;
	if (field->type == PT_MESSAGE && !proto_message_is_map_entry(field->containing_type) &&
	    proto_feature(field->merged_features, PROTO_FEATURE_MESSAGE_ENCODING) == PROTO_MESSAGE_DELIMITED) {
		LpSymbol *type = lookup_symbol(b, pb_str(field->proto, FD_TYPE_NAME), field->full_name, LOOKUP_TYPES);
		if (!type || type->kind != PROTO_MESSAGE || !proto_message_is_map_entry(type->desc))
			field->type = PT_GROUP;
	}
	if (field->options && pb_has(field->options, FOPT_CTYPE)) {
		field->user = (void *)(intptr_t)(pb_int(field->options, FOPT_CTYPE, 0) + 1);
		proto_msg_clear(field->options, FOPT_CTYPE);
	}
}

static void post_process_message(Builder *b, LpMessage *m)
{
	for (int i = 0; i < m->n_fields; i++)
		post_process_field(b, &m->fields[i]);
	for (int i = 0; i < m->n_nested; i++)
		post_process_message(b, &m->nested[i]);
	for (int i = 0; i < m->n_extensions; i++)
		post_process_field(b, &m->extensions[i]);
}

/* ---- ValidateOptions ---- */

static bool is_lite(const LpFile *file) { return file && pb_int(file->options, FILEOPT_OPTIMIZE_FOR, 1) == 3; }

static bool is_custom_option_extension(const LpField *f)
{
	static const char *const names[] = {"FileOptions", "MessageOptions", "FieldOptions", "EnumOptions",
					    "EnumValueOptions", "ServiceOptions", "MethodOptions", "OneofOptions",
					    "ExtensionRangeOptions"};
	if (!f->is_extension || !f->containing_type)
		return false;
	for (int i = 0; i < 9; i++)
		if (str_eq(f->containing_type->full_name, xcat("google.protobuf.", names[i])) ||
		    str_eq(f->containing_type->full_name, xcat("proto2.", names[i])))
			return true;
	return false;
}

static void validate_proto3_field(Builder *b, const LpField *field)
{
	if (field->is_extension && !is_custom_option_extension(field))
		add_error(b, field->full_name, "Extensions in proto3 are only allowed for defining options.");
	if (proto_field_is_required(field))
		add_error(b, field->full_name, "Required fields are not allowed in proto3.");
	if (field->has_default_value)
		add_error(b, field->full_name, "Explicit default values are not allowed in proto3.");
	if (cpp_type_of(field->type) == LP_CPPTYPE_ENUM && field->enum_type && proto_enum_is_closed(field->enum_type))
		add_error(b, field->full_name,
			  xcat("Enum type \"", field->enum_type->full_name, "\" is not an open enum, but is used in \"",
			       field->containing_type->full_name, "\" which is a proto3 message type."));
	if (field->type == PT_GROUP)
		add_error(b, field->full_name, "Groups are not supported in proto3 syntax.");
}

static void validate_proto3_message(Builder *b, const LpMessage *message)
{
	for (int i = 0; i < message->n_nested; i++)
		validate_proto3_message(b, &message->nested[i]);
	for (int i = 0; i < message->n_fields; i++)
		validate_proto3_field(b, &message->fields[i]);
	for (int i = 0; i < message->n_extensions; i++)
		validate_proto3_field(b, &message->extensions[i]);
	if (message->n_extension_ranges > 0)
		add_error(b, message->full_name, "Extension ranges are not allowed in proto3.");
	if (pb_int(message->options, MSGOPT_MESSAGE_SET_WIRE_FORMAT, 0))
		add_error(b, message->full_name, "MessageSet is not supported in proto3.");
}

static void validate_file_options(Builder *b, const LpFile *file)
{
	/* ValidateFileFeatures */
	if (!is_legacy_edition(file->edition)) {
		if (proto_feature(file->merged_features, PROTO_FEATURE_FIELD_PRESENCE) == PROTO_PRESENCE_LEGACY_REQUIRED)
			add_error(b, file->name, "Required presence can't be specified by default.");
		if (pb_int(file->options, FILEOPT_JAVA_STRING_CHECK_UTF8, 0))
			add_error(b, file->name,
				  "File option java_string_check_utf8 is not allowed under editions. Use the "
				  "(pb.java).utf8_validation feature to control this behavior.");
	}
	if (!is_lite(file)) {
		for (int i = 0; i < file->n_dependencies; i++) {
			if (is_lite(file->dependencies[i])) {
				add_error(b, file->dependencies[i]->name,
					  xcat("Files that do not use optimize_for = LITE_RUNTIME cannot import files which do "
					       "use this option.  This file is not lite, but it imports \"",
					       file->dependencies[i]->name, "\" which is."));
				break;
			}
		}
	}
	if (file->edition == LP_EDITION_PROTO3) {
		for (int i = 0; i < file->n_extensions; i++)
			validate_proto3_field(b, &file->extensions[i]);
		for (int i = 0; i < file->n_messages; i++)
			validate_proto3_message(b, &file->messages[i]);
	}
	if (file->edition < LP_EDITION_2024 && file->n_option_dependencies > 0)
		add_error(b, "option", "option imports are not supported before edition 2024.");
	if (file->edition >= LP_EDITION_2024 && b->pool->version && b->pool->version->java_multiple_files_error &&
	    pb_has(file->options, FILEOPT_JAVA_MULTIPLE_FILES))
		add_error(b, file->name,
			  "The file option `java_multiple_files` is not supported in editions 2024 and above, which "
			  "defaults to the feature value of `nest_in_file_class = NO` (equivalent to "
			  "`java_multiple_files = true`).");
	if (file->edition >= LP_EDITION_2024 && file->n_weak_dependencies > 0)
		add_error(b, "weak", "weak imports are not allowed under edition 2024 and beyond.");
}

/* CheckFieldJsonNameUniqueness(message_name, message, descriptor, use_custom_names) */
static void check_field_json_name_uniqueness_pass(Builder *b, const LpMessage *message, bool use_custom)
{
	Map name_to_field = {0};
	typedef struct {
		const LpField *field;
		const char *orig_name;
		bool is_custom;
	} Details;
	for (int i = 0; i < message->n_fields; i++) {
		const LpField *field = &message->fields[i];
		const PbMsg *fp = field->proto;
		const char *name = pb_str(fp, FD_NAME);
		char *default_json_name = proto_json_name(name);
		Details *d = xalloc(sizeof *d);
		d->field = field;
		if (use_custom && pb_has(fp, FD_JSON_NAME) && !str_eq(pb_str(fp, FD_JSON_NAME), default_json_name)) {
			d->orig_name = pb_str(fp, FD_JSON_NAME);
			d->is_custom = true;
		} else {
			d->orig_name = default_json_name;
		}
		size_t on = strlen(d->orig_name);
		if (d->is_custom && on > 0 && d->orig_name[0] == '[' && d->orig_name[on - 1] == ']') {
			add_error(b, message->full_name,
				  xcat("The custom JSON name of field \"", name, "\" (\"", d->orig_name,
				       "\") is invalid: JSON names may not start with '[' and end with ']'."));
			continue;
		}
		Details *match = map_get(&name_to_field, d->orig_name);
		if (!match) {
			map_put(&name_to_field, d->orig_name, d);
			continue;
		}
		if (use_custom && !d->is_custom && !match->is_custom)
			continue;
		const char *suffix = str_eq(d->orig_name, match->orig_name) ? "" : xcat(" (\"", match->orig_name, "\")");
		char *msg = xcat("The ", d->is_custom ? "custom" : "default", " JSON name of field \"", name, "\" (\"",
				 d->orig_name, "\") conflicts with the ", match->is_custom ? "custom" : "default",
				 " JSON name of field \"", pb_str(match->field->proto, FD_NAME), "\"", suffix, ".");
		bool involves_default = !d->is_custom || !match->is_custom;
		if (proto_feature(message->merged_features, PROTO_FEATURE_JSON_FORMAT) == PROTO_JSON_LEGACY_BEST_EFFORT &&
		    involves_default)
			continue; /* a warning */
		add_error(b, message->full_name, msg);
	}
}

/* ValidateExtensionDeclaration */
static void validate_extension_declaration(Builder *b, const char *full_name, const PbMsg *range_options,
					   const PbMsg *range_proto, Map *full_name_set)
{
	Map numbers = {0};
	int64_t start = pb_int(range_proto, ER_START, 0), end = pb_int(range_proto, ER_END, 0);
	for (int i = 0; i < pb_count(range_options, EROPT_DECLARATION); i++) {
		const PbMsg *decl = pb_msg_at(range_options, EROPT_DECLARATION, i);
		int number = (int)pb_int(decl, DECL_NUMBER, 0);
		if (number < start || number >= end)
			add_error(b, full_name, xfmt("Extension declaration number %d is not in the extension range.", number));
		char *nk = xfmt("%d", number);
		if (map_has(&numbers, nk))
			add_error(b, full_name, xfmt("Extension declaration number %d is declared multiple times.", number));
		else
			map_put(&numbers, nk, nk);
		bool has_full_name = pb_has(decl, DECL_FULL_NAME), has_type = pb_has(decl, DECL_TYPE);
		if (!has_full_name || !has_type) {
			if (has_full_name != has_type || !pb_int(decl, DECL_RESERVED, 0))
				add_error(b, full_name,
					  xfmt("Extension declaration #%d should have both \"full_name\" and \"type\" set.", number));
		} else {
			const char *dfn = pb_str(decl, DECL_FULL_NAME);
			if (map_has(full_name_set, dfn)) {
				add_error(b, dfn, xcat("Extension field name \"", dfn, "\" is declared multiple times."));
				return;
			}
			map_put(full_name_set, dfn, (void *)dfn);
			const char *syms[2] = {dfn, pb_str(decl, DECL_TYPE)};
			for (int k = 0; k < 2; k++) {
				const char *s = syms[k];
				if (k == 1) {
					static const char *const non_message[] = {
						"double", "float", "int64", "uint64", "int32", "fixed32", "fixed64", "bool",
						"string", "bytes", "uint32", "enum", "sfixed32", "sfixed64", "sint32", "sint64"};
					bool nm = false;
					for (int j = 0; j < 16; j++)
						nm |= str_eq(s, non_message[j]);
					if (nm)
						continue;
				}
				if (s[0] != '.') {
					add_error(b, full_name,
						  xcat("\"", s, "\" must have a leading dot to indicate the fully-qualified scope."));
					continue;
				}
				/* ValidateQualifiedName */
				bool ok = true, last_was_period = false;
				for (const char *p = s; *p; p++) {
					if (ascii_isalnum((unsigned char)*p) || *p == '_')
						last_was_period = false;
					else if (*p == '.') {
						if (last_was_period)
							ok = false;
						last_was_period = true;
					} else
						ok = false;
				}
				if (!ok || last_was_period)
					add_error(b, full_name, xcat("\"", s, "\" contains invalid identifiers."));
			}
		}
	}
}

/* ValidateOptions(Descriptor) */
static void validate_message_options(Builder *b, const LpMessage *message)
{
	if (b->pool->version && b->pool->version->field_json_name_errors) {
		Vec errors = {0};
		b->pool->version->field_json_name_errors(message, &errors);
		for (int i = 0; i < errors.n; i++)
			add_error(b, message->full_name, VEC_AT(errors, const char, i));
	} else if (!pb_int(message->options, MSGOPT_DEPRECATED_LEGACY_JSON_FIELD_CONFLICTS, 0)) {
		check_field_json_name_uniqueness_pass(b, message, false);
		check_field_json_name_uniqueness_pass(b, message, true);
	}
	/* ValidateExtensionRangeOptions */
	int64_t max_extension_range = pb_int(message->options, MSGOPT_MESSAGE_SET_WIRE_FORMAT, 0) ? 2147483647 : 536870911;
	Map full_name_set = {0};
	for (int i = 0; i < message->n_extension_ranges; i++) {
		const LpExtensionRange *range = &message->extension_ranges[i];
		if (range->end > max_extension_range + 1)
			add_error(b, message->full_name,
				  xfmt("Extension numbers cannot be greater than %lld.", (long long)max_extension_range));
		const PbMsg *ro = range->options;
		if (pb_count(ro, EROPT_DECLARATION) > 0) {
			if (pb_has(ro, EROPT_VERIFICATION) && pb_int(ro, EROPT_VERIFICATION, 1) == 1 /* UNVERIFIED */) {
				add_error(b, message->full_name,
					  "Cannot mark the extension range as UNVERIFIED when it has extension(s) declared.");
				return;
			}
			validate_extension_declaration(b, message->full_name, ro, range->proto, &full_name_set);
		}
	}
}

static bool is_string_map_type(const LpField *field)
{
	if (!proto_field_is_map(field))
		return false;
	for (int i = 0; i < field->message_type->n_fields; i++)
		if (field->message_type->fields[i].type == PT_STRING)
			return true;
	return false;
}

/* ValidateFieldFeatures */
static void validate_field_features(Builder *b, const LpField *field)
{
	if (field->file->edition < LP_EDITION_2023)
		return;
	const PbMsg *proto = field->proto;
	if (pb_int(proto, FD_LABEL, LABEL_OPTIONAL) == LABEL_REQUIRED)
		add_error(b, field->full_name,
			  "Required label is not allowed under editions.  Use the feature field_presence = LEGACY_REQUIRED "
			  "to control this behavior.");
	if (pb_int(proto, FD_TYPE, PT_DOUBLE) == PT_GROUP)
		add_error(b, field->full_name,
			  "Group types are not allowed under editions.  Use the feature message_encoding = DELIMITED to "
			  "control this behavior.");
	if (pb_has(field->options, FOPT_PACKED))
		add_error(b, field->full_name,
			  "Field option packed is not allowed under editions.  Use the repeated_field_encoding feature to "
			  "control this behavior.");
	if (!proto_field_is_repeated(field) && !proto_field_has_presence(field)) {
		if (field->has_default_value)
			add_error(b, field->full_name, "Implicit presence fields can't specify defaults.");
		if (field->enum_type && proto_enum_is_closed(field->enum_type))
			add_error(b, field->full_name, "Implicit presence enum fields must always be open.");
	}
	if (field->is_extension && proto_field_is_required(field))
		add_error(b, field->full_name, "Extensions can't be required.");
	if (field->containing_type && proto_message_is_map_entry(field->containing_type))
		return;
	const PbMsg *pf = field->proto_features;
	if (pb_has(pf, PROTO_FEATURE_FIELD_PRESENCE)) {
		int fp = (int)pb_int(pf, PROTO_FEATURE_FIELD_PRESENCE, 0);
		if (field->containing_oneof)
			add_error(b, field->full_name, "Oneof fields can't specify field presence.");
		else if (proto_field_is_repeated(field))
			add_error(b, field->full_name, "Repeated fields can't specify field presence.");
		else if (field->is_extension && fp != PROTO_PRESENCE_LEGACY_REQUIRED)
			add_error(b, field->full_name, "Extensions can't specify field presence.");
		else if (field->message_type && fp == PROTO_PRESENCE_IMPLICIT)
			add_error(b, field->full_name, "Message fields can't specify implicit presence.");
	}
	if (!proto_field_is_repeated(field) && pb_has(pf, PROTO_FEATURE_REPEATED_FIELD_ENCODING))
		add_error(b, field->full_name, "Only repeated fields can specify repeated field encoding.");
	if (field->type != PT_STRING && !is_string_map_type(field) && pb_has(pf, PROTO_FEATURE_UTF8_VALIDATION))
		add_error(b, field->full_name, "Only string fields can specify utf8 validation.");
	if (!proto_field_is_packable(field) && pb_int(pf, PROTO_FEATURE_REPEATED_FIELD_ENCODING, 0) == PROTO_REPEATED_PACKED)
		add_error(b, field->full_name,
			  "Only repeated primitive fields can specify PACKED repeated field encoding.");
	if ((cpp_type_of(field->type) != LP_CPPTYPE_MESSAGE || (field->message_type && proto_message_is_map_entry(field->message_type))) &&
	    pb_has(pf, PROTO_FEATURE_MESSAGE_ENCODING))
		add_error(b, field->full_name, "Only message fields can specify message encoding.");
}

/* ValidateMapEntry */
static bool validate_map_entry(Builder *b, const LpField *field)
{
	const LpMessage *message = field->message_type;
	if (message->n_extensions != 0 || message->n_extension_ranges != 0 || message->n_nested != 0 ||
	    message->n_enums != 0 || message->n_fields != 2)
		return false;
	/* before 36: the checks of the map field itself */
	if (b->pool->version && b->pool->version->legacy_map_entry &&
	    (!proto_field_is_repeated(field) ||
	     !str_eq(message->name, xcat(proto_to_camel_case(field->name, false), "Entry")) ||
	     field->containing_type != message->containing_type))
		return false;
	const LpField *key = &message->fields[0], *value = &message->fields[1];
	if (key->number != 1 || value->number != 2)
		return false;
	if (key->label != LABEL_OPTIONAL || !str_eq(key->name, "key"))
		return false;
	if (value->label != LABEL_OPTIONAL || !str_eq(value->name, "value"))
		return false;
	switch (key->type) {
	case PT_ENUM:
		add_error(b, field->full_name, "Key in map fields cannot be enum types.");
		break;
	case PT_FLOAT: case PT_DOUBLE: case PT_MESSAGE: case PT_GROUP: case PT_BYTES:
		add_error(b, field->full_name, "Key in map fields cannot be float/double, bytes or message types.");
		break;
	default:
		break;
	}
	if (value->type == PT_ENUM && value->enum_type && value->enum_type->n_values > 0 &&
	    value->enum_type->values[0].number != 0)
		add_error(b, field->full_name, "Enum value in map must define 0 as the first value.");
	return true;
}

/* ValidateOptions(FieldDescriptor) */
static void validate_field_options(Builder *b, const LpField *field)
{
	validate_field_features(b, field);
	if (field->file->edition >= LP_EDITION_2024 && field_legacy_ctype(field) >= 0)
		add_error(b, field->full_name,
			  "ctype option is not allowed under edition 2024 and beyond. Use the feature string_type = "
			  "VIEW|CORD|STRING|... instead.");
	const PbMsg *opts = field->options;
	if (pb_int(opts, FOPT_LAZY, 0) || pb_int(opts, FOPT_UNVERIFIED_LAZY, 0)) {
		if (field->type != PT_MESSAGE)
			add_error(b, field->full_name, "[lazy = true] can only be specified for submessage fields.");
		if (pb_int(opts, FOPT_UNVERIFIED_LAZY, 0) && field->is_extension)
			add_error(b, field->full_name, "Extensions cannot be marked as unverified_lazy.");
	}
	if (pb_int(opts, FOPT_PACKED, 0) && !proto_field_is_packable(field))
		add_error(b, field->full_name, "[packed = true] can only be specified for repeated primitive fields.");
	if (field->containing_type && pb_int(field->containing_type->options, MSGOPT_MESSAGE_SET_WIRE_FORMAT, 0)) {
		if (field->is_extension) {
			if (proto_field_is_required(field) || proto_field_is_repeated(field) || field->type != PT_MESSAGE)
				add_error(b, field->full_name, "Extensions of MessageSets must be optional messages.");
		} else {
			add_error(b, field->full_name, "MessageSets cannot have fields, only extensions.");
		}
	}
	if (is_lite(field->file) && field->containing_type && !is_lite(field->containing_type->file))
		add_error(b, field->full_name,
			  "Extensions to non-lite types can only be declared in non-lite files.  Note that you cannot "
			  "extend a non-lite type to contain a lite type, but the reverse is allowed.");
	bool legacy_map_entry = b->pool->version && b->pool->version->legacy_map_entry;
	if (!legacy_map_entry && field->type == PT_GROUP && field->message_type &&
	    proto_message_is_map_entry(field->message_type))
		add_error(b, field->full_name,
			  "Groups cannot be map entries. Use a regular message field with map<KeyType, ValueType> syntax "
			  "instead.");
	if (proto_field_is_map(field)) {
		const LpMessage *message = field->message_type;
		if (!legacy_map_entry &&
		    (!proto_field_is_repeated(field) ||
		     !str_eq(message->name, xcat(proto_to_camel_case(field->name, false), "Entry")) ||
		     field->containing_type != message->containing_type)) {
			char *error = xcat(message->full_name,
					   " is a synthetic MapEntry message type, which is not allowed to be used as a field "
					   "type.");
			const LpMessage *container = message->containing_type;
			if (container) {
				LpSymbol *alter = lookup_symbol(b, message->name, container->full_name, LOOKUP_TYPES);
				if (alter && alter->desc != message)
					error = xcat(error, " Maybe you meant \".", alter->full_name, "\"?");
			}
			add_error(b, field->full_name, error);
		}
		if (!validate_map_entry(b, field))
			add_error(b, field->full_name,
				  "map_entry should not be set explicitly. Use map<KeyType, ValueType> instead.");
	}
	/* ValidateJSType */
	int jstype = (int)pb_int(opts, FOPT_JSTYPE, 0);
	if (jstype != 0) {
		switch (field->type) {
		case PT_UINT64: case PT_INT64: case PT_SINT64: case PT_FIXED64: case PT_SFIXED64:
			if (jstype == 1 || jstype == 2)
				break;
			add_error(b, field->full_name,
				  xcat("Illegal jstype for int64, uint64, sint64, fixed64 or sfixed64 field: ",
				       jstype == 0 ? "JS_NORMAL" : jstype == 1 ? "JS_STRING" : "JS_NUMBER"));
			break;
		default:
			add_error(b, field->full_name,
				  "jstype is only allowed on int64, uint64, sint64, fixed64 or sfixed64 fields.");
		}
	}
	if (field->is_extension && field->has_json_name && !str_eq(field->json_name, proto_json_name(field->name)))
		add_error(b, field->full_name, "option json_name is not allowed on extension fields.");
	if (str_has_nul(pb_bytes(field->proto, FD_JSON_NAME)))
		add_error(b, field->full_name, "json_name cannot have embedded null characters.");
	/* extension declarations are not enforced (ExtDeclEnforcementLevel::kNoEnforcement) */
}

/* CheckEnumValueUniqueness: in ValidateOptions(EnumDescriptor), or in BuildEnum for the versions
 * with their own error (LpVersion.enum_value_name_error, 3.x) */
static void check_enum_value_uniqueness(Builder *b, const LpEnum *enm)
{
	Map values = {0};
	for (int i = 0; i < enm->n_values; i++) {
		const LpEnumValue *value = &enm->values[i];
		char *stripped = enum_value_to_pascal_case(prefix_remover_maybe_remove(enm->name, value->name));
		const LpEnumValue *prev = map_get(&values, stripped);
		if (!prev) {
			map_put(&values, stripped, (void *)value);
			continue;
		}
		if (!str_eq(prev->name, value->name) && prev->number != value->number) {
			if (b->pool->version && b->pool->version->enum_value_name_error) {
				bool warning = false;
				const char *e = b->pool->version->enum_value_name_error(enm, value, prev, &warning);
				if (warning)
					add_warning(b, value->full_name, e);
				else
					add_error(b, value->full_name, e);
				continue;
			}
			if (pb_int(enm->options, ENUMOPT_DEPRECATED_LEGACY_JSON_FIELD_CONFLICTS, 0) &&
			    enm->file->edition == LP_EDITION_PROTO2)
				continue; /* a warning */
			add_error(b, value->full_name,
				  xcat("Enum name ", value->name, " has the same name as ", prev->name,
				       " if you ignore case and strip out the enum name prefix (if any). (If you are using "
				       "allow_alias, please assign the same number to each enum value name.)"));
		}
	}
}

/* CheckEnumValueUniqueness (36.2), CheckEnumCustomStringUniqueness, ValidateOptions(EnumDescriptor) */
static void validate_enum_options(Builder *b, const LpEnum *enm)
{
	if (!b->pool->version || !b->pool->version->enum_value_name_error)
		check_enum_value_uniqueness(b, enm);

	/* CheckEnumCustomStringUniqueness: pb.enumvalue.json */
	Map by_json = {0}, first_by_number = {0};
	bool allow_alias = pb_int(enm->options, ENUMOPT_ALLOW_ALIAS, 0) != 0;
	for (int i = 0; i < enm->n_values; i++) {
		const LpEnumValue *value = &enm->values[i];
		const PbMsg *json = pb_has(value->options, EVOPT_JSON) ? pb_msg(value->options, EVOPT_JSON) : NULL;
		if (allow_alias) {
			char *nk = xfmt("%d", value->number);
			const LpEnumValue *primary = map_get(&first_by_number, nk);
			if (!primary) {
				map_put(&first_by_number, nk, (void *)value);
			} else {
				const PbMsg *pj = pb_has(primary->options, EVOPT_JSON) ? pb_msg(primary->options, EVOPT_JSON) : NULL;
				bool ph = pj && pb_has(pj, 1), ch = json && pb_has(json, 1);
				if (ph != ch || (ph && !str_equal(pb_bytes(pj, 1), pb_bytes(json, 1)))) {
					const char *primary_str = ph ? xcat("\"", pb_str(pj, 1), "\"") : "unset";
					const char *current_str = ch ? xcat("\"", pb_str(json, 1), "\"") : "unset";
					add_error(b, enm->full_name,
						  xfmt("Alias values for number %d must have the same custom JSON string. \"%s\" "
						       "has %s but primary alias \"%s\" has %s.",
						       value->number, value->name, current_str, primary->name, primary_str));
				}
			}
		}
		for (int k = 0; k < 2; k++) {
			if (k == 1 && !json)
				break;
			const char *name = k == 0 ? value->name : pb_str(json, 1);
			const LpEnumValue *conflict = map_get(&by_json, name);
			if (!conflict) {
				map_put(&by_json, name, (void *)value);
			} else if (conflict != value && conflict->number != value->number) {
				add_error(b, enm->full_name,
					  xcat(k == 1 ? "Custom" : "Default", " JSON string \"", name, "\" conflicts with ",
					       str_eq(conflict->name, name) ? "default" : "custom", " JSON string of \"",
					       conflict->name, "\"."));
			}
		}
	}

	if (!proto_enum_is_closed(enm) && enm->n_values > 0 && enm->values[0].number != 0)
		add_error(b, enm->full_name, "The first enum value must be zero for open enums.");

	if (!allow_alias) {
		Map used = {0}, numbers = {0};
		for (int i = 0; i < enm->n_values; i++)
			map_put(&numbers, xfmt("%d", enm->values[i].number), "");
		for (int i = 0; i < enm->n_values; i++) {
			const LpEnumValue *v = &enm->values[i];
			char *nk = xfmt("%d", v->number);
			const LpEnumValue *prev = map_get(&used, nk);
			if (!prev) {
				map_put(&used, nk, (void *)v);
				continue;
			}
			int64_t next_value = (int64_t)v->number + 1;
			while (next_value <= 2147483647 && map_has(&numbers, xfmt("%lld", (long long)next_value)))
				next_value++;
			char *error = xcat("\"", v->full_name, "\" uses the same enum value as \"", prev->full_name,
					   "\". If this is intended, set 'option allow_alias = true;' to the enum definition.");
			if (next_value < 2147483647)
				error = xcat(error, " The next available enum value is ", lp_itoa(next_value), ".");
			add_error(b, enm->full_name, error);
		}
	}
}

/* absl::SimpleAtoi(int32) */
static bool simple_atoi32(Str s, int32_t *out)
{
	size_t i = 0, n = s.n;
	while (i < n && (s.p[i] == ' ' || (s.p[i] >= '\t' && s.p[i] <= '\r')))
		i++;
	while (n > i && (s.p[n - 1] == ' ' || (s.p[n - 1] >= '\t' && s.p[n - 1] <= '\r')))
		n--;
	bool neg = false;
	if (i < n && (s.p[i] == '-' || s.p[i] == '+'))
		neg = s.p[i++] == '-';
	if (i >= n)
		return false;
	int64_t v = 0;
	for (; i < n; i++) {
		if (!ascii_isdigit((unsigned char)s.p[i]))
			return false;
		v = v * 10 + (s.p[i] - '0');
		if (v > 2147483648LL)
			return false;
	}
	if (neg)
		v = -v;
	if (v > 2147483647 || v < -2147483648LL)
		return false;
	*out = (int32_t)v;
	return true;
}

static void validate_enum_value_options(Builder *b, const LpEnumValue *v)
{
	if (!pb_has(v->options, EVOPT_JSON))
		return;
	Str custom = pb_bytes(pb_msg(v->options, EVOPT_JSON), 1);
	if (str_has_nul(custom))
		add_error(b, v->full_name, "Custom JSON strings cannot contain embedded null characters.");
	int32_t parsed;
	if (simple_atoi32(custom, &parsed) && parsed != v->number)
		add_error(b, v->full_name,
			  xfmt("Custom JSON string \"%s\" parses as integer %d, which does not match the enum value's "
			       "number %d.",
			       custom.p, (int)parsed, v->number));
}

static void validate_message(Builder *b, const LpMessage *m);

static void validate_enum(Builder *b, const LpEnum *e)
{
	validate_enum_options(b, e);
	for (int i = 0; i < e->n_values; i++)
		validate_enum_value_options(b, &e->values[i]);
}

/* VisitDescriptors order: message, enums, oneofs, fields, nested, extensions, extension ranges */
static void validate_message(Builder *b, const LpMessage *m)
{
	validate_message_options(b, m);
	for (int i = 0; i < m->n_enums; i++)
		validate_enum(b, &m->enums[i]);
	for (int i = 0; i < m->n_fields; i++)
		validate_field_options(b, &m->fields[i]);
	for (int i = 0; i < m->n_nested; i++)
		validate_message(b, &m->nested[i]);
	for (int i = 0; i < m->n_extensions; i++)
		validate_field_options(b, &m->extensions[i]);
}

static void validate_file(Builder *b, const LpFile *file)
{
	validate_file_options(b, file);
	for (int i = 0; i < file->n_messages; i++)
		validate_message(b, &file->messages[i]);
	for (int i = 0; i < file->n_enums; i++)
		validate_enum(b, &file->enums[i]);
	for (int i = 0; i < file->n_extensions; i++)
		validate_field_options(b, &file->extensions[i]);
	for (int i = 0; i < file->n_services; i++) {
		const LpService *s = &file->services[i];
		if (is_lite(s->file) && (pb_int(s->file->options, FILEOPT_CC_GENERIC_SERVICES, 0) ||
					 pb_int(s->file->options, FILEOPT_JAVA_GENERIC_SERVICES, 0)))
			add_error(b, s->full_name,
				  "Files with optimize_for = LITE_RUNTIME cannot define services unless you set both options "
				  "cc_generic_services and java_generic_services to false.");
	}
}

/* DetectMapConflicts */
static void detect_map_conflicts(Builder *b, const LpMessage *message)
{
	Map seen = {0};
	for (int i = 0; i < message->n_nested; i++) {
		const LpMessage *nested = &message->nested[i];
		const LpMessage *prev = map_get(&seen, nested->name);
		if (!prev) {
			map_put(&seen, nested->name, (void *)nested);
		} else if (proto_message_is_map_entry(prev) || proto_message_is_map_entry(nested)) {
			add_error(b, message->full_name,
				  xcat("Expanded map entry type ", nested->name, " conflicts with an existing nested message type."));
			if (!pool_legacy_builder(b->pool))
				break;
		}
		detect_map_conflicts(b, nested);
	}
	static const char *const kinds[3] = {"field", "enum type", "oneof type"};
	for (int k = 0; k < 3; k++) {
		int n = k == 0 ? message->n_fields : k == 1 ? message->n_enums : message->n_oneofs;
		for (int i = 0; i < n; i++) {
			const char *name = k == 0 ? message->fields[i].name : k == 1 ? message->enums[i].name : message->oneofs[i].name;
			const LpMessage *it = map_get(&seen, name);
			if (it && proto_message_is_map_entry(it))
				add_error(b, message->full_name,
					  xcat("Expanded map entry type ", it->name, " conflicts with an existing ", kinds[k], "."));
		}
	}
}

/* ---- deferred feature-lifetime validation ---- */

static void collect_lifetimes(Builder *b, const PbMsg *proto_features, const PbMsg *options, const char *full_name)
{
	if (proto_features) {
		LifetimesInfo *li = xalloc(sizeof *li);
		li->option = proto_features;
		li->full_name = full_name;
		vec_push(&b->lifetimes, li);
	}
	if (options && !proto_msg_is_empty(options) &&
	    !(b->pool->version && b->pool->version->legacy_feature_lifetimes)) {
		LifetimesInfo *li = xalloc(sizeof *li);
		li->option = options;
		li->full_name = full_name;
		vec_push(&b->lifetimes, li);
	}
}

static void collect_message_lifetimes(Builder *b, const LpMessage *m);

static void collect_enum_lifetimes(Builder *b, const LpEnum *e)
{
	collect_lifetimes(b, e->proto_features, e->options, e->full_name);
	for (int i = 0; i < e->n_values; i++)
		collect_lifetimes(b, e->values[i].proto_features, e->values[i].options, e->values[i].full_name);
}

static void collect_message_lifetimes(Builder *b, const LpMessage *m)
{
	collect_lifetimes(b, m->proto_features, m->options, m->full_name);
	for (int i = 0; i < m->n_enums; i++)
		collect_enum_lifetimes(b, &m->enums[i]);
	for (int i = 0; i < m->n_oneofs; i++)
		collect_lifetimes(b, m->oneofs[i].proto_features, m->oneofs[i].options, m->oneofs[i].full_name);
	for (int i = 0; i < m->n_fields; i++)
		collect_lifetimes(b, m->fields[i].proto_features, m->fields[i].options, m->fields[i].full_name);
	for (int i = 0; i < m->n_nested; i++)
		collect_message_lifetimes(b, &m->nested[i]);
	for (int i = 0; i < m->n_extensions; i++)
		collect_lifetimes(b, m->extensions[i].proto_features, m->extensions[i].options, m->extensions[i].full_name);
	for (int i = 0; i < m->n_extension_ranges; i++)
		collect_lifetimes(b, m->extension_ranges[i].proto_features, m->extension_ranges[i].options, m->full_name);
}

static void collect_file_lifetimes(Builder *b, const LpFile *f)
{
	collect_lifetimes(b, f->proto_features, f->options, f->name);
	for (int i = 0; i < f->n_messages; i++)
		collect_message_lifetimes(b, &f->messages[i]);
	for (int i = 0; i < f->n_enums; i++)
		collect_enum_lifetimes(b, &f->enums[i]);
	for (int i = 0; i < f->n_extensions; i++)
		collect_lifetimes(b, f->extensions[i].proto_features, f->extensions[i].options, f->extensions[i].full_name);
	for (int i = 0; i < f->n_services; i++) {
		const LpService *s = &f->services[i];
		collect_lifetimes(b, s->proto_features, s->options, s->full_name);
		for (int j = 0; j < s->n_methods; j++)
			collect_lifetimes(b, s->methods[j].proto_features, s->methods[j].options, s->methods[j].full_name);
	}
}

/* ---- BuildFile ---- */

static bool proto_file_matches(LpFile *existing, PbMsg *proto);

/* DescriptorBuilder::BuildFile + BuildFileImpl + DeferredValidation::Validate */
/* The OptionInterpreter is not ported: an element whose options still hold uninterpreted_option
 * (protoc and buf send interpreted options) fails the build instead of keeping them, which would
 * give another descriptor than upstream. */
static bool check_uninterpreted(Builder *b, const char *element, const PbMsg *options)
{
	if (!options || !pb_count(options, OPT_UNINTERPRETED))
		return true;
	buf_fmt(b->log,
		"unsupported by protoc-gen-uni: uninterpreted_option in the options of %s in \"%s\" (the option "
		"interpreter of protoc is not ported; protoc and buf send interpreted options)\n",
		element, b->filename);
	b->had_errors = true;
	return false;
}

static bool check_uninterpreted_field(Builder *b, const LpField *f) { return check_uninterpreted(b, f->full_name, f->options); }

static bool check_uninterpreted_enum(Builder *b, const LpEnum *e)
{
	if (!check_uninterpreted(b, e->full_name, e->options))
		return false;
	for (int i = 0; i < e->n_values; i++)
		if (!check_uninterpreted(b, e->values[i].full_name, e->values[i].options))
			return false;
	return true;
}

static bool check_uninterpreted_message(Builder *b, const LpMessage *m)
{
	if (!check_uninterpreted(b, m->full_name, m->options))
		return false;
	for (int i = 0; i < m->n_fields; i++)
		if (!check_uninterpreted_field(b, &m->fields[i]))
			return false;
	for (int i = 0; i < m->n_oneofs; i++)
		if (!check_uninterpreted(b, m->oneofs[i].full_name, m->oneofs[i].options))
			return false;
	for (int i = 0; i < m->n_extension_ranges; i++)
		if (!check_uninterpreted(b, xcat("an extension range of ", m->full_name), m->extension_ranges[i].options))
			return false;
	for (int i = 0; i < m->n_extensions; i++)
		if (!check_uninterpreted_field(b, &m->extensions[i]))
			return false;
	for (int i = 0; i < m->n_enums; i++)
		if (!check_uninterpreted_enum(b, &m->enums[i]))
			return false;
	for (int i = 0; i < m->n_nested; i++)
		if (!check_uninterpreted_message(b, &m->nested[i]))
			return false;
	return true;
}

static COLD void check_uninterpreted_file(Builder *b, const LpFile *file)
{
	if (!check_uninterpreted(b, "the file", file->options))
		return;
	for (int i = 0; i < file->n_messages; i++)
		if (!check_uninterpreted_message(b, &file->messages[i]))
			return;
	for (int i = 0; i < file->n_enums; i++)
		if (!check_uninterpreted_enum(b, &file->enums[i]))
			return;
	for (int i = 0; i < file->n_services; i++) {
		const LpService *s = &file->services[i];
		if (!check_uninterpreted(b, s->full_name, s->options))
			return;
		for (int j = 0; j < s->n_methods; j++)
			if (!check_uninterpreted(b, s->methods[j].full_name, s->methods[j].options))
				return;
	}
	for (int i = 0; i < file->n_extensions; i++)
		if (!check_uninterpreted_field(b, &file->extensions[i]))
			return;
}

static LpFile *lp_build_file(LpPool *pool, PbMsg *proto, Buf *log)
{
	Builder bb = {0}, *b = &bb;
	b->pool = pool;
	b->log = log;
	b->filename = pb_str(proto, FDP_NAME);
	b->recursion_depth = 32;
	b->undefine_resolved_name = "";
	b->dependencies = &pool->dependencies;
	b->option_dependencies = &pool->option_dependencies;
	file_set_clear(b->dependencies);
	file_set_clear(b->option_dependencies);
	b->public_dependencies_recorded = true; /* until the imports are recorded */

	LpFile *existing = lp_find_file(pool, b->filename);
	if (existing && proto_file_matches(existing, proto))
		return existing;
	for (int i = 0; i < pool->pending_files.n; i++) {
		if (str_eq(VEC_AT(pool->pending_files, const char, i), b->filename)) {
			/* AddRecursiveImportError */
			Buf e = {0};
			buf_adds(&e, "File recursively imports itself: ");
			for (int j = i; j < pool->pending_files.n; j++) {
				buf_adds(&e, VEC_AT(pool->pending_files, const char, j));
				buf_adds(&e, " -> ");
			}
			buf_adds(&e, b->filename);
			add_error(b, i + 1 < pool->pending_files.n ? VEC_AT(pool->pending_files, const char, i + 1) : b->filename,
				  buf_str(&e));
			return NULL;
		}
	}
	if (pb_bytes(proto, FDP_PACKAGE).n > 511 && !pool_legacy_builder(pool)) {
		add_error(b, pb_str(proto, FDP_PACKAGE), "Package name is too long");
		return NULL;
	}

	/* BuildFileImpl */
	LpFile *result = xalloc(sizeof *result);
	b->file = result;
	result->kind = PROTO_FILE;
	result->pool = &pool->base;
	result->proto = proto;
	result->id = pool->base.next_id++;
	if (pb_has(proto, FDP_EDITION)) {
		result->edition = (int)pb_int(proto, FDP_EDITION, 0);
	} else {
		const char *syntax = pb_str(proto, FDP_SYNTAX);
		if (!*syntax || str_eq(syntax, "proto2"))
			result->edition = LP_EDITION_PROTO2;
		else if (str_eq(syntax, "proto3"))
			result->edition = LP_EDITION_PROTO3;
		else {
			result->edition = LP_EDITION_UNKNOWN;
			add_error(b, b->filename, xcat("Unrecognized syntax: ", syntax));
		}
	}
	const char *err = NULL;
	if (result->edition != LP_EDITION_UNKNOWN || !pool->version || !pool->version->no_editions) {
		err = lp_feature_resolver_create(&b->resolver, result->edition, pool->feature_set_defaults,
						 pool->feature_set_def);
		if (err)
			add_error(b, b->filename, err);
		else
			b->has_resolver = true;
	}

	result->source_code_info = pb_has(proto, FDP_SOURCE_CODE_INFO) ? pb_msg(proto, FDP_SOURCE_CODE_INFO) : NULL;
	if (!pb_has(proto, FDP_NAME))
		add_error(b, "", "Missing field: FileDescriptorProto.name.");
	result->name = b->filename;
	result->package = result->full_name = pb_str(proto, FDP_PACKAGE);
	result->merged_features = pool->empty_features;

	if (str_has_nul(pb_bytes(proto, FDP_NAME))) {
		add_error(b, result->name, xcat("\"", result->name, "\" contains null character."));
		return NULL;
	}
	if (lp_find_file(pool, result->name)) {
		add_error(b, b->filename, "A file with this name is already in the pool.");
		return NULL;
	}
	proto_pool_add_file(&pool->base, result);
	if (*result->package) {
		int dots = 0;
		size_t prefix_bytes = 0; /* the names AddPackage copies: every prefix ending before a '.' */
		for (const char *p = result->package; *p; p++)
			if (*p == '.') {
				dots++;
				prefix_bytes += (size_t)(p - result->package) + 1;
			}
		if (dots > 100 && !pool_legacy_builder(pool)) {
			add_error(b, result->package, "Exceeds Maximum Package Depth");
			return NULL;
		}
		/* Without those limits (3.19.1), AddPackage takes time and memory quadratic in the length
		 * of the package. 1 MiB of prefixes is 20 times what any package within them (511 bytes,
		 * 100 dots) needs. */
		if (prefix_bytes > LP_MAX_PACKAGE_PREFIX_BYTES) {
			buf_fmt(b->log,
				"unsupported by protoc-gen-uni: the package of \"%s\" has %d dots in %lld bytes (its "
				"prefixes would take over 1 MiB; libprotoc 3.21 rejects over 100 dots or 511 bytes)\n",
				b->filename, dots, (long long)strlen(result->package));
			b->had_errors = true;
			return NULL;
		}
		add_package(b, result->package, str_has_nul(pb_bytes(proto, FDP_PACKAGE)), result, true);
	}

	/* dependencies */
	int n_deps = pb_count(proto, FDP_DEPENDENCY), n_opt = pb_count(proto, FDP_OPTION_DEPENDENCY);
	result->n_dependencies = n_deps;
	result->dependencies = xalloc(sizeof(LpFile *) * (size_t)(n_deps + 1));
	result->n_option_dependencies = n_opt;
	result->option_dependencies = xalloc(sizeof(char *) * (size_t)(n_opt + 1));
	for (int i = 0; i < n_opt; i++)
		result->option_dependencies[i] = pb_str_at(proto, FDP_OPTION_DEPENDENCY, i);
	LpFile **result_option_dependencies = xalloc(sizeof(LpFile *) * (size_t)(n_opt + 1));
	Map seen = {0};
	Map weak = {0};
	for (int i = 0; i < pb_count(proto, FDP_WEAK_DEPENDENCY); i++)
		map_put(&weak, xfmt("%d", (int)pb_int_at(proto, FDP_WEAK_DEPENDENCY, i)), "");
	for (int i = 0; i < n_deps + n_opt; i++) {
		bool is_option_dep = i >= n_deps;
		const char *name = is_option_dep ? pb_str_at(proto, FDP_OPTION_DEPENDENCY, i - n_deps)
						 : pb_str_at(proto, FDP_DEPENDENCY, i);
		if (map_has(&seen, name))
			add_error(b, name, xcat("Import \"", name, "\" was listed twice."));
		else
			map_put(&seen, name, "");
		LpFile *dependency = lp_find_file(pool, name);
		if (dependency == result)
			return NULL;
		if (!dependency) {
			if (map_has(&weak, xfmt("%d", i)) || is_option_dep) {
				/* a placeholder file */
				dependency = xalloc(sizeof *dependency);
				dependency->kind = PROTO_FILE;
				dependency->name = name;
				dependency->package = dependency->full_name = "";
				dependency->pool = &pool->base;
				dependency->merged_features = pool->empty_features;
				dependency->id = pool->base.next_id++;
			} else {
				add_error(b, name, xcat("Import \"", name, "\" has not been loaded."));
			}
		}
		if (is_option_dep)
			result_option_dependencies[i - n_deps] = dependency;
		else
			result->dependencies[i] = dependency;
	}

	result->public_dependencies = xalloc(sizeof(int) * (size_t)(pb_count(proto, FDP_PUBLIC_DEPENDENCY) + 1));
	for (int i = 0; i < pb_count(proto, FDP_PUBLIC_DEPENDENCY); i++) {
		int64_t index = pb_int_at(proto, FDP_PUBLIC_DEPENDENCY, i);
		if (index >= 0 && index < n_deps)
			result->public_dependencies[result->n_public_dependencies++] = (int)index;
		else
			add_error(b, b->filename, "Invalid public dependency index.");
	}

	for (int i = 0; i < n_deps; i++)
		record_dependency(b->dependencies, result->dependencies[i]);
	for (int i = 0; i < n_opt; i++)
		record_dependency(b->option_dependencies, result_option_dependencies[i]);
	b->public_dependencies_recorded = false;

	result->weak_dependencies = xalloc(sizeof(int) * (size_t)(pb_count(proto, FDP_WEAK_DEPENDENCY) + 1));
	for (int i = 0; i < pb_count(proto, FDP_WEAK_DEPENDENCY); i++) {
		int64_t index = pb_int_at(proto, FDP_WEAK_DEPENDENCY, i);
		if (index >= 0 && index < n_deps)
			result->weak_dependencies[result->n_weak_dependencies++] = (int)index;
		else
			add_error(b, b->filename, "Invalid weak dependency index.");
	}

	/* Convert children. */
	result->n_messages = pb_count(proto, FDP_MESSAGE_TYPE);
	result->messages = xalloc(sizeof(LpMessage) * (size_t)(result->n_messages + 1));
	for (int i = 0; i < result->n_messages; i++)
		build_message(b, pb_msg_at(proto, FDP_MESSAGE_TYPE, i), NULL, &result->messages[i], i);
	result->n_enums = pb_count(proto, FDP_ENUM_TYPE);
	result->enums = xalloc(sizeof(LpEnum) * (size_t)(result->n_enums + 1));
	for (int i = 0; i < result->n_enums; i++)
		build_enum(b, pb_msg_at(proto, FDP_ENUM_TYPE, i), NULL, &result->enums[i], i);
	result->n_services = pb_count(proto, FDP_SERVICE);
	result->services = xalloc(sizeof(LpService) * (size_t)(result->n_services + 1));
	for (int i = 0; i < result->n_services; i++)
		build_service(b, pb_msg_at(proto, FDP_SERVICE, i), &result->services[i], i);
	result->n_extensions = pb_count(proto, FDP_EXTENSION);
	result->extensions = xalloc(sizeof(LpField) * (size_t)(result->n_extensions + 1));
	for (int i = 0; i < result->n_extensions; i++)
		build_field_or_extension(b, pb_msg_at(proto, FDP_EXTENSION, i), NULL, &result->extensions[i], true, i);

	result->options = allocate_options(proto, FDP_OPTIONS);

	cross_link_file(b, result, proto);

	if (b->message_hints.n && !(pool_legacy_builder(b->pool)))
		suggest_field_numbers(b, result);

	/* OptionInterpreter::InterpretNonExtensionOptions / InterpretOptions: not ported */
	if (!b->had_errors)
		check_uninterpreted_file(b, result);

	if (!b->had_errors) {
		resolve_file_features(b, result);
		for (int i = 0; i < result->n_messages; i++)
			post_process_message(b, &result->messages[i]);
		for (int i = 0; i < result->n_extensions; i++)
			post_process_field(b, &result->extensions[i]);
	}

	if (!b->had_errors)
		validate_file(b, result);

	if (b->had_errors)
		for (int i = 0; i < result->n_messages; i++)
			detect_map_conflicts(b, &result->messages[i]);

	if (!b->had_errors && pool->builtin)
		collect_file_lifetimes(b, result);

	if (b->had_errors)
		return NULL;

	/* DeferredValidation::Validate */
	bool has_errors = false;
	for (int i = 0; i < b->lifetimes.n; i++) {
		const LifetimesInfo *li = VEC_AT(b->lifetimes, LifetimesInfo, i);
		Vec errors = {0};
		lp_validate_feature_lifetimes(result->edition, li->option, pool, &errors);
		for (int j = 0; j < errors.n; j++) {
			has_errors = true;
			buf_fmt(log, "%s %s: %s\n", b->filename, li->full_name, VEC_AT(errors, const char, j));
		}
	}
	return has_errors ? NULL : result;
}

/* ---- CopyTo ---- */

static void restore_features(PbMsg *proto, uint32_t options_field, uint32_t features_field, const PbMsg *features)
{
	if (features)
		proto_msg_set_msg(proto_msg_mutable(proto, options_field), features_field, proto_msg_clone(features));
}

static void copy_options(PbMsg *proto, uint32_t options_field, const PbMsg *options)
{
	if (options)
		proto_msg_set_msg(proto, options_field, proto_msg_clone(options));
}

static const char *qualified_name(const char *full_name) { return xcat(".", full_name); }

static void field_copy_to(const LpField *f, PbMsg *proto)
{
	proto_msg_set_cs(proto, FD_NAME, f->name);
	proto_msg_set_u(proto, FD_NUMBER, (uint64_t)(int64_t)f->number);
	if (f->has_json_name)
		proto_msg_set_cs(proto, FD_JSON_NAME, f->json_name);
	if (f->proto3_optional)
		proto_msg_set_u(proto, FD_PROTO3_OPTIONAL, 1);
	if (proto_field_is_required(f) && !is_legacy_edition(f->file->edition))
		proto_msg_set_u(proto, FD_LABEL, LABEL_OPTIONAL);
	else
		proto_msg_set_u(proto, FD_LABEL, (uint64_t)f->label);
	if (f->type == PT_GROUP && !is_legacy_edition(f->file->edition))
		proto_msg_set_u(proto, FD_TYPE, PT_MESSAGE);
	else
		proto_msg_set_u(proto, FD_TYPE, (uint64_t)f->type);
	if (f->is_extension)
		proto_msg_set_cs(proto, FD_EXTENDEE, qualified_name(f->containing_type->full_name));
	if (cpp_type_of(f->type) == LP_CPPTYPE_MESSAGE)
		proto_msg_set_cs(proto, FD_TYPE_NAME, qualified_name(f->message_type->full_name));
	else if (cpp_type_of(f->type) == LP_CPPTYPE_ENUM)
		proto_msg_set_cs(proto, FD_TYPE_NAME, qualified_name(f->enum_type->full_name));
	if (f->has_default_value)
		proto_msg_set_cs(proto, FD_DEFAULT_VALUE, lp_field_default_value_as_string(f, false));
	if (f->containing_oneof && !f->is_extension)
		proto_msg_set_u(proto, FD_ONEOF_INDEX, (uint64_t)f->containing_oneof->index);
	copy_options(proto, FD_OPTIONS, f->options);
	if (field_legacy_ctype(f) >= 0)
		proto_msg_set_u(proto_msg_mutable(proto, FD_OPTIONS), FOPT_CTYPE, (uint64_t)field_legacy_ctype(f));
	restore_features(proto, FD_OPTIONS, FOPT_FEATURES, f->proto_features);
}

static void enum_copy_to(const LpEnum *e, PbMsg *proto)
{
	proto_msg_set_cs(proto, EDP_NAME, e->name);
	for (int i = 0; i < e->n_values; i++) {
		const LpEnumValue *v = &e->values[i];
		PbMsg *vp = proto_msg_add(proto, EDP_VALUE);
		proto_msg_set_cs(vp, EVDP_NAME, v->name);
		proto_msg_set_u(vp, EVDP_NUMBER, (uint64_t)(int64_t)v->number);
		copy_options(vp, EVDP_OPTIONS, v->options);
		restore_features(vp, EVDP_OPTIONS, EVOPT_FEATURES, v->proto_features);
	}
	for (int i = 0; i < e->n_reserved_ranges; i++) {
		PbMsg *r = proto_msg_add(proto, EDP_RESERVED_RANGE);
		proto_msg_set_u(r, 1, (uint64_t)(int64_t)e->reserved_ranges[i].start);
		proto_msg_set_u(r, 2, (uint64_t)(int64_t)e->reserved_ranges[i].end);
	}
	for (int i = 0; i < e->n_reserved_names; i++)
		proto_msg_add_s(proto, EDP_RESERVED_NAME, pb_bytes_at(e->proto, EDP_RESERVED_NAME, i));
	if (e->visibility)
		proto_msg_set_u(proto, EDP_VISIBILITY, (uint64_t)e->visibility);
	copy_options(proto, EDP_OPTIONS, e->options);
	restore_features(proto, EDP_OPTIONS, ENUMOPT_FEATURES, e->proto_features);
}

static void message_copy_to(const LpMessage *m, PbMsg *proto)
{
	proto_msg_set_cs(proto, DP_NAME, m->name);
	for (int i = 0; i < m->n_reserved_ranges; i++) {
		PbMsg *r = proto_msg_add(proto, DP_RESERVED_RANGE);
		proto_msg_set_u(r, 1, (uint64_t)(int64_t)m->reserved_ranges[i].start);
		proto_msg_set_u(r, 2, (uint64_t)(int64_t)m->reserved_ranges[i].end);
	}
	for (int i = 0; i < m->n_reserved_names; i++)
		proto_msg_add_s(proto, DP_RESERVED_NAME, pb_bytes_at(m->proto, DP_RESERVED_NAME, i));
	copy_options(proto, DP_OPTIONS, m->options);
	if (m->visibility)
		proto_msg_set_u(proto, DP_VISIBILITY, (uint64_t)m->visibility);
	restore_features(proto, DP_OPTIONS, MSGOPT_FEATURES, m->proto_features);

	for (int i = 0; i < m->n_fields; i++)
		field_copy_to(&m->fields[i], proto_msg_add(proto, DP_FIELD));
	for (int i = 0; i < m->n_oneofs; i++) {
		const LpOneof *o = &m->oneofs[i];
		PbMsg *op = proto_msg_add(proto, DP_ONEOF_DECL);
		proto_msg_set_cs(op, ODP_NAME, o->name);
		copy_options(op, ODP_OPTIONS, o->options);
		restore_features(op, ODP_OPTIONS, ONEOFOPT_FEATURES, o->proto_features);
	}
	for (int i = 0; i < m->n_nested; i++)
		message_copy_to(&m->nested[i], proto_msg_add(proto, DP_NESTED_TYPE));
	for (int i = 0; i < m->n_enums; i++)
		enum_copy_to(&m->enums[i], proto_msg_add(proto, DP_ENUM_TYPE));
	for (int i = 0; i < m->n_extension_ranges; i++) {
		const LpExtensionRange *r = &m->extension_ranges[i];
		PbMsg *rp = proto_msg_add(proto, DP_EXTENSION_RANGE);
		proto_msg_set_u(rp, ER_START, (uint64_t)(int64_t)r->start);
		proto_msg_set_u(rp, ER_END, (uint64_t)(int64_t)r->end);
		copy_options(rp, ER_OPTIONS, r->options);
		restore_features(rp, ER_OPTIONS, EROPT_FEATURES, r->proto_features);
	}
	for (int i = 0; i < m->n_extensions; i++)
		field_copy_to(&m->extensions[i], proto_msg_add(proto, DP_EXTENSION));
}

/* FileDescriptor::CopyTo, as a FileDescriptorProto of the builtin pool. */
static PbMsg *lp_file_copy_to(const LpFile *file)
{
	LpPool *builtin = lp_file_pool(file)->builtin ? lp_file_pool(file)->builtin : lp_file_pool(file);
	PbMsg *proto = pb_new(&lp_msgdef_by_name(builtin, "google.protobuf.FileDescriptorProto")->pb);
	proto_msg_set_cs(proto, FDP_NAME, file->name);
	if (*file->package)
		proto_msg_set_cs(proto, FDP_PACKAGE, file->package);
	if (file->edition == LP_EDITION_PROTO3) {
		proto_msg_set_cs(proto, FDP_SYNTAX, "proto3");
	} else if (!is_legacy_edition(file->edition)) {
		proto_msg_set_cs(proto, FDP_SYNTAX, "editions");
		proto_msg_set_u(proto, FDP_EDITION, (uint64_t)(int64_t)file->edition);
	}
	copy_options(proto, FDP_OPTIONS, file->options);
	restore_features(proto, FDP_OPTIONS, FILEOPT_FEATURES, file->proto_features);
	for (int i = 0; i < file->n_dependencies; i++)
		proto_msg_add_s(proto, FDP_DEPENDENCY, str_c(file->dependencies[i]->name));
	for (int i = 0; i < file->n_public_dependencies; i++)
		proto_msg_add_u(proto, FDP_PUBLIC_DEPENDENCY, (uint64_t)(int64_t)file->public_dependencies[i]);
	for (int i = 0; i < file->n_weak_dependencies; i++)
		proto_msg_add_u(proto, FDP_WEAK_DEPENDENCY, (uint64_t)(int64_t)file->weak_dependencies[i]);
	for (int i = 0; i < file->n_option_dependencies; i++)
		proto_msg_add_s(proto, FDP_OPTION_DEPENDENCY, str_c(file->option_dependencies[i]));
	for (int i = 0; i < file->n_messages; i++)
		message_copy_to(&file->messages[i], proto_msg_add(proto, FDP_MESSAGE_TYPE));
	for (int i = 0; i < file->n_enums; i++)
		enum_copy_to(&file->enums[i], proto_msg_add(proto, FDP_ENUM_TYPE));
	for (int i = 0; i < file->n_services; i++) {
		const LpService *s = &file->services[i];
		PbMsg *sp = proto_msg_add(proto, FDP_SERVICE);
		proto_msg_set_cs(sp, SDP_NAME, s->name);
		for (int j = 0; j < s->n_methods; j++) {
			const LpMethod *m = &s->methods[j];
			PbMsg *mp = proto_msg_add(sp, SDP_METHOD);
			proto_msg_set_cs(mp, MDP_NAME, m->name);
			proto_msg_set_cs(mp, MDP_INPUT_TYPE, qualified_name(m->input_type->full_name));
			proto_msg_set_cs(mp, MDP_OUTPUT_TYPE, qualified_name(m->output_type->full_name));
			copy_options(mp, MDP_OPTIONS, m->options);
			if (m->client_streaming)
				proto_msg_set_u(mp, MDP_CLIENT_STREAMING, 1);
			if (m->server_streaming)
				proto_msg_set_u(mp, MDP_SERVER_STREAMING, 1);
			restore_features(mp, MDP_OPTIONS, METHOPT_FEATURES, m->proto_features);
		}
		copy_options(sp, SDP_OPTIONS, s->options);
		restore_features(sp, SDP_OPTIONS, SVCOPT_FEATURES, s->proto_features);
	}
	for (int i = 0; i < file->n_extensions; i++)
		field_copy_to(&file->extensions[i], proto_msg_add(proto, FDP_EXTENSION));
	return proto;
}

/* ExistingFileMatchesProto: the deterministic serializations of CopyTo and of the proto agree. */
static bool proto_file_matches(LpFile *existing, PbMsg *proto)
{
	if (!lp_file_pool(existing)->builtin)
		return false;
	PbMsg *existing_proto = lp_file_copy_to(existing);
	if (existing->edition == LP_EDITION_PROTO2 && pb_has(proto, FDP_SYNTAX))
		proto_msg_set_cs(existing_proto, FDP_SYNTAX, "proto2");
	return str_equal(lp_serialize_str(existing_proto), lp_serialize_str(proto));
}

/* ---- message definitions from a pool ---- */

static int cmp_field_ptr(const void *a, const void *b)
{
	const LpField *x = *(const LpField *const *)a, *y = *(const LpField *const *)b;
	return x->number < y->number ? -1 : x->number > y->number;
}

/* The cached definition of m, created empty (filled by msgdef_fill) if absent. */
static LpMsgDef *msgdef_shell(LpPool *pool, const LpMessage *m, Vec *todo)
{
	if (pool->defs_files != pool->base.file_list.n) {
		memset(&pool->defs, 0, sizeof pool->defs);
		pool->defs_files = pool->base.file_list.n;
	}
	LpMsgDef *d = map_get(&pool->defs, m->full_name);
	if (!d) {
		d = xalloc(sizeof *d);
		d->desc = m;
		map_put(&pool->defs, m->full_name, d);
		vec_push(todo, d);
	}
	return d;
}

static void msgdef_fill(LpPool *pool, LpMsgDef *d, Vec *todo)
{
	const LpMessage *m = d->desc;
	const Vec *exts = proto_pool_extensions(&pool->base, m->full_name);
	int n_ext = exts ? exts->n : 0;
	int n = 0;
	const LpField **fs = xalloc(sizeof(LpField *) * (size_t)(m->n_fields + n_ext + 1));
	for (int i = 0; i < m->n_fields; i++)
		fs[n++] = &m->fields[i];
	for (int i = 0; i < n_ext; i++)
		fs[n++] = VEC_AT(*exts, LpField, i);
	qsort(fs, (size_t)n, sizeof *fs, cmp_field_ptr);
	int k = 0;
	for (int i = 0; i < n; i++)
		if (k == 0 || fs[k - 1]->number != fs[i]->number)
			fs[k++] = fs[i];
	n = k;
	PbField *pf = xalloc(sizeof(PbField) * (size_t)(n + 1));
	d->fields = fs;
	d->flags = xalloc((size_t)(n + 1));
	d->oneof = xalloc(sizeof(int) * (size_t)(n + 1));
	d->pb.n = n;
	d->pb.f = pf;
	for (int i = 0; i < n; i++) {
		const LpField *f = fs[i];
		pf[i].num = (uint32_t)f->number;
		pf[i].type = (uint8_t)f->type;
		pf[i].label = f->label == LABEL_REPEATED ? (proto_field_is_packed(f) ? PL_PACKED : PL_REP) : PL_OPT;
		if (f->message_type && (f->type == PT_MESSAGE || f->type == PT_GROUP))
			pf[i].msg = &msgdef_shell(pool, f->message_type, todo)->pb;
		else if (f->type == PT_MESSAGE || f->type == PT_GROUP)
			pf[i].type = PT_BYTES; /* unresolved: keep the bytes */
		if (f->type == PT_ENUM && f->enum_type && proto_enum_is_closed(f->enum_type)) {
			int32_t *vals = xalloc(sizeof(int32_t) * (size_t)(f->enum_type->n_values + 1));
			for (int j = 0; j < f->enum_type->n_values; j++)
				vals[j] = f->enum_type->values[j].number;
			pf[i].enum_vals = vals;
			pf[i].n_enum_vals = f->enum_type->n_values;
		}
		if (!proto_field_has_presence(f) && !proto_field_is_repeated(f))
			d->flags[i] |= LPF_IMPLICIT;
		if (proto_field_requires_utf8_validation(f))
			d->flags[i] |= LPF_UTF8;
		if (proto_field_is_required(f) || f->label == LABEL_REQUIRED)
			d->flags[i] |= LPF_REQUIRED;
		if (f->containing_oneof && !f->is_extension)
			d->oneof[i] = f->containing_oneof->index + 1;
		if (!f->is_extension && proto_message_is_map_entry(m))
			d->flags[i] |= LPF_ALWAYS;
	}
}

/* The definition of m in pool (DynamicMessageFactory, or the generated message for the builtin
 * pool): its fields and the extensions of the pool, by number. Built iteratively. */
static const LpMsgDef *lp_msgdef(LpPool *pool, const LpMessage *m)
{
	Vec todo = {0};
	LpMsgDef *d = msgdef_shell(pool, m, &todo);
	while (todo.n)
		msgdef_fill(pool, VEC_AT(todo, LpMsgDef, --todo.n), &todo);
	return d;
}

static const LpMsgDef *lp_msgdef_by_name(LpPool *pool, const char *full_name)
{
	const LpMessage *m = lp_find_message(pool, full_name);
	if (!m)
		fail("libprotoc: no message %s in the pool", full_name);
	return lp_msgdef(pool, m);
}

/* FileDescriptor::CopyTo (public) */
PbMsg *lp_file_to_proto(const LpFile *file) { return lp_file_copy_to(file); }

int lp_field_legacy_ctype(const LpField *f) { return field_legacy_ctype(f); }
