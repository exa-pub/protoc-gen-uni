/* Copyright 2018 The Go Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file. */

/* Ported from google.golang.org/protobuf v1.36.12 compiler/protogen/protogen.go (Options.New,
 * the descriptor wrappers and their naming), protogen_apilevel.go and protogen_opaque.go. Where
 * v1.31.0 differs, the code tests PgOptions.Version (protogen_v1.31.0.c). */
#include "internal.h"

#include <string.h>

static char map_true;
#define MAP_TRUE ((void *)&map_true)

bool pg_ident_eq(PgGoIdent a, PgGoIdent b) { return str_eq(a.GoName, b.GoName) && str_eq(a.GoImportPath, b.GoImportPath); }

/* ---- path (package path of Go) ---- */

/* path.Clean */
COLD const char *pg_path_clean(const char *path)
{
	size_t n = strlen(path);
	if (n == 0)
		return ".";
	bool rooted = path[0] == '/';
	char *out = xalloc(n + 2);
	size_t w = 0, r = 0, dotdot = 0;
	if (rooted) {
		out[w++] = '/';
		r = 1, dotdot = 1;
	}
	while (r < n) {
		if (path[r] == '/') {
			r++; /* empty path element */
		} else if (path[r] == '.' && (r + 1 == n || path[r + 1] == '/')) {
			r++; /* . element */
		} else if (path[r] == '.' && path[r + 1] == '.' && (r + 2 == n || path[r + 2] == '/')) {
			/* .. element: remove to last / */
			r += 2;
			if (w > dotdot) {
				w--;
				while (w > dotdot && out[w] != '/')
					w--;
			} else if (!rooted) {
				if (w > 0)
					out[w++] = '/';
				out[w++] = '.';
				out[w++] = '.';
				dotdot = w;
			}
		} else {
			if ((rooted && w != 1) || (!rooted && w != 0))
				out[w++] = '/';
			for (; r < n && path[r] != '/'; r++)
				out[w++] = path[r];
		}
	}
	if (w == 0)
		return ".";
	out[w] = 0;
	return out;
}

/* path.Base */
COLD const char *pg_path_base(const char *path)
{
	size_t n = strlen(path);
	if (n == 0)
		return ".";
	while (n > 0 && path[n - 1] == '/')
		n--;
	if (n == 0)
		return "/";
	size_t i = n;
	while (i > 0 && path[i - 1] != '/')
		i--;
	return xstrndup(path + i, n - i);
}

/* path.Ext */
static COLD const char *path_ext(const char *path)
{
	for (size_t i = strlen(path); i > 0 && path[i - 1] != '/'; i--)
		if (path[i - 1] == '.')
			return path + i - 1;
	return "";
}

/* path.Dir */
COLD const char *pg_path_dir(const char *path)
{
	const char *slash = strrchr(path, '/');
	return pg_path_clean(slash ? xstrndup(path, (size_t)(slash - path + 1)) : "");
}

/* path.Join of two elements */
COLD const char *pg_path_join(const char *a, const char *b)
{
	if (!*a && !*b)
		return "";
	if (!*a)
		return pg_path_clean(b);
	if (!*b)
		return pg_path_clean(a);
	return pg_path_clean(xcat(a, "/", b));
}

const char *pg_clean_package_name(const char *name) { return strs_go_sanitized(name, strlen(name)); }

/* ---- locations and comments ---- */

PgLocation pg_location_append(PgLocation loc, int32_t num, int32_t idx)
{
	int32_t *p = xalloc(sizeof *p * (size_t)(loc.nPath + 2));
	if (loc.nPath)
		memcpy(p, loc.Path, sizeof *p * (size_t)loc.nPath);
	p[loc.nPath] = num;
	p[loc.nPath + 1] = idx;
	return (PgLocation){loc.SourceFile, p, loc.nPath + 2};
}

static COLD PgCommentSet make_comment_set(PgPlugin *gen, GoSourceLocation loc)
{
	PgCommentSet c = {0};
	if (pg_plugin_internal_strip_for_editions_diff(gen))
		return c;
	c.LeadingDetached = xalloc(sizeof *c.LeadingDetached * (size_t)(loc.n_detached ? loc.n_detached : 1));
	c.nLeadingDetached = loc.n_detached;
	for (int i = 0; i < loc.n_detached; i++)
		c.LeadingDetached[i].s = loc.detached[i];
	c.Leading.s = loc.leading.p ? loc.leading : (Str){"", 0};
	c.Trailing.s = loc.trailing.p ? loc.trailing : (Str){"", 0};
	return c;
}

static COLD PgCommentSet comments_of(PgPlugin *gen, PgFile *f, const GoDesc *d)
{
	return make_comment_set(gen, go_source_location_by_desc(f->Desc, d));
}

PgComments pg_comments_cat(PgComments c, const char *s)
{
	Buf b = {0};
	buf_add_str(&b, c.s);
	buf_adds(&b, s);
	return (PgComments){buf_view(&b).p ? buf_view(&b) : (Str){"", 0}};
}

Str pg_comments_string(PgComments c)
{
	if (!c.s.n)
		return (Str){"", 0};
	Str s = str_trim_suffix_s(c.s, STR_LIT("\n"));
	Buf b = {0};
	size_t start = 0;
	for (size_t i = 0; i <= s.n; i++) {
		if (i == s.n || s.p[i] == '\n') {
			buf_adds(&b, "//");
			buf_add(&b, s.p + start, i - start);
			buf_addc(&b, '\n');
			start = i + 1;
		}
	}
	return buf_view(&b);
}

/* ---- naming ---- */

/* newGoIdent returns the Go identifier for a descriptor. */
static COLD PgGoIdent new_go_ident(PgFile *f, const GoDesc *d)
{
	const char *name = d->full_name;
	const char *pkg = xcat(f->Desc->d.full_name, ".");
	if (has_prefix(name, pkg))
		name += strlen(pkg);
	return pg_ident(f->GoImportPath, strs_go_camel_case(name));
}

