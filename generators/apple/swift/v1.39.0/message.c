/* Copyright (c) 2014 - 2016 Apple Inc. and the project authors
 * Licensed under Apache License v2.0 with Runtime Library Exception
 *
 * See LICENSE.txt for license information:
 * https://github.com/apple/swift-protobuf/blob/main/LICENSE.txt */

/* Port of Sources/protoc-gen-swift/MessageGenerator.swift and the storage classes of
 * MessageStorageClassGenerator.swift. */

enum { STORAGE_NONE, STORAGE_CLASS, STORAGE_ANY };

struct MessageGen {
	Gen *g;
	SwMessage *d;
	char *swift_full_name, *swift_relative_name;
	Vec fields;               /* FieldGen * */
	Vec fields_sorted_by_number;
	Vec oneofs;               /* OneofGen * */
	int storage;
	Vec enums;                /* EnumGen * */
	Vec messages;             /* MessageGen * */
	bool is_extensible;
};

static MessageGen *message_gen(Gen *g, SwMessage *d, ExtensionSet *extension_set)
{
	MessageGen *m = xalloc(sizeof *m);
	m->g = g;
	m->d = d;
	m->is_extensible = d->n_extension_ranges > 0;
	m->swift_relative_name = sw_relative_name_message(g->namer, d);
	m->swift_full_name = sw_full_name_message(g->namer, d);
	bool use_heap_storage = should_use_heap_storage(d);
	for (int i = 0; i < d->real_oneofs.n; i++)
		vec_push(&m->oneofs, oneof_gen(g, d->real_oneofs.v[i], use_heap_storage));
	for (int i = 0; i < d->fields.n; i++) {
		SwField *f = d->fields.v[i];
		if (sw_field_real_containing_oneof(f)) {
			if (f->oneof_index >= m->oneofs.n)
				fail("Index out of range");
			vec_push(&m->fields, oneof_field_generator(m->oneofs.v[f->oneof_index], f->number));
		} else {
			vec_push(&m->fields, message_field_gen(g, f, use_heap_storage));
		}
	}
	m->fields_sorted_by_number = sort_by_number(&m->fields);
	extension_set_add(extension_set, &d->extensions);
	for (int i = 0; i < d->enums.n; i++)
		vec_push(&m->enums, enum_gen(g, d->enums.v[i]));
	for (int i = 0; i < d->messages.n; i++) {
		SwMessage *nested = d->messages.v[i];
		if (!nested->map_entry)
			vec_push(&m->messages, message_gen(g, nested, extension_set));
	}
	if (d->is_any) {
		if (!use_heap_storage)
			fail("google.protobuf.Any must use heap storage");
		m->storage = STORAGE_ANY;
	} else {
		m->storage = use_heap_storage ? STORAGE_CLASS : STORAGE_NONE;
	}
	return m;
}

/* ---- MessageStorageClassGenerator / AnyMessageStorageClassGenerator ---- */

static const char *storage_visibility(const MessageGen *m) { return m->storage == STORAGE_ANY ? "internal" : "fileprivate"; }

static void storage_generate_type_declaration(SwPrinter *p, const MessageGen *m)
{
	if (m->storage == STORAGE_ANY) {
		sw_print(p, "typealias _StorageClass = AnyMessageStorage");
		return;
	}
	sw_print(p, "fileprivate class _StorageClass {");
	sw_indent(p);
	for (int i = 0; i < m->fields.n; i++)
		fg_generate_storage(p, m->fields.v[i]);
	sw_print(p, "\n"
		    "  // This property is used as the initial default value for new instances of the type.\n"
		    "  // The type itself is protecting the reference to its storage via CoW semantics.\n"
		    "  // This will force a copy to be made of this reference when the first mutation occurs;\n"
		    "  // hence, it is safe to mark this as `nonisolated(unsafe)`.\n"
		    "  static nonisolated(unsafe) let defaultInstance = _StorageClass()\n"
		    "\n"
		    "private init() {}\n");
	sw_print(p, "init(copying source: _StorageClass) {");
	sw_indent(p);
	for (int i = 0; i < m->fields.n; i++)
		fg_generate_storage_class_clone(p, m->fields.v[i]);
	sw_outdent(p);
	sw_print(p, "}");
	sw_outdent(p);
	sw_print(p, "}");
}

