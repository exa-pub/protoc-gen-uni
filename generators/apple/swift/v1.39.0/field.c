/* Copyright (c) 2014 - 2017 Apple Inc. and the project authors
 * Licensed under Apache License v2.0 with Runtime Library Exception
 *
 * See LICENSE.txt for license information:
 * https://github.com/apple/swift-protobuf/blob/main/LICENSE.txt */

/* Port of Sources/protoc-gen-swift/FieldGenerator.swift (the FieldGenerator protocol and
 * FieldGeneratorBase) and MessageFieldGenerator.swift. A FieldGen is either a
 * MessageFieldGenerator or a OneofGenerator.MemberFieldGenerator (oneof.c). */

typedef struct {
	bool is_member; /* OneofGenerator.MemberFieldGenerator */
	int number;
	SwField *fd;
	Str comments;
	char *swift_name, *swift_type, *swift_default_value;
	/* MessageFieldGenerator */
	bool uses_heap_storage, has_field_presence;
	char *underscore_swift_name, *stored_property, *swift_has_name, *swift_clear_name;
	char *swift_storage_type, *traits_type;
	/* MemberFieldGenerator */
	OneofGen *oneof;
	int group;
	char *dotted_swift_name;
	const char *proto_generic_type;
} FieldGen;

/* ---- helpers shared by the generators ---- */