/* splitImportPathAndPackageName */
static COLD void split_import_path_and_package_name(const char *s, const char **imp, const char **pkg)
{
	const char *semi = strchr(s, ';');
	if (semi) {
		*imp = xstrndup(s, (size_t)(semi - s));
		*pkg = semi + 1;
	} else {
		*imp = s;
		*pkg = "";
	}
}

/* ---- API level (protogen_apilevel.go) ---- */

static COLD int file_api_level(const GoFile *fd, int def)
{
	int level = def;
	int al = fd->features.APILevel;
	if (al != PG_API_LEVEL_UNSPECIFIED)
		level = al;
	return level;
}

static COLD int message_api_level(const GoMessage *md, int def)
{
	int level = def;
	int al = md->features.APILevel;
	if (al != PG_API_LEVEL_UNSPECIFIED)
		level = al;
	return level;
}

static COLD int default_api_level(const PgPlugin *p)
{
	if (p->opts.DefaultAPILevel != PG_API_LEVEL_UNSPECIFIED)
		return p->opts.DefaultAPILevel;
	return PG_API_OPEN;
}

static COLD const char *field_method_name_prefix(const PgField *f, const char *prefix)
{
	switch (f->Parent->APILevel) {
	case PG_API_OPEN:
		return ""; /* In the Open API, only generate getters (no Has or Clear methods). */
	case PG_API_HYBRID:
		return xcat(prefix, f->hasConflictHybrid ? "_" : "", f->camelCase);
	case PG_API_OPAQUE:
		return xcat(prefix, f->camelCase);
	default:
		fail("BUG: message is neither open, nor hybrid, nor opaque?!");
	}
}

COLD const char *pg_field_method_name(const PgField *f, const char *method, const char **compat)
{
	*compat = NULL;
	if (str_eq(method, "Get")) {
		switch (f->Parent->APILevel) {
		case PG_API_OPEN:
			/* In the Open API, only generate a getter with the old style mangled name. */
			return xcat("Get", f->GoName);
		case PG_API_HYBRID: {
			/* In the Hybrid API, return the mangled getter name and the old style name if
			 * needed, for backwards compatibility with the Open API. */
			const char *orig = xcat("Get", f->hasConflictHybrid ? "_" : "", f->camelCase);
			const char *mangled = xcat("Get", f->GoName);
			if (!str_eq(mangled, orig))
				*compat = mangled;
			return orig;
		}
		case PG_API_OPAQUE:
			return field_method_name_prefix(f, "Get");
		default:
			fail("BUG: message is neither open, nor hybrid, nor opaque?!");
		}
	}
	if (str_eq(method, "Set") || str_eq(method, "Has") || str_eq(method, "Clear"))
		return field_method_name_prefix(f, method);
	fail("Field.MethodName called for unknown method %s", quote_go(str_c(method)));
}

COLD const char *pg_oneof_method_name(const PgOneof *o, const char *method)
{
	if (!str_eq(method, "Has") && !str_eq(method, "Clear") && !str_eq(method, "Which"))
		fail("Oneof.MethodName called for unknown method %s", quote_go(str_c(method)));
	switch (o->Parent->APILevel) {
	case PG_API_OPEN:
		return "";
	case PG_API_HYBRID:
		return xcat(method, o->hasConflictHybrid ? "_" : "", o->camelCase);
	case PG_API_OPAQUE:
		return xcat(method, o->camelCase);
	default:
		fail("BUG: message is neither open, nor hybrid, nor opaque?!");
	}
}

/* ---- protogen_opaque.go ---- */

static COLD void resolve_camel_case_conflict(PgField *f)
{
	const char *suffix = xcat("_", go_itoa(f->Desc->number));
	f->camelCase = xcat(f->camelCase, suffix);
	if (f->Oneof)
		f->Oneof->camelCase = xcat(f->Oneof->camelCase, suffix);
}

/* Finds fields with different names whose camelCase is identical and resolves the conflict by
 * appending a _<fieldnum> suffix. */
static COLD void resolve_camel_case_conflicts(PgMessage *m)
{
	Map camel2field = {0};
	for (int i = 0; i < m->nFields; i++) {
		PgField *f = m->Fields[i];
		PgField *other = map_get(&camel2field, f->camelCase);
		if (other) {
			resolve_camel_case_conflict(other);
			resolve_camel_case_conflict(f);
			/* Assumption: at most two fields can have the same camelCase. */
			continue;
		}
		map_put(&camel2field, f->camelCase, f);
	}
}

static COLD void opaque_new_message_hook(PgMessage *m)
{
	for (int i = 0; i < m->nFields; i++)
		if (str_eq(m->Fields[i]->camelCase, "Build"))
			m->Fields[i]->camelCase = xcat(m->Fields[i]->camelCase, "_");
	resolve_camel_case_conflicts(m);
	Map camel_cases = {0};
	for (int i = 0; i < m->nFields; i++) {
		PgField *f = m->Fields[i];
		if (f->Oneof)
			map_put(&camel_cases, f->Oneof->camelCase, MAP_TRUE);
		map_put(&camel_cases, f->camelCase, MAP_TRUE);
	}
	for (int i = 0; i < m->nFields; i++) {
		PgField *f = m->Fields[i];
		static const char *const methods[] = {"Set", "Get", "Has", "Clear"};
		int nm = go_field_has_presence(f->Desc) ? 4 : 2;
		for (int k = 0; k < nm; k++)
			if (map_get(&camel_cases, xcat(methods[k], f->camelCase)))
				f->hasConflictHybrid = true;
	}
	for (int i = 0; i < m->nFields; i++) {
		PgField *f = m->Fields[i];
		if (!f->Oneof)
			continue;
		static const char *const methods[] = {"Has", "Clear", "Which"};
		for (int k = 0; k < 3; k++)
			if (map_get(&camel_cases, xcat(methods[k], f->Oneof->camelCase)))
				f->Oneof->hasConflictHybrid = true;
	}
}

/* ---- descriptor wrappers ---- */

