/* Copyright (c) 2014 - 2016 Apple Inc. and the project authors
 * Licensed under Apache License v2.0 with Runtime Library Exception
 *
 * See LICENSE.txt for license information:
 * https://github.com/apple/swift-protobuf/blob/main/LICENSE.txt */

/* Port of Sources/protoc-gen-swift/ExtensionSetGenerator.swift. */

typedef struct {
	Gen *g;
	SwField *fd;
	Str comments;
	char *containing_type_swift_full_name, *swift_full_extension_name;
	int offset; /* position in ExtensionSet.extensions */
} ExtensionGen;

struct ExtensionSet {
	Gen *g;
	Vec extensions; /* ExtensionGen *, in the order they were added */
};

static const char *extension_field_type(const ExtensionGen *e)
{
	const SwField *f = e->fd;
	const char *label;
	if (sw_field_is_required(f))
		label = "Required";
	else if (sw_field_is_repeated(f))
		label = sw_field_is_packed(f) ? "Packed" : "Repeated";
	else
		label = "Optional";
	const char *modifier = f->type == PT_GROUP ? "Group" : f->type == PT_MESSAGE ? "Message" : f->type == PT_ENUM ? "Enum" : "";
	return xcat(e->g->pfx, label, modifier, "ExtensionField");
}

static void extension_set_add(ExtensionSet *s, const Vec *fields)
{
	for (int i = 0; i < fields->n; i++) {
		ExtensionGen *e = xalloc(sizeof *e);
		e->g = s->g;
		e->fd = fields->v[i];
		e->swift_full_extension_name = sw_full_name_extension(s->g->namer, e->fd);
		e->comments = gen_comments_with_deprecation(s->g, SW_FIELD, e->fd);
		e->containing_type_swift_full_name = sw_full_name_message(s->g->namer, e->fd->containing_type);
		e->offset = s->extensions.n;
		vec_push(&s->extensions, e);
	}
}

static void extension_generate_declaration(SwPrinter *p, const ExtensionGen *e)
{
	Gen *g = e->g;
	const SwField *f = e->fd;
	bool is_file_scope = !f->extension_scope;
	const char *traits = traits_type(g->namer, f);
	const char *relative = sw_relative_name_extension(g->namer, f);
	const char *field_name_path;
	if (f->containing_type->message_set_wire_format && f->type == PT_MESSAGE && !sw_field_is_repeated(f) &&
	    !sw_field_is_required(f) && f->message_type == f->extension_scope)
		field_name_path = f->message_type->full_name;
	else
		field_name_path = sw_field_full_name(f);
	print_c(p, e->comments, "%s%s%slet %s = %sMessageExtension<%s<%s>, %s>(", vis(g), is_file_scope ? "nonisolated " : "",
		is_file_scope ? "" : "static ", relative, g->pfx, extension_field_type(e), traits,
		e->containing_type_swift_full_name);
	print_indented2(p, xfmt("_protobuf_fieldNumber: %d,", (int)f->number), xfmt("fieldName: \"%s\"", field_name_path));
	sw_print(p, ")");
}

static void extension_generate_message_swift_extension(SwPrinter *p, const ExtensionGen *e)
{
	Gen *g = e->g;
	const SwField *f = e->fd;
	const char *api_type = swift_type(g->namer, f);
	SwFieldNames names = sw_message_property_names_extension(g->namer, f);
	const char *default_value = swift_default_value(g->namer, f);
	const char *ext = e->swift_full_extension_name;
	sw_print_blank(p);
	print_c(p, e->comments, "%svar %s: %s {", vis(g), names.name, api_type);
	print_indented2(p, xfmt("get {return getExtensionValue(ext: %s) ?? %s}", ext, default_value),
			xfmt("set {setExtensionValue(ext: %s, value: newValue)}", ext));
	sw_print(p, "}");
	if (sw_field_is_repeated(f))
		return;
	sw_printf(p, "/// Returns true if extension `%s`\n/// has been explicitly set.", ext);
	sw_printf(p, "%svar %s: Bool {", vis(g), names.has);
	sw_indent(p);
	sw_printf(p, "return hasExtensionValue(ext: %s)", ext);
	sw_outdent(p);
	sw_print(p, "}");
	sw_printf(p, "/// Clears the value of extension `%s`.", ext);
	sw_print(p, "/// Subsequent reads from it will return its default value.");
	sw_printf(p, "%smutating func %s() {", vis(g), names.clear);
	sw_indent(p);
	sw_printf(p, "clearExtensionValue(ext: %s)", ext);
	sw_outdent(p);
	sw_print(p, "}");
}