/* print("\(comments)<formatted>") */
static void print_c(SwPrinter *p, Str comments, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
static void print_c(SwPrinter *p, Str comments, const char *fmt, ...)
{
	Buf b = {0};
	buf_add_str(&b, comments);
	va_list ap;
	va_start(ap, fmt);
	buf_vfmt(&b, fmt, ap);
	va_end(ap);
	sw_print_str(p, buf_view(&b));
}

/* printIndented(a, b) */
static void print_indented2(SwPrinter *p, const char *a, const char *b)
{
	sw_indent(p);
	sw_print(p, a);
	sw_print(p, b);
	sw_outdent(p);
}

/* ProvidesSourceCodeLocation.protoSourceComments(generatorOptions:) */
static Str gen_comments(const Gen *g, SwKind k, const void *d)
{
	if (g->opts->experimental_strip_nonfunctional_codegen)
		return STR_LIT("");
	return sw_proto_source_comments(k, d, "///", NULL);
}

/* protoSourceCommentsWithDeprecation(generatorOptions:) */
static Str gen_comments_with_deprecation(const Gen *g, SwKind k, const void *d)
{
	if (g->opts->experimental_strip_nonfunctional_codegen)
		return sw_deprecation_comment(k, d, "///");
	return sw_proto_source_comments_with_deprecation(k, d, "///", NULL);
}

static const char *vis(const Gen *g) { return g->opts->visibility_source_snippet; }

static bool is_group_or_message(const SwField *f) { return f->type == PT_GROUP || f->type == PT_MESSAGE; }

/* ---- FieldGeneratorBase ---- */

static void fg_write_proto_name_instruction(const FieldGen *fg, NameWriter *w)
{
	const SwField *f = fg->fd;
	bool group_like = sw_field_is_group_like(f);
	const char *proto_name = group_like ? f->message_type->name : f->name;
	const char *json_name = f->json_name;
	if (group_like)
		nw_write_group(w, fg->number, proto_name);
	else if (str_eq(json_name, proto_name))
		nw_write_same(w, fg->number, proto_name);
	else if (str_eq(json_name, sw_to_json_field_name(proto_name)))
		nw_write_standard(w, fg->number, proto_name);
	else
		nw_write_unique(w, fg->number, proto_name, json_name);
}

/* ---- MessageFieldGenerator ---- */

static FieldGen *message_field_gen(Gen *g, SwField *fd, bool uses_heap_storage)
{
	if (sw_field_real_containing_oneof(fd))
		fail("%s: field in a oneof", fd->name);
	FieldGen *fg = xalloc(sizeof *fg);
	fg->fd = fd;
	fg->number = fd->number;
	fg->uses_heap_storage = uses_heap_storage;
	fg->has_field_presence = sw_field_has_presence(fd);
	SwFieldNames names = sw_message_property_names(g->namer, fd, "_", fg->has_field_presence);
	fg->swift_name = names.name;
	fg->underscore_swift_name = names.prefixed;
	fg->swift_has_name = names.has;
	fg->swift_clear_name = names.clear;
	fg->swift_type = swift_type(g->namer, fd);
	fg->swift_storage_type = swift_storage_type(g->namer, fd);
	fg->swift_default_value = swift_default_value(g->namer, fd);
	fg->traits_type = traits_type(g->namer, fd);
	fg->comments = gen_comments_with_deprecation(g, SW_FIELD, fd);
	if (uses_heap_storage)
		fg->stored_property = xcat("_storage.", fg->underscore_swift_name);
	else
		fg->stored_property = xcat("self.", fg->has_field_presence ? fg->underscore_swift_name : fg->swift_name);
	return fg;
}

static void mf_generate_storage(SwPrinter *p, const FieldGen *fg)
{
	const char *default_value = fg->has_field_presence ? "nil" : fg->swift_default_value;
	if (fg->uses_heap_storage)
		sw_printf(p, "var %s: %s = %s", fg->underscore_swift_name, fg->swift_storage_type, default_value);
	else if (fg->has_field_presence)
		sw_printf(p, "fileprivate var %s: %s = %s", fg->underscore_swift_name, fg->swift_storage_type, default_value);
}

static void mf_generate_interface(Gen *g, SwPrinter *p, const FieldGen *fg)
{
	const char *v = vis(g);
	sw_print_blank(p);
	if (fg->uses_heap_storage) {
		print_c(p, fg->comments, "%svar %s: %s {", v, fg->swift_name, fg->swift_type);
		const char *default_clause = fg->has_field_presence ? xcat(" ?? ", fg->swift_default_value) : "";
		print_indented2(p, xfmt("get {_storage.%s%s}", fg->underscore_swift_name, default_clause),
				xfmt("set {_uniqueStorage().%s = newValue}", fg->underscore_swift_name));
		sw_print(p, "}");
	} else if (fg->has_field_presence) {
		print_c(p, fg->comments, "%svar %s: %s {", v, fg->swift_name, fg->swift_type);
		print_indented2(p, xfmt("get {%s ?? %s}", fg->underscore_swift_name, fg->swift_default_value),
				xfmt("set {%s = newValue}", fg->underscore_swift_name));
		sw_print(p, "}");
	} else {
		print_c(p, fg->comments, "%svar %s: %s = %s", v, fg->swift_name, fg->swift_storage_type, fg->swift_default_value);
	}
	if (!fg->has_field_presence)
		return;
	const char *immutable_prefix = fg->uses_heap_storage ? "_storage." : "self.";
	sw_printf(p, "/// Returns true if `%s` has been explicitly set.", fg->swift_name);
	sw_printf(p, "%svar %s: Bool {%s%s != nil}", v, fg->swift_has_name, immutable_prefix, fg->underscore_swift_name);
	const char *mutable_prefix = fg->uses_heap_storage ? "_uniqueStorage()." : "self.";
	sw_printf(p, "/// Clears the value of `%s`. Subsequent reads from it will return its default value.", fg->swift_name);
	sw_printf(p, "%smutating func %s() {%s%s = nil}", v, fg->swift_clear_name, mutable_prefix, fg->underscore_swift_name);
}

static void mf_generate_storage_class_clone(SwPrinter *p, const FieldGen *fg)
{
	sw_printf(p, "%s = source.%s", fg->underscore_swift_name, fg->underscore_swift_name);
}

static void mf_generate_field_comparison(SwPrinter *p, const FieldGen *fg)
{
	if (fg->uses_heap_storage) {
		sw_printf(p, "if _storage.%s != rhs_storage.%s {return false}", fg->underscore_swift_name, fg->underscore_swift_name);
	} else {
		const char *name = fg->has_field_presence ? fg->underscore_swift_name : fg->swift_name;
		sw_printf(p, "if lhs.%s != rhs.%s {return false}", name, name);
	}
}

static void mf_generate_required_field_check(SwPrinter *p, const FieldGen *fg)
{
	if (!sw_field_is_required(fg->fd))
		return;
	sw_printf(p, "if %s == nil {return false}", fg->stored_property);
}

static void mf_generate_is_initialized_check(Gen *g, SwPrinter *p, const FieldGen *fg)
{
	if (!is_group_or_message(fg->fd) || !contains_required_fields(fg->fd->message_type))
		return;
	if (sw_field_is_repeated(fg->fd))
		sw_printf(p, "if !%sInternal.areAllInitialized(%s) {return false}", g->pfx, fg->stored_property);
	else
		sw_printf(p, "if let v = %s, !v.isInitialized {return false}", fg->stored_property);
}

static void mf_generate_decode_field_case(SwPrinter *p, const FieldGen *fg)
{
	const char *method, *traits_arg;
	if (sw_field_is_map(fg->fd)) {
		method = "decodeMapField";
		traits_arg = xcat("fieldType: ", fg->traits_type, ".self, ");
	} else {
		method = xcat("decode", sw_field_is_repeated(fg->fd) ? "Repeated" : "Singular", proto_generic_type(fg->fd), "Field");
		traits_arg = "";
	}
	sw_printf(p, "case %d: try { try decoder.%s(%svalue: &%s) }()", fg->number, method, traits_arg, fg->stored_property);
}

static bool mf_generate_traverse_uses_locals(const FieldGen *fg)
{
	return !sw_field_is_repeated(fg->fd) && fg->has_field_presence;
}

static void mf_generate_traverse(SwPrinter *p, const FieldGen *fg)
{
	const SwField *f = fg->fd;
	const char *method, *traits_arg;
	if (sw_field_is_map(f)) {
		method = "visitMapField";
		traits_arg = xcat("fieldType: ", fg->traits_type, ".self, ");
	} else {
		const char *modifier = sw_field_is_packed(f) ? "Packed" : sw_field_is_repeated(f) ? "Repeated" : "Singular";
		method = xcat("visit", modifier, proto_generic_type(f), "Field");
		traits_arg = "";
	}
	const char *var_name = fg->has_field_presence ? "v" : fg->stored_property;
	bool uses_locals = false;
	const char *conditional;
	if (sw_field_is_repeated(f)) {
		conditional = xcat("!", var_name, ".isEmpty");
	} else if (fg->has_field_presence) {
		conditional = xcat("let v = ", fg->stored_property);
		uses_locals = true;
	} else if (f->type == PT_STRING || f->type == PT_BYTES) {
		conditional = xcat("!", var_name, ".isEmpty");
	} else if (f->type == PT_FLOAT || f->type == PT_DOUBLE) {
		conditional = xcat(var_name, ".bitPattern != 0");
	} else {
		conditional = xcat(var_name, " != ", fg->swift_default_value);
	}
	sw_printf(p, "%sif %s {", uses_locals ? "try { " : "", conditional);
	sw_indent(p);
	sw_printf(p, "try visitor.%s(%svalue: %s, fieldNumber: %d)", method, traits_arg, var_name, fg->number);
	sw_outdent(p);
	sw_printf(p, "}%s", uses_locals ? " }()" : "");
}

/* ---- the FieldGenerator protocol ---- */

static void oneof_generate_interface(Gen *g, SwPrinter *p, const FieldGen *fg);
static void oneof_generate_storage(SwPrinter *p, const FieldGen *fg);
static void oneof_generate_storage_class_clone(SwPrinter *p, const FieldGen *fg);
static void oneof_generate_decode_field_case(SwPrinter *p, const FieldGen *fg);
static void oneof_generate_field_comparison(SwPrinter *p, const FieldGen *fg);
static void oneof_generate_is_initialized_check(SwPrinter *p, const FieldGen *fg);
static void oneof_generate_traverse(Gen *g, SwPrinter *p, const FieldGen *fg);

static void fg_generate_interface(Gen *g, SwPrinter *p, const FieldGen *fg)
{
	if (fg->is_member)
		oneof_generate_interface(g, p, fg);
	else
		mf_generate_interface(g, p, fg);
}

static void fg_generate_storage(SwPrinter *p, const FieldGen *fg)
{
	if (fg->is_member)
		oneof_generate_storage(p, fg);
	else
		mf_generate_storage(p, fg);
}

static void fg_generate_storage_class_clone(SwPrinter *p, const FieldGen *fg)
{
	if (fg->is_member)
		oneof_generate_storage_class_clone(p, fg);
	else
		mf_generate_storage_class_clone(p, fg);
}

static void fg_generate_decode_field_case(SwPrinter *p, const FieldGen *fg)
{
	if (fg->is_member)
		oneof_generate_decode_field_case(p, fg);
	else
		mf_generate_decode_field_case(p, fg);
}

static bool fg_generate_traverse_uses_locals(const FieldGen *fg)
{
	return fg->is_member ? true : mf_generate_traverse_uses_locals(fg);
}

static void fg_generate_traverse(Gen *g, SwPrinter *p, const FieldGen *fg)
{
	if (fg->is_member)
		oneof_generate_traverse(g, p, fg);
	else
		mf_generate_traverse(p, fg);
}

static void fg_generate_field_comparison(SwPrinter *p, const FieldGen *fg)
{
	if (fg->is_member)
		oneof_generate_field_comparison(p, fg);
	else
		mf_generate_field_comparison(p, fg);
}

static void fg_generate_required_field_check(SwPrinter *p, const FieldGen *fg)
{
	if (!fg->is_member) /* oneof members are all optional */
		mf_generate_required_field_check(p, fg);
}

static void fg_generate_is_initialized_check(Gen *g, SwPrinter *p, const FieldGen *fg)
{
	if (fg->is_member)
		oneof_generate_is_initialized_check(p, fg);
	else
		mf_generate_is_initialized_check(g, p, fg);
}