static PgEnum *new_enum(PgPlugin *gen, PgFile *f, PgMessage *parent, GoEnum *desc);
static PgMessage *new_message(PgPlugin *gen, PgFile *f, PgMessage *parent, GoMessage *desc);
static PgField *new_field(PgPlugin *gen, PgFile *f, PgMessage *message, PgOneof *oneof, GoField *desc);

static COLD PgEnumValue *new_enum_value(PgPlugin *gen, PgFile *f, PgMessage *message, PgEnum *e, GoEnumValue *desc)
{
	/* A top-level enum value's name is: EnumName_ValueName; an enum value contained in a
	 * message is: MessageName_ValueName. For historical reasons, enum value names are not
	 * camel-cased. */
	PgGoIdent parent_ident = message ? message->GoIdent : e->GoIdent;
	const char *name = xcat(parent_ident.GoName, "_", desc->d.name);
	const char *prefixed_name = NULL;
	PgLocation loc = pg_location_append(e->Location, GENID_EnumDescriptorProto_Value_field_number, desc->d.index);
	if (PG_V1_36(gen->opts.Version)) { /* v1.31.0: no strip_enum_prefix */
		Buf pb = {0};
		for (const char *s = e->Desc->d.name; *s; s++)
			if (*s != '_')
				buf_addc(&pb, *s >= 'A' && *s <= 'Z' ? (char)(*s + 32) : *s);
		const char *prefix = buf_str(&pb);
		/* Start with the StripEnumPrefix of the enum descriptor, then override it with the
		 * StripEnumPrefix of the enum value descriptor, if any. */
		int sep = e->Desc->features.StripEnumPrefix;
		const PbMsg *evof = pb_msg(desc->d.options, GENID_EnumValueOptions_Features_field_number);
		const PbMsg *gf = pb_msg(evof, GENID_FeatureSet_Go_ext_number);
		if (gf && pb_has(gf, GENID_GoFeatures_StripEnumPrefix_field_number))
			sep = (int)pb_int(gf, GENID_GoFeatures_StripEnumPrefix_field_number, 0);
		switch (sep) {
		case GENID_GoFeatures_STRIP_ENUM_PREFIX_KEEP_enum_value:
			break; /* keep long name */
		case GENID_GoFeatures_STRIP_ENUM_PREFIX_STRIP_enum_value:
			name = xcat(parent_ident.GoName, "_", strs_trim_enum_prefix(desc->d.name, prefix));
			break;
		case GENID_GoFeatures_STRIP_ENUM_PREFIX_GENERATE_BOTH_enum_value:
			prefixed_name = name;
			name = xcat(parent_ident.GoName, "_", strs_trim_enum_prefix(desc->d.name, prefix));
			break;
		}
	}
	PgEnumValue *ev = xalloc(sizeof *ev);
	ev->Desc = desc;
	ev->GoIdent = pg_ident(f->GoImportPath, name);
	ev->Parent = e;
	ev->Location = loc;
	ev->Comments = comments_of(gen, f, &desc->d);
	if (prefixed_name && *prefixed_name)
		ev->PrefixedAlias = pg_ident(f->GoImportPath, prefixed_name);
	else
		ev->PrefixedAlias = pg_ident("", "");
	desc->d.user = ev;
	return ev;
}

static COLD PgEnum *new_enum(PgPlugin *gen, PgFile *f, PgMessage *parent, GoEnum *desc)
{
	PgEnum *e = xalloc(sizeof *e);
	if (parent)
		e->Location = pg_location_append(parent->Location, GENID_DescriptorProto_EnumType_field_number, desc->d.index);
	else
		e->Location = pg_location_append(f->location, GENID_FileDescriptorProto_EnumType_field_number, desc->d.index);
	e->Desc = desc;
	e->GoIdent = new_go_ident(f, &desc->d);
	e->Comments = comments_of(gen, f, &desc->d);
	desc->d.user = e;
	map_put(&gen->enumsByName, desc->d.full_name, e);
	e->Values = xalloc(sizeof *e->Values * (size_t)(desc->n_values ? desc->n_values : 1));
	e->nValues = desc->n_values;
	for (int i = 0; i < desc->n_values; i++)
		e->Values[i] = new_enum_value(gen, f, parent, e, desc->values[i]);
	return e;
}

static COLD PgOneof *new_oneof(PgPlugin *gen, PgFile *f, PgMessage *message, GoOneof *desc)
{
	PgOneof *o = xalloc(sizeof *o);
	o->Location = pg_location_append(message->Location, GENID_DescriptorProto_OneofDecl_field_number, desc->d.index);
	const char *camel = strs_go_camel_case(desc->d.name);
	o->Desc = desc;
	o->Parent = message;
	o->GoName = camel;
	o->GoIdent = pg_ident(f->GoImportPath, xcat(message->GoIdent.GoName, "_", camel));
	o->Comments = comments_of(gen, f, &desc->d);
	o->Fields = xalloc(sizeof *o->Fields * (size_t)(desc->n_fields ? desc->n_fields : 1));
	o->camelCase = strs_go_camel_case(desc->d.name); /* opaqueNewOneofHook */
	desc->d.user = o;
	return o;
}

static COLD PgField *new_field(PgPlugin *gen, PgFile *f, PgMessage *message, PgOneof *oneof, GoField *desc)
{
	PgField *field = xalloc(sizeof *field);
	if (desc->is_extension && !message)
		field->Location = pg_location_append(f->location, GENID_FileDescriptorProto_Extension_field_number, desc->d.index);
	else if (desc->is_extension)
		field->Location = pg_location_append(message->Location, GENID_DescriptorProto_Extension_field_number, desc->d.index);
	else
		field->Location = pg_location_append(message->Location, GENID_DescriptorProto_Field_field_number, desc->d.index);
	(void)oneof;
	const char *camel = strs_go_camel_case(desc->d.name);
	field->Desc = desc;
	field->GoName = camel;
	field->GoIdent = pg_ident(f->GoImportPath, message ? xcat(message->GoIdent.GoName, "_", camel) : camel);
	field->Parent = message;
	field->Comments = comments_of(gen, f, &desc->d);
	field->camelCase = strs_go_camel_case(desc->d.name); /* opaqueNewFieldHook */
	desc->d.user = field;
	return field;
}