static void storage_generate_unique_storage(SwPrinter *p, const MessageGen *m)
{
	sw_printf(p, "%s mutating func _uniqueStorage() -> _StorageClass {", storage_visibility(m));
	sw_indent(p);
	sw_print(p, "if !isKnownUniquelyReferenced(&_storage) {");
	sw_print_indented(p, "_storage = _StorageClass(copying: _storage)");
	sw_print(p, "}");
	sw_print(p, "return _storage");
	sw_outdent(p);
	sw_print(p, "}");
}

/* ---- MessageGenerator ---- */

/* generateWithLifetimeExtension: the part before body(&p); with_lifetime_end after it. */
static void with_lifetime_begin(SwPrinter *p, const MessageGen *m, bool can_throw, bool returns, const char *captured,
				const char *qualifier)
{
	if (m->storage == STORAGE_NONE)
		return;
	const char *prefix_keywords = xcat(returns ? "return " : "", can_throw ? "try " : "");
	const char *self_qualifier = qualifier ? xcat(qualifier, ".") : "";
	if (captured) {
		sw_printf(p, "%swithExtendedLifetime((%s_storage, %s._storage)) { (_args: (_StorageClass, _StorageClass)) in",
			  prefix_keywords, self_qualifier, captured);
		sw_indent(p);
		sw_print(p, "let _storage = _args.0");
		sw_printf(p, "let %s_storage = _args.1", captured);
	} else {
		sw_printf(p, "%swithExtendedLifetime(%s_storage) { (_storage: _StorageClass) in", prefix_keywords, self_qualifier);
		sw_indent(p);
	}
}

static void with_lifetime_end(SwPrinter *p, const MessageGen *m)
{
	if (m->storage == STORAGE_NONE)
		return;
	sw_outdent(p);
	sw_print(p, "}");
}

static void message_generate_main_struct(SwPrinter *p, MessageGen *m, const MessageGen *parent)
{
	(void)parent;
	Gen *g = m->g;
	const SwMessage *d = m->d;
	if (d->message_set_wire_format && m->fields.n) {
		g->error = xcat(d->full_name, " has the option message_set_wire_format but it also has fields.");
		return;
	}
	for (int i = 0; i < d->extensions.n; i++) {
		const SwField *e = d->extensions.v[i];
		if (!e->containing_type->message_set_wire_format)
			continue;
		if (e->type != PT_MESSAGE) {
			g->error = xcat(e->containing_type->full_name, " has the option message_set_wire_format but ",
					sw_field_full_name(e), " is a non message extension field.");
			return;
		}
		if (sw_field_is_required(e) || sw_field_is_repeated(e)) {
			g->error = xcat(e->containing_type->full_name, " has the option message_set_wire_format but ",
					sw_field_full_name(e), " cannot be required nor repeated extension field.");
			return;
		}
	}
	Buf conformances = {0};
	if (m->is_extensible)
		buf_fmt(&conformances, "%sExtensibleMessage, ", g->pfx);
	buf_adds(&conformances, m->storage != STORAGE_NONE ? "@unchecked Sendable" : "Sendable");

	sw_print_blank(p);
	print_c(p, gen_comments_with_deprecation(g, SW_MESSAGE, d), "%snonisolated struct %s: %s {", vis(g),
		m->swift_relative_name, buf_str(&conformances));
	sw_indent(p);
	sw_printf(p, "// %s.Message conformance is added in an extension below. See the\n"
		     "// `Message` and `Message+*Additions` files in the SwiftProtobuf library for\n"
		     "// methods supported on all messages.",
		  sw_namer_swift_protobuf_module_name(g->namer));
	for (int i = 0; i < m->fields.n; i++)
		fg_generate_interface(g, p, m->fields.v[i]);
	sw_print_blank(p);
	sw_printf(p, "%svar unknownFields = %sUnknownStorage()", vis(g), g->pfx);
	for (int i = 0; i < m->oneofs.n; i++)
		oneof_generate_main_enum(p, m->oneofs.v[i]);
	for (int i = 0; i < m->enums.n; i++)
		enum_generate_main_enum(p, m->enums.v[i]);
	for (int i = 0; i < m->messages.n; i++)
		message_generate_main_struct(p, m->messages.v[i], m);
	sw_print_blank(p);
	sw_printf(p, "%sinit() {}", vis(g));
	if (m->is_extensible) {
		sw_print_blank(p);
		sw_printf(p, "%svar _protobuf_extensionFieldValues = %sExtensionFieldValueSet()", vis(g), g->pfx);
	}
	if (m->storage != STORAGE_NONE) {
		if (!m->is_extensible)
			sw_print_blank(p);
		sw_printf(p, "%s var _storage = _StorageClass.defaultInstance", storage_visibility(m));
	} else {
		SwPrinter sub;
		sw_printer_init_from(&sub, p);
		for (int i = 0; i < m->fields.n; i++)
			fg_generate_storage(&sub, m->fields.v[i]);
		if (!sw_printer_is_empty(&sub)) {
			if (!m->is_extensible)
				sw_print_blank(p);
			sw_append(p, &sub, false);
		}
	}
	sw_outdent(p);
	sw_print(p, "}");
}