static void extension_set_generate_message_swift_extensions(SwPrinter *p, ExtensionSet *s)
{
	if (!s->extensions.n)
		return;
	sw_print(p, "\n"
		    "// MARK: - Extension Properties\n"
		    "\n"
		    "// Swift Extensions on the extended Messages to add easy access to the declared\n"
		    "// extension fields. The names are based on the extension field name from the proto\n"
		    "// declaration. To avoid naming collisions, the names are prefixed with the name of\n"
		    "// the scope where the extend directive occurs.");
	/* Grouped by the extended message, keeping the declaration order within a group. */
	int n = s->extensions.n;
	ExtensionGen **grouped = xalloc(sizeof(ExtensionGen *) * (size_t)n);
	for (int i = 0; i < n; i++) {
		ExtensionGen *x = s->extensions.v[i];
		int j = i;
		while (j > 0 && strcmp(grouped[j - 1]->containing_type_swift_full_name, x->containing_type_swift_full_name) > 0) {
			grouped[j] = grouped[j - 1];
			j--;
		}
		grouped[j] = x;
	}
	const char *current_type = "";
	for (int i = 0; i < n; i++) {
		ExtensionGen *e = grouped[i];
		if (!str_eq(current_type, e->containing_type_swift_full_name)) {
			if (*current_type) {
				sw_outdent(p);
				sw_print(p, "}");
			}
			current_type = e->containing_type_swift_full_name;
			sw_print_blank(p);
			sw_printf(p, "nonisolated extension %s {", current_type);
			sw_indent(p);
		}
		extension_generate_message_swift_extension(p, e);
	}
	sw_outdent(p);
	sw_print_blank(p);
	sw_print(p, "}");
}

static void extension_set_generate_file_registry(SwPrinter *p, ExtensionSet *s)
{
	if (!s->extensions.n)
		return;
	Gen *g = s->g;
	PathParts parts = split_path(g->file->name);
	char *id = sw_to_upper_camel(parts.base);
	const char *file_prefix = sw_namer_type_prefix(g->namer, g->file);
	sw_printf(p, "\n"
		     "// MARK: - File's ExtensionMap: %s%s_Extensions\n"
		     "\n"
		     "/// A `SwiftProtobuf.SimpleExtensionMap` that includes all of the extensions defined by\n"
		     "/// this .proto file. It can be used any place an `SwiftProtobuf.ExtensionMap` is needed\n"
		     "/// in parsing, or it can be combined with other `SwiftProtobuf.SimpleExtensionMap`s to create\n"
		     "/// a larger `SwiftProtobuf.SimpleExtensionMap`.\n"
		     "%snonisolated let %s%s_Extensions: %sSimpleExtensionMap = [",
		  file_prefix, id, vis(g), file_prefix, id, g->pfx);
	sw_indent(p);
	int last = s->extensions.n - 1;
	for (int i = 0; i < s->extensions.n; i++)
		sw_printf(p, "%s%s", VEC_AT(s->extensions, ExtensionGen, i)->swift_full_extension_name, i != last ? "," : "");
	sw_outdent(p);
	sw_print(p, "]");
}

static void extension_set_generate_declarations(SwPrinter *p, ExtensionSet *s)
{
	if (!s->extensions.n)
		return;
	Gen *g = s->g;
	sw_print(p, "\n"
		    "// Extension Objects - The only reason these might be needed is when manually\n"
		    "// constructing a `SimpleExtensionMap`, otherwise, use the above _Extension Properties_\n"
		    "// accessors for the extension fields on the messages directly.");
	const SwMessage *current_scope = NULL;
	bool add_newline = true;
	for (int i = 0; i < s->extensions.n; i++) {
		ExtensionGen *e = s->extensions.v[i];
		if (current_scope != e->fd->extension_scope) {
			if (current_scope) {
				sw_outdent(p);
				sw_print(p, "}");
				sw_outdent(p);
				sw_print(p, "}");
			}
			current_scope = e->fd->extension_scope;
			if (!current_scope)
				fail("Unexpectedly found nil while unwrapping an Optional value");
			sw_print_blank(p);
			sw_printf(p, "nonisolated extension %s {", sw_full_name_message(g->namer, current_scope));
			sw_indent(p);
			sw_printf(p, "%senum Extensions {", vis(g));
			sw_indent(p);
			add_newline = false;
		}
		if (add_newline)
			sw_print_blank(p);
		else
			add_newline = true;
		extension_generate_declaration(p, e);
	}
	if (current_scope) {
		sw_outdent(p);
		sw_print(p, "}");
		sw_outdent(p);
		sw_print(p, "}");
	}
}