/* makeNameUnique of newMessage: usedNames maps a name to whether it is used (MAP_TRUE). */
static COLD const char *make_name_unique(Map *used, const char *name, bool has_getter)
{
	while (map_get(used, name) || (has_getter && map_get(used, xcat("Get", name))))
		name = xcat(name, "_");
	map_put(used, name, MAP_TRUE);
	map_put(used, xcat("Get", name), has_getter ? MAP_TRUE : NULL);
	return name;
}

static COLD PgMessage *new_message(PgPlugin *gen, PgFile *f, PgMessage *parent, GoMessage *desc)
{
	PgMessage *m = xalloc(sizeof *m);
	if (parent)
		m->Location = pg_location_append(parent->Location, GENID_DescriptorProto_NestedType_field_number, desc->d.index);
	else
		m->Location = pg_location_append(f->location, GENID_FileDescriptorProto_MessageType_field_number, desc->d.index);
	/* editions feature semantics: applies to nested messages */
	int def = parent ? parent->APILevel : f->APILevel;
	m->Desc = desc;
	m->GoIdent = new_go_ident(f, &desc->d);
	m->Comments = comments_of(gen, f, &desc->d);
	m->APILevel = message_api_level(desc, def);
	desc->d.user = m;
	map_put(&gen->messagesByName, desc->d.full_name, m);
	m->Enums = xalloc(sizeof *m->Enums * (size_t)(desc->n_enums ? desc->n_enums : 1));
	m->nEnums = desc->n_enums;
	for (int i = 0; i < desc->n_enums; i++)
		m->Enums[i] = new_enum(gen, f, m, desc->enums[i]);
	m->Messages = xalloc(sizeof *m->Messages * (size_t)(desc->n_messages ? desc->n_messages : 1));
	m->nMessages = desc->n_messages;
	for (int i = 0; i < desc->n_messages; i++)
		m->Messages[i] = new_message(gen, f, m, desc->messages[i]);
	m->Oneofs = xalloc(sizeof *m->Oneofs * (size_t)(desc->n_oneofs ? desc->n_oneofs : 1));
	m->nOneofs = desc->n_oneofs;
	for (int i = 0; i < desc->n_oneofs; i++)
		m->Oneofs[i] = new_oneof(gen, f, m, desc->oneofs[i]);
	m->Fields = xalloc(sizeof *m->Fields * (size_t)(desc->n_fields ? desc->n_fields : 1));
	m->nFields = desc->n_fields;
	for (int i = 0; i < desc->n_fields; i++) {
		GoField *fd = desc->fields[i];
		PgOneof *oneof = fd->oneof ? m->Oneofs[fd->oneof->d.index] : NULL;
		m->Fields[i] = new_field(gen, f, m, oneof, fd);
	}
	m->Extensions = xalloc(sizeof *m->Extensions * (size_t)(desc->n_extensions ? desc->n_extensions : 1));
	m->nExtensions = desc->n_extensions;
	for (int i = 0; i < desc->n_extensions; i++)
		m->Extensions[i] = new_field(gen, f, m, NULL, desc->extensions[i]);

	/* Resolve local references between fields and oneofs. */
	for (int i = 0; i < m->nFields; i++) {
		PgField *field = m->Fields[i];
		if (field->Desc->oneof) {
			PgOneof *oneof = m->Oneofs[field->Desc->oneof->d.index];
			field->Oneof = oneof;
			oneof->Fields[oneof->nFields++] = field;
		}
	}

	/* Field name conflict resolution: well-known method names and a Get* method per field;
	 * for each field in turn, add _s to its name until there are no conflicts. */
	Map used = {0};
	static const char *const reserved[] = {"Reset", "String", "ProtoMessage", "Marshal", "Unmarshal",
					       "ExtensionRangeArray", "ExtensionMap", "Descriptor"};
	for (int i = 0; i < (int)(sizeof reserved / sizeof *reserved); i++)
		map_put(&used, reserved[i], MAP_TRUE);
	for (int i = 0; i < m->nFields; i++) {
		PgField *field = m->Fields[i];
		field->GoName = make_name_unique(&used, field->GoName, true);
		field->GoIdent.GoName = xcat(m->GoIdent.GoName, "_", field->GoName);
		if (field->Oneof && field->Oneof->Fields[0] == field) {
			/* Make the name for a oneof unique as well. For historical reasons, this
			 * assumes that a getter method is not generated for oneofs. */
			field->Oneof->GoName = make_name_unique(&used, field->Oneof->GoName, false);
			field->Oneof->GoIdent.GoName = xcat(m->GoIdent.GoName, "_", field->Oneof->GoName);
		}
	}

	/* Oneof field name conflict resolution (incomplete: other oneof field types are not
	 * considered). */
	for (int i = 0; i < m->nFields; i++) {
		PgField *field = m->Fields[i];
		if (!field->Oneof)
			continue;
		for (bool again = true; again;) {
			again = false;
			for (int k = 0; k < m->nMessages && !again; k++)
				if (pg_ident_eq(m->Messages[k]->GoIdent, field->GoIdent))
					field->GoIdent.GoName = xcat(field->GoIdent.GoName, "_"), again = true;
			for (int k = 0; k < m->nEnums && !again; k++)
				if (pg_ident_eq(m->Enums[k]->GoIdent, field->GoIdent))
					field->GoIdent.GoName = xcat(field->GoIdent.GoName, "_"), again = true;
		}
	}

	opaque_new_message_hook(m);
	return m;
}