static void message_generate_proto_name_providing(SwPrinter *p, const MessageGen *m)
{
	Gen *g = m->g;
	if (g->opts->experimental_hidden_names & HIDDEN_FIELDS) {
		sw_printf(p, "%sstatic let _protobuf_nameMap = %s_NameMap()", vis(g), g->pfx);
		return;
	}
	NameWriter w;
	name_writer_init(&w);
	for (int i = 0; i < m->fields_sorted_by_number.n; i++)
		fg_write_proto_name_instruction(m->fields_sorted_by_number.v[i], &w);
	for (int i = 0; i < m->d->reserved_names.n; i++)
		nw_write_reserved_name(&w, m->d->reserved_names.v[i]);
	for (int i = 0; i < m->d->n_reserved_ranges; i++)
		nw_write_reserved_numbers(&w, m->d->reserved_ranges[i]);
	if (!w.has_data)
		sw_printf(p, "%sstatic let _protobuf_nameMap = %s_NameMap()", vis(g), g->pfx);
	else
		sw_printf(p, "%sstatic let _protobuf_nameMap = %s_NameMap(bytecode: %s)", vis(g), g->pfx, name_writer_literal(&w));
}

static void message_generate_decode_message(SwPrinter *p, const MessageGen *m)
{
	Gen *g = m->g;
	sw_printf(p, "%smutating func decodeMessage<D: %sDecoder>(decoder: inout D) throws {", vis(g), g->pfx);
	sw_indent(p);
	if (m->storage != STORAGE_NONE)
		sw_print(p, "_ = _uniqueStorage()");
	if (m->d->message_set_wire_format && m->is_extensible) {
		sw_printf(p, "try decoder.decodeExtensionFieldsAsMessageSet(values: &_protobuf_extensionFieldValues, messageType: %s.self)",
			  m->swift_full_name);
	} else if (!m->fields.n && !m->is_extensible) {
		sw_print(p, "// Load everything into unknown fields");
		sw_print(p, "while try decoder.nextFieldNumber() != nil {}");
	} else {
		with_lifetime_begin(p, m, true, false, NULL, NULL);
		sw_print(p, "while let fieldNumber = try decoder.nextFieldNumber() {");
		sw_indent(p);
		SwRange *ranges;
		int n_ranges = normalized_extension_ranges(m->d, &ranges);
		if (m->fields.n || n_ranges > 3) {
			sw_print(p, "// The use of inline closures is to circumvent an issue where the compiler\n"
				    "// allocates stack space for every case branch when no optimizations are\n"
				    "// enabled. https://github.com/apple/swift-protobuf/issues/1034\n"
				    "switch fieldNumber {");
			for (int i = 0; i < m->fields_sorted_by_number.n; i++)
				fg_generate_decode_field_case(p, m->fields_sorted_by_number.v[i]);
			if (m->is_extensible) {
				sw_printf(p, "case %s:", ranges_case_expression(ranges, n_ranges));
				sw_print_indented(p, xfmt("try { try decoder.decodeExtensionField(values: &_protobuf_extensionFieldValues, messageType: %s.self, fieldNumber: fieldNumber) }()",
							  m->swift_full_name));
			}
			sw_print(p, "default: break");
			sw_print(p, "}");
		} else if (m->is_extensible) {
			sw_printf(p, "if %s {", ranges_boolean_expression(ranges, n_ranges, "fieldNumber"));
			sw_print_indented(p, xfmt("try decoder.decodeExtensionField(values: &_protobuf_extensionFieldValues, messageType: %s.self, fieldNumber: fieldNumber)",
						  m->swift_full_name));
			sw_print(p, "}");
		}
		sw_outdent(p);
		sw_print(p, "}");
		with_lifetime_end(p, m);
	}
	sw_outdent(p);
	sw_print(p, "}");
}