static COLD const char *field_resolve_dependencies(PgPlugin *gen, PgField *field)
{
	GoField *desc = field->Desc;
	switch (desc->kind) {
	case PT_ENUM: {
		const char *name = desc->enum_type->d.full_name;
		PgEnum *e = map_get(&gen->enumsByName, name);
		if (!e)
			return xcat("field ", desc->d.full_name, ": no descriptor for enum ", name);
		field->Enum = e;
		break;
	}
	case PT_MESSAGE:
	case PT_GROUP: {
		const char *name = desc->message_type->d.full_name;
		PgMessage *m = map_get(&gen->messagesByName, name);
		if (!m)
			return xcat("field ", desc->d.full_name, ": no descriptor for type ", name);
		field->Message = m;
		break;
	}
	}
	if (desc->is_extension) {
		const char *name = desc->containing->d.full_name;
		PgMessage *m = map_get(&gen->messagesByName, name);
		if (!m)
			return xcat("field ", desc->d.full_name, ": no descriptor for type ", name);
		field->Extendee = m;
	}
	return NULL;
}

static COLD const char *message_resolve_dependencies(PgPlugin *gen, PgMessage *m)
{
	const char *err;
	for (int i = 0; i < m->nFields; i++)
		if ((err = field_resolve_dependencies(gen, m->Fields[i])))
			return err;
	for (int i = 0; i < m->nMessages; i++)
		if ((err = message_resolve_dependencies(gen, m->Messages[i])))
			return err;
	for (int i = 0; i < m->nExtensions; i++)
		if ((err = field_resolve_dependencies(gen, m->Extensions[i])))
			return err;
	return NULL;
}

static COLD const char *method_resolve_dependencies(PgPlugin *gen, PgMethod *method)
{
	GoMethod *desc = method->Desc;
	const char *in_name = desc->input->d.full_name;
	PgMessage *in = map_get(&gen->messagesByName, in_name);
	if (!in)
		return xcat("method ", desc->d.full_name, ": no descriptor for type ", in_name);
	method->Input = in;
	const char *out_name = desc->output->d.full_name;
	PgMessage *out = map_get(&gen->messagesByName, out_name);
	if (!out)
		return xcat("method ", desc->d.full_name, ": no descriptor for type ", out_name);
	method->Output = out;
	return NULL;
}

static COLD PgService *new_service(PgPlugin *gen, PgFile *f, GoService *desc)
{
	PgService *s = xalloc(sizeof *s);
	s->Location = pg_location_append(f->location, GENID_FileDescriptorProto_Service_field_number, desc->d.index);
	s->Desc = desc;
	s->GoName = strs_go_camel_case(desc->d.name);
	s->Comments = comments_of(gen, f, &desc->d);
	desc->d.user = s;
	s->Methods = xalloc(sizeof *s->Methods * (size_t)(desc->n_methods ? desc->n_methods : 1));
	s->nMethods = desc->n_methods;
	for (int i = 0; i < desc->n_methods; i++) {
		GoMethod *md = desc->methods[i];
		PgMethod *m = s->Methods[i] = xalloc(sizeof *m);
		m->Location = pg_location_append(s->Location, GENID_ServiceDescriptorProto_Method_field_number, md->d.index);
		m->Desc = md;
		m->GoName = strs_go_camel_case(md->d.name);
		m->Parent = s;
		m->Comments = comments_of(gen, f, &md->d);
		md->d.user = m;
	}
	return s;
}

/* defaultFeatures: the edition checks against the compiled-in defaults (PROTO2..2024). */
static COLD const char *default_features_check(const GoFile *fd)
{
	int edition;
	PbMsg *p = fd->proto;
	Str syntax = pb_bytes(p, GENID_FileDescriptorProto_Syntax_field_number);
	if (str_eqc(syntax, "editions"))
		edition = (int)pb_int(p, GENID_FileDescriptorProto_Edition_field_number, 0);
	else if (str_eqc(syntax, "proto3"))
		edition = GENID_Edition_EDITION_PROTO3;
	else
		edition = GENID_Edition_EDITION_PROTO2;
	if (edition < FEATURES_MINIMUM_EDITION)
		return xcat("edition ", go_edition_string(edition), " is lower than the minimum supported edition ",
			    go_edition_string(FEATURES_MINIMUM_EDITION));
	if (edition > FEATURES_MAXIMUM_EDITION && edition != GENID_Edition_EDITION_UNSTABLE)
		return xcat("edition ", go_edition_string(edition), " is greater than the maximum supported edition ",
			    go_edition_string(FEATURES_MAXIMUM_EDITION));
	return NULL;
}

static COLD const char *new_file(PgPlugin *gen, PbMsg *p, const char *package_name, const char *import_path, int api_level,
			    PgFile **out)
{
	const char *err;
	if (gen->opts.Version && (err = gen->opts.Version->check_file(p)))
		return err;
	GoFile *desc = go_registry_new_file(gen->fileReg, p, &err);
	if (!desc)
		return gen->opts.Version ? gen->opts.Version->new_file_error(err) : err;
	int def = default_api_level(gen);
	if (api_level != PG_API_LEVEL_UNSPECIFIED)
		def = api_level;
	if ((err = default_features_check(desc)))
		return err;
	if (gen->opts.Version && (err = gen->opts.Version->validate_file(desc)))
		return err;
	PgFile *f = xalloc(sizeof *f);
	f->Desc = desc;
	f->Proto = p;
	f->GoPackageName = package_name;
	f->GoImportPath = import_path;
	f->location = (PgLocation){desc->d.name, NULL, 0};
	f->APILevel = file_api_level(desc, def);
	desc->d.user = f;

	/* Determine the prefix for generated Go files. */
	const char *prefix = desc->d.name;
	const char *ext = path_ext(prefix);
	if (str_eq(ext, ".proto") || str_eq(ext, ".protodevel"))
		prefix = xstrndup(prefix, strlen(prefix) - strlen(ext));
	if (gen->pathType == PATH_TYPE_IMPORT) {
		/* If paths=import, the output filename is derived from the Go import path. */
		prefix = pg_path_join(f->GoImportPath, pg_path_base(prefix));
	}
	f->GoDescriptorIdent = pg_ident(f->GoImportPath, xcat("File_", strs_go_sanitized(desc->d.name, strlen(desc->d.name))));
	f->GeneratedFilenamePrefix = prefix;

	f->Enums = xalloc(sizeof *f->Enums * (size_t)(desc->n_enums ? desc->n_enums : 1));
	f->nEnums = desc->n_enums;
	for (int i = 0; i < desc->n_enums; i++)
		f->Enums[i] = new_enum(gen, f, NULL, desc->enums[i]);
	f->Messages = xalloc(sizeof *f->Messages * (size_t)(desc->n_messages ? desc->n_messages : 1));
	f->nMessages = desc->n_messages;
	for (int i = 0; i < desc->n_messages; i++)
		f->Messages[i] = new_message(gen, f, NULL, desc->messages[i]);
	f->Extensions = xalloc(sizeof *f->Extensions * (size_t)(desc->n_extensions ? desc->n_extensions : 1));
	f->nExtensions = desc->n_extensions;
	for (int i = 0; i < desc->n_extensions; i++)
		f->Extensions[i] = new_field(gen, f, NULL, NULL, desc->extensions[i]);
	f->Services = xalloc(sizeof *f->Services * (size_t)(desc->n_services ? desc->n_services : 1));
	f->nServices = desc->n_services;
	for (int i = 0; i < desc->n_services; i++)
		f->Services[i] = new_service(gen, f, desc->services[i]);
	for (int i = 0; i < f->nMessages; i++)
		if ((err = message_resolve_dependencies(gen, f->Messages[i])))
			return err;
	for (int i = 0; i < f->nExtensions; i++)
		if ((err = field_resolve_dependencies(gen, f->Extensions[i])))
			return err;
	for (int i = 0; i < f->nServices; i++)
		for (int j = 0; j < f->Services[i]->nMethods; j++)
			if ((err = method_resolve_dependencies(gen, f->Services[i]->Methods[j])))
				return err;
	*out = f;
	return NULL;
}

/* ---- Options.New ---- */

/* gatherTransitiveDependencies: FileToGenerate and its transitive dependencies in topological
 * order, or every ProtoFile without files to generate. */
static COLD void td_add(Map *files, Map *deps, Vec *sorted, const char *name)
{
	if (map_get(deps, name))
		return;
	PbMsg *f = map_get(files, name);
	if (!f)
		return; /* This shouldn't happen, but will fail later if it does. */
	map_put(deps, name, MAP_TRUE);
	for (int i = 0; i < pb_count(f, GENID_FileDescriptorProto_Dependency_field_number); i++)
		td_add(files, deps, sorted, pb_str_at(f, GENID_FileDescriptorProto_Dependency_field_number, i));
	vec_push(sorted, f);
}

static COLD Vec gather_transitive_dependencies(PbMsg *req, bool all)
{
	Vec out = {0};
	int nf = pb_count(req, GENID_CodeGeneratorRequest_ProtoFile_field_number);
	if (all || !pb_count(req, GENID_CodeGeneratorRequest_FileToGenerate_field_number)) {
		for (int i = 0; i < nf; i++)
			vec_push(&out, pb_msg_at(req, GENID_CodeGeneratorRequest_ProtoFile_field_number, i));
		return out;
	}
	Map files = {0}, deps = {0};
	for (int i = 0; i < nf; i++) {
		PbMsg *f = pb_msg_at(req, GENID_CodeGeneratorRequest_ProtoFile_field_number, i);
		map_put(&files, pb_str(f, GENID_FileDescriptorProto_Name_field_number), f);
	}
	for (int i = 0; i < pb_count(req, GENID_CodeGeneratorRequest_FileToGenerate_field_number); i++)
		td_add(&files, &deps, &out, pb_str_at(req, GENID_CodeGeneratorRequest_FileToGenerate_field_number, i));
	return out;
}

static COLD int parse_api_level(const char *value)
{
	if (str_eq(value, "API_OPEN"))
		return PG_API_OPEN;
	if (str_eq(value, "API_HYBRID"))
		return PG_API_HYBRID;
	if (str_eq(value, "API_OPAQUE"))
		return PG_API_OPAQUE;
	return -1;
}

/* An ordered string map (Go map iteration replaced by insertion order where it matters). */
typedef struct {
	Map m;
	Vec keys;
} OMap;

static COLD void omap_put(OMap *o, const char *k, void *v)
{
	if (!map_has(&o->m, k))
		vec_push(&o->keys, (void *)k);
	map_put(&o->m, k, v);
}

#define GO_PACKAGE_DOC_URL "https://protobuf.dev/reference/go/go-generated#package"

/* The errors of Options.New may contain any bytes of the parameter or the request. */
#define ERR(s) str_c(s)
static const Str no_err = {NULL, 0};