static void message_generate_traverse(SwPrinter *p, const MessageGen *m)
{
	Gen *g = m->g;
	sw_printf(p, "%sfunc traverse<V: %sVisitor>(visitor: inout V) throws {", vis(g), g->pfx);
	sw_indent(p);
	with_lifetime_begin(p, m, true, false, NULL, NULL);
	if (m->storage == STORAGE_ANY)
		sw_print(p, "try _storage.preTraverse()");
	const char *visit_extensions = m->d->message_set_wire_format ? "visitExtensionFieldsAsMessageSet" : "visitExtensionFields";
	bool uses_locals = false;
	for (int i = 0; i < m->fields.n && !uses_locals; i++)
		uses_locals = fg_generate_traverse_uses_locals(m->fields.v[i]);
	if (uses_locals)
		sw_print(p, "// The use of inline closures is to circumvent an issue where the compiler\n"
			    "// allocates stack space for every if/case branch local when no optimizations\n"
			    "// are enabled. https://github.com/apple/swift-protobuf/issues/1034 and\n"
			    "// https://github.com/apple/swift-protobuf/issues/1182");
	SwRange *ranges;
	int n_ranges = ambitious_extension_ranges(m->d, &ranges);
	int next = 0;
	for (int i = 0; i < m->fields_sorted_by_number.n; i++) {
		const FieldGen *f = m->fields_sorted_by_number.v[i];
		while (next < n_ranges && ranges[next].start < f->number) {
			sw_printf(p, "try visitor.%s(fields: _protobuf_extensionFieldValues, start: %d, end: %d)", visit_extensions,
				  (int)ranges[next].start, (int)ranges[next].end);
			next++;
		}
		fg_generate_traverse(g, p, f);
	}
	for (; next < n_ranges; next++)
		sw_printf(p, "try visitor.%s(fields: _protobuf_extensionFieldValues, start: %d, end: %d)", visit_extensions,
			  (int)ranges[next].start, (int)ranges[next].end);
	with_lifetime_end(p, m);
	sw_print(p, "try unknownFields.traverse(visitor: &visitor)");
	sw_outdent(p);
	sw_print(p, "}");
}