static COLD Str options_new(PgPlugin *gen, PgOptions opts)
{
	PbMsg *req = gen->Request;
	OMap package_names = {0}, import_paths = {0};
	Map api_level = {0};

	Str param = pb_bytes(req, GENID_CodeGeneratorRequest_Parameter_field_number);
	Str *elems;
	int n = str_split_s(param, ',', &elems);
	for (int i = 0; i < n; i++) {
		Str e = elems[i];
		const char *eq = memchr(e.p, '=', e.n);
		Str ps = {e.p, eq ? (size_t)(eq - e.p) : e.n};
		Str vs = eq ? (Str){eq + 1, e.n - ps.n - 1} : (Str){"", 0};
		/* names and values without NUL bytes as C strings (a NUL never matches) */
		const char *p = memchr(ps.p, 0, ps.n) ? "\x01" : xstrndup(ps.p, ps.n);
		const char *value = xstrndup(vs.p, vs.n);
		if (!ps.n)
			continue; /* Ignore. */
		if (str_eq(p, "module")) {
			gen->module = (Str){value, vs.n};
		} else if (str_eq(p, "paths")) {
			if (str_eqc(vs, "import"))
				gen->pathType = PATH_TYPE_IMPORT;
			else if (str_eqc(vs, "source_relative"))
				gen->pathType = PATH_TYPE_SOURCE_RELATIVE;
			else
				return ERR(xcat("unknown path type ", quote_go(vs), ": want \"import\" or \"source_relative\""));
		} else if (str_eq(p, "annotate_code")) {
			if (str_eqc(vs, "true") || !vs.n)
				gen->annotateCode = true;
			else if (!str_eqc(vs, "false"))
				return ERR(xcat("bad value for parameter ", quote_go(ps), ": want \"true\" or \"false\""));
		} else if (PG_V1_36(opts.Version) && str_eq(p, "default_api_level")) { /* v1.31.0: ParamFunc */
			int level = memchr(vs.p, 0, vs.n) ? -1 : parse_api_level(value);
			if (level < 0)
				return ERR(xcat("unknown API level ", quote_go(vs), " for parameter ", quote_go(ps),
						": want \"API_OPEN\", \"API_HYBRID\" or \"API_OPAQUE\""));
			opts.DefaultAPILevel = level;
			gen->opts = opts;
		} else if (ps.p[0] == 'M') {
			const char *imp, *pkg;
			split_import_path_and_package_name(value, &imp, &pkg);
			const char *file = xstrndup(ps.p + 1, ps.n - 1);
			if (*pkg)
				omap_put(&package_names, file, (void *)pkg);
			if (*imp)
				omap_put(&import_paths, file, (void *)imp);
		} else if (PG_V1_36(opts.Version) && str_has_prefix(ps, STR_LIT("apilevelM"))) {
			int level = memchr(vs.p, 0, vs.n) ? -1 : parse_api_level(value);
			if (level < 0)
				return ERR(xcat("unknown API level ", quote_go(vs), " for parameter ", quote_go(ps),
						": want \"API_OPEN\", \"API_HYBRID\" or \"API_OPAQUE\""));
			map_put(&api_level, xstrndup(ps.p + 9, ps.n - 9), (void *)(intptr_t)level);
		} else if (opts.ParamFunc) {
			Str err = opts.ParamFunc(opts.ParamCtx, ps, vs);
			if (err.p)
				return err;
		}
	}

	/* When the module= option is provided, we strip the module name prefix from generated
	 * files. This only makes sense if generated filenames are based on the import path. */
	if (gen->module.n && gen->pathType == PATH_TYPE_SOURCE_RELATIVE)
		return ERR("cannot use module= with paths=source_relative");

	/* v1.31.0, v1.36.7: every ProtoFile */
	Vec files = gather_transitive_dependencies(req, opts.Version != NULL);

	/* Figure out the import path and package name for each file. */
	for (int i = 0; i < files.n; i++) {
		PbMsg *fdesc = files.v[i];
		const char *filename = pb_str(fdesc, GENID_FileDescriptorProto_Name_field_number);
		const char *imp, *pkg;
		split_import_path_and_package_name(
			pb_str(pb_msg(fdesc, GENID_FileDescriptorProto_Options_field_number), GENID_FileOptions_GoPackage_field_number), &imp,
			&pkg);
		const char *cur_imp = map_get(&import_paths.m, filename), *cur_pkg = map_get(&package_names.m, filename);
		if ((!cur_imp || !*cur_imp) && *imp)
			omap_put(&import_paths, filename, (void *)imp), cur_imp = imp;
		if ((!cur_pkg || !*cur_pkg) && *pkg)
			omap_put(&package_names, filename, (void *)pkg), cur_pkg = pkg;
		if (!cur_imp || !*cur_imp) {
			/* The import path must be specified one way or another. */
			return ERR(xcat("unable to determine Go import path for ", quote_go(str_c(filename)),
				    "\n\nPlease specify either:\n\t\xe2\x80\xa2 a \"go_package\" option in the .proto source file, or\n"
				    "\t\xe2\x80\xa2 a \"M\" argument on the command line.\n\nSee ",
				    GO_PACKAGE_DOC_URL, " for more information.\n"));
		}
		if (!strchr(cur_imp, '.') && !strchr(cur_imp, '/')) {
			/* Check that import paths contain at least a dot or slash to avoid a common
			 * mistake where import path is confused with package name. */
			return ERR(xcat("invalid Go import path ", quote_go(str_c(cur_imp)), " for ", quote_go(str_c(filename)),
				    "\n\nThe import path must contain at least one period ('.') or forward slash ('/') "
				    "character.\n\nSee ",
				    GO_PACKAGE_DOC_URL, " for more information.\n"));
		}
		if (!cur_pkg || !*cur_pkg) {
			/* If the package name is not explicitly specified, then derive a reasonable
			 * package name from the import path (from go_package first, then the M flag). */
			if (!*imp)
				imp = cur_imp;
			omap_put(&package_names, filename, (void *)pg_clean_package_name(pg_path_base(imp)));
		}
	}

	/* Consistency check: Every file with the same Go import path should have the same Go
	 * package name. */
	OMap package_files = {0};
	for (int i = 0; i < import_paths.keys.n; i++) {
		const char *filename = import_paths.keys.v[i];
		if (!map_has(&package_names.m, filename))
			continue; /* M<file>=<import_path> of a file not in the request */
		const char *ip = map_get(&import_paths.m, filename);
		Vec *v = map_get(&package_files.m, ip);
		if (!v) {
			v = xalloc(sizeof *v);
			omap_put(&package_files, ip, v);
		}
		vec_push(v, (void *)filename);
	}
	for (int i = 0; i < package_files.keys.n; i++) {
		const char *ip = package_files.keys.v[i];
		Vec *filenames = map_get(&package_files.m, ip);
		for (int k = 1; k < filenames->n; k++) {
			const char *a = map_get(&package_names.m, filenames->v[0]), *b = map_get(&package_names.m, filenames->v[k]);
			if (!str_eq(a, b))
				return ERR(xcat("Go package ", quote_go(str_c(ip)), " has inconsistent names ", a, " (", (const char *)filenames->v[0], ") and ",
					    b, " (", (const char *)filenames->v[k], ")"));
		}
	}

	/* The extracted types from the full import set */
	Vec novel = {0};
	for (int i = 0; i < files.n; i++) {
		PbMsg *fdesc = files.v[i];
		const char *filename = pb_str(fdesc, GENID_FileDescriptorProto_Name_field_number);
		if (map_get(&gen->FilesByPath, filename))
			return ERR(xcat("duplicate file name: ", quote_go(str_c(filename))));
		const char *pkg = map_get(&package_names.m, filename), *imp = map_get(&import_paths.m, filename);
		PgFile *f = NULL;
		const char *err = new_file(gen, fdesc, pkg ? pkg : "", imp ? imp : "", (int)(intptr_t)map_get(&api_level, filename), &f);
		if (err)
			return ERR(err);
		gen->Files = xrealloc(gen->Files, sizeof *gen->Files * (size_t)gen->nFiles, sizeof *gen->Files * (size_t)(gen->nFiles + 1));
		gen->Files[gen->nFiles++] = f;
		map_put(&gen->FilesByPath, filename, f);
		/* registerAllExtensionsFromFile: the top-level extensions only */
		for (int k = 0; k < f->Desc->n_extensions; k++) {
			GoField *xd = f->Desc->extensions[k];
			/* already in protoregistry.GlobalTypes: pb.go (v1.31.0: none) and the plugin's */
			bool linked = PG_V1_36(gen->opts.Version) && str_eq(xd->d.full_name, "pb.go");
			for (const char *const *l = gen->opts.LinkedExtensions; l && *l && !linked; l++)
				linked = str_eq(xd->d.full_name, *l);
			if (linked)
				continue;
			for (int j = 0; j < novel.n; j++) {
				GoField *prev = novel.v[j];
				if (prev->number == xd->number && prev->containing == xd->containing)
					return ERR(go_err("extension number %d is already registered on message %s", xd->number,
						      xd->containing->d.full_name));
			}
			vec_push(&novel, xd);
		}
	}
	for (int i = 0; i < pb_count(req, GENID_CodeGeneratorRequest_FileToGenerate_field_number); i++) {
		Str name = pb_bytes_at(req, GENID_CodeGeneratorRequest_FileToGenerate_field_number, i);
		PgFile *f = memchr(name.p, 0, name.n) ? NULL : map_get(&gen->FilesByPath, name.p);
		if (!f) {
			Buf b = {0};
			buf_adds(&b, "no descriptor for generated file: ");
			buf_add_str(&b, name);
			return buf_view(&b);
		}
		f->Generate = true;
	}

	/* Create fully-linked descriptors if new extensions were found */
	if (novel.n) {
		GoSchema *s = gopb_schema_with_extensions((GoField *const *)novel.v, novel.n);
		const GoMsgDef *fdp = gopb_schema_msg(s, "google.protobuf.FileDescriptorProto");
		for (int i = 0; i < gen->nFiles; i++) {
			PgFile *f = gen->Files[i];
			Buf b = {0};
			gopb_encode(f->Proto, &b);
			PbMsg *p = pb_new(&fdp->pb);
			const char *err = gopb_decode(p, (const uint8_t *)b.p, b.n);
			if (err)
				return ERR(err);
			f->Proto = p;
		}
	}
	return no_err;
}