static void message_generate_equality(SwPrinter *p, const MessageGen *m)
{
	Gen *g = m->g;
	sw_printf(p, "%sstatic func ==(lhs: %s, rhs: %s) -> Bool {", vis(g), m->swift_full_name, m->swift_full_name);
	sw_indent(p);
	bool compare_fields = true;
	if (m->storage != STORAGE_NONE) {
		sw_print(p, "if lhs._storage !== rhs._storage {");
		sw_indent(p);
		sw_print_nl(p, STR_LIT("let storagesAreEqual: Bool = "), false);
		if (m->storage == STORAGE_ANY) {
			sw_print(p, "lhs._storage.isEqualTo(other: rhs._storage)");
			compare_fields = false;
		}
	}
	if (compare_fields) {
		with_lifetime_begin(p, m, false, false, "rhs", "lhs");
		for (int i = 0; i < m->fields.n; i++)
			fg_generate_field_comparison(p, m->fields.v[i]);
		if (m->storage != STORAGE_NONE)
			sw_print(p, "return true");
		with_lifetime_end(p, m);
	}
	if (m->storage != STORAGE_NONE) {
		sw_print(p, "if !storagesAreEqual {return false}");
		sw_outdent(p);
		sw_print(p, "}");
	}
	sw_print(p, "if lhs.unknownFields != rhs.unknownFields {return false}");
	if (m->is_extensible)
		sw_print(p, "if lhs._protobuf_extensionFieldValues != rhs._protobuf_extensionFieldValues {return false}");
	sw_print(p, "return true");
	sw_outdent(p);
	sw_print(p, "}");
}

static void message_generate_is_initialized(SwPrinter *p, const MessageGen *m)
{
	Gen *g = m->g;
	SwPrinter checks;
	sw_printer_init(&checks, true);
	for (int i = 0; i < m->fields.n; i++)
		fg_generate_required_field_check(&checks, m->fields.v[i]);
	for (int i = 0; i < m->fields.n; i++)
		fg_generate_is_initialized_check(g, &checks, m->fields.v[i]);
	bool generated_checks = !sw_printer_is_empty(&checks);
	if (!m->is_extensible && !generated_checks)
		return;
	sw_print(p, "public var isInitialized: Bool {");
	sw_indent(p);
	if (m->is_extensible)
		sw_print(p, "if !_protobuf_extensionFieldValues.isInitialized {return false}");
	if (generated_checks) {
		with_lifetime_begin(p, m, false, true, NULL, NULL);
		sw_append(p, &checks, true);
		sw_print(p, "return true");
		with_lifetime_end(p, m);
	} else {
		sw_print(p, "return true");
	}
	sw_outdent(p);
	sw_print(p, "}");
	sw_print(p, "");
}

static void message_generate_runtime_support(SwPrinter *p, MessageGen *m, const MessageGen *parent)
{
	Gen *g = m->g;
	const SwMessage *d = m->d;
	sw_print_blank(p);
	sw_printf(p, "nonisolated extension %s: %sMessage, %s_MessageImplementationBase, %s_ProtoNameProviding {",
		  m->swift_full_name, g->pfx, g->pfx, g->pfx);
	sw_indent(p);
	if (g->opts->experimental_hidden_names & HIDDEN_TYPES)
		sw_printf(p, "%sstatic let protoMessageName: String = \"\"", vis(g));
	else if (parent)
		sw_printf(p, "%sstatic let protoMessageName: String = %s.protoMessageName + \".%s\"", vis(g), parent->swift_full_name, d->name);
	else if (*d->file->package)
		sw_printf(p, "%sstatic let protoMessageName: String = _protobuf_package + \".%s\"", vis(g), d->name);
	else
		sw_printf(p, "%sstatic let protoMessageName: String = \"%s\"", vis(g), d->name);
	message_generate_proto_name_providing(p, m);
	if (m->storage != STORAGE_NONE) {
		sw_print_blank(p);
		storage_generate_type_declaration(p, m);
		sw_print_blank(p);
		storage_generate_unique_storage(p, m);
	}
	sw_print_blank(p);
	message_generate_is_initialized(p, m);
	message_generate_decode_message(p, m);
	sw_print_blank(p);
	message_generate_traverse(p, m);
	sw_print_blank(p);
	message_generate_equality(p, m);
	sw_outdent(p);
	sw_print(p, "}");
	for (int i = 0; i < m->enums.n; i++)
		enum_generate_runtime_support(p, m->enums.v[i]);
	for (int i = 0; i < m->messages.n; i++)
		message_generate_runtime_support(p, m->messages.v[i], m);
}