typedef struct {
	PgPlugin *gen;
	PgOptions opts;
	Str err;
} NewCtx;

static COLD void run_new(void *arg)
{
	NewCtx *c = arg;
	c->err = options_new(c->gen, c->opts);
}

COLD PbMsg *pg_parse_request(const uint8_t *req, size_t n, const char **err)
{
	const GoMsgDef *rd = gopb_schema_msg(gopb_schema_base(), "google.protobuf.compiler.CodeGeneratorRequest");
	PbMsg *m = pb_new(&rd->pb);
	*err = gopb_decode(m, req, n);
	return *err ? NULL : m;
}

static bool has_nul(Str s) { return s.n && memchr(s.p, 0, s.n); }

COLD const char *pg_unsupported(const PbMsg *req)
{
#define UNSUPPORTED "unsupported by protoc-gen-uni: "
	enum { FILE_OPTIONS_GO_PACKAGE = 11 };
	for (int i = 0; i < pb_count(req, CGR_PROTO_FILE); i++) {
		const PbMsg *fd = pb_msg_at(req, CGR_PROTO_FILE, i);
		Str name = pb_bytes(fd, FDP_NAME);
		if (has_nul(name))
			return xcat(UNSUPPORTED "NUL byte in file name ", quote_go(name));
		for (int j = 0; j < pb_count(fd, FDP_DEPENDENCY); j++) {
			Str dep = pb_bytes_at(fd, FDP_DEPENDENCY, j);
			if (has_nul(dep))
				return xcat(UNSUPPORTED "NUL byte in file name ", quote_go(dep));
		}
		Str pkg = pb_bytes(pb_msg(fd, FDP_OPTIONS), FILE_OPTIONS_GO_PACKAGE);
		if (has_nul(pkg))
			return xcat(UNSUPPORTED "NUL byte in go_package ", quote_go(pkg), " of ", quote_go(name));
	}
	/* the parameter elements "M<file>=<import path>" */
	Str *params;
	int n = str_split_s(pb_bytes(req, CGR_PARAMETER), ',', &params);
	for (int i = 0; i < n; i++)
		if (params[i].n && params[i].p[0] == 'M' && has_nul(params[i]))
			return xcat(UNSUPPORTED "NUL byte in parameter ", quote_go(params[i]));
	return NULL;
#undef UNSUPPORTED
}

COLD PgPlugin *pg_options_new(const PgOptions *opts, PbMsg *req, Str *err)
{
	PgPlugin *gen = xalloc(sizeof *gen);
	gen->opts = *opts;
	gen->fileReg = go_registry_new();
	gen->module = (Str){"", 0};
	gen->Request = req;
	NewCtx c = {gen, *opts, {NULL, 0}};
	if (try_run(run_new, &c))
		c.err = fail_message();
	*err = c.err;
	return err->p ? NULL : gen;
}

void pg_plugin_error(PgPlugin *gen, Str err)
{
	if (!gen->has_err) {
		gen->has_err = true;
		gen->err = err;
	}
}

COLD bool pg_plugin_internal_strip_for_editions_diff(const PgPlugin *gen)
{
	return gen->opts.InternalStripForEditionsDiff && *gen->opts.InternalStripForEditionsDiff;
}
