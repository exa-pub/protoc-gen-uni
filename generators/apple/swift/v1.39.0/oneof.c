/* Copyright (c) 2014 - 2016 Apple Inc. and the project authors
 * Licensed under Apache License v2.0 with Runtime Library Exception
 *
 * See LICENSE.txt for license information:
 * https://github.com/apple/swift-protobuf/blob/main/LICENSE.txt */

/* Port of Sources/protoc-gen-swift/OneofGenerator.swift. */

struct OneofGen {
	Gen *g;
	SwOneof *od;
	bool uses_heap_storage;
	Vec fields;               /* FieldGen *, in declaration order */
	Vec field_sorted_grouped; /* Vec * of FieldGen *: number order, grouped like in the parent */
	char *swift_relative_name, *swift_full_name;
	Str comments;
	char *swift_field_name, *underscore_swift_field_name, *stored_property;
};

/* OneofGenerator.MemberFieldGenerator.init */
static FieldGen *member_field_gen(Gen *g, SwField *fd)
{
	if (!fd->has_oneof_index)
		fail("%s: not in a oneof", fd->name);
	FieldGen *fg = xalloc(sizeof *fg);
	fg->is_member = true;
	fg->fd = fd;
	fg->number = fd->number;
	fg->group = -1;
	SwFieldNames names = sw_message_property_names(g->namer, fd, ".", false);
	fg->swift_name = names.name;
	fg->dotted_swift_name = names.prefixed;
	fg->swift_type = swift_type(g->namer, fd);
	fg->swift_default_value = swift_default_value(g->namer, fd);
	fg->proto_generic_type = proto_generic_type(fd);
	fg->comments = gen_comments_with_deprecation(g, SW_FIELD, fd);
	return fg;
}

static int cmp_int64(const void *a, const void *b)
{
	int64_t x = *(const int64_t *)a, y = *(const int64_t *)b;
	return x < y ? -1 : x > y;
}

/* A stable sort of field generators by number (like Swift's sort). */
static Vec sort_by_number(const Vec *fields)
{
	Vec r = {0};
	for (int i = 0; i < fields->n; i++)
		vec_push(&r, fields->v[i]);
	for (int i = 1; i < r.n; i++) {
		FieldGen *x = r.v[i];
		int j = i;
		while (j > 0 && VEC_AT(r, FieldGen, j - 1)->number > x->number) {
			r.v[j] = r.v[j - 1];
			j--;
		}
		r.v[j] = x;
	}
	return r;
}

static OneofGen *oneof_gen(Gen *g, SwOneof *od, bool uses_heap_storage)
{
	OneofGen *o = xalloc(sizeof *o);
	o->g = g;
	o->od = od;
	o->uses_heap_storage = uses_heap_storage;
	o->comments = gen_comments(g, SW_ONEOF, od);
	o->swift_relative_name = sw_relative_name_oneof(g->namer, od);
	o->swift_full_name = sw_full_name_oneof(g->namer, od);
	SwFieldNames names = sw_message_property_name_oneof(g->namer, od, "_");
	o->swift_field_name = names.name;
	o->underscore_swift_field_name = names.prefixed;
	o->stored_property = uses_heap_storage ? xcat("_storage.", o->underscore_swift_field_name)
					       : xcat("self.", o->swift_field_name);
	for (int i = 0; i < od->fields.n; i++)
		vec_push(&o->fields, member_field_gen(g, od->fields.v[i]));
	Vec sorted = sort_by_number(&o->fields);

	/* Bucket the fields in continuous chunks based on the other fields in the parent and the
	 * starts of the parent's extension ranges. */
	const SwMessage *parent = od->containing_type;
	SwRange *ranges;
	int n_ranges = normalized_extension_ranges(parent, &ranges);
	int n_numbers = parent->fields.n + n_ranges;
	int64_t *numbers = xalloc(sizeof(int64_t) * (size_t)(n_numbers ? n_numbers : 1));
	for (int i = 0; i < parent->fields.n; i++)
		numbers[i] = VEC_AT(parent->fields, SwField, i)->number;
	for (int i = 0; i < n_ranges; i++)
		numbers[parent->fields.n + i] = ranges[i].start;
	qsort(numbers, (size_t)n_numbers, sizeof *numbers, cmp_int64);
	int it = 0;
	bool has_next = it < n_numbers;
	int64_t next = has_next ? numbers[it++] : 0;
	Vec *current = xalloc(sizeof *current);
	for (int i = 0; i < sorted.n; i++) {
		FieldGen *f = sorted.v[i];
		if (!has_next || next != f->number) {
			if (current->n) {
				vec_push(&o->field_sorted_grouped, current);
				current = xalloc(sizeof *current);
			}
			while (!has_next || next != f->number) {
				if (it >= n_numbers)
					fail("%s: oneof field not in its message", od->name); /* upstream loops forever */
				has_next = true;
				next = numbers[it++];
			}
		}
		vec_push(current, f);
		has_next = it < n_numbers;
		next = has_next ? numbers[it++] : 0;
	}
	if (current->n)
		vec_push(&o->field_sorted_grouped, current);
	for (int i = 0; i < o->field_sorted_grouped.n; i++) {
		Vec *group = o->field_sorted_grouped.v[i];
		for (int j = 0; j < group->n; j++) {
			FieldGen *f = group->v[j];
			f->oneof = o;
			f->group = i;
		}
	}
	return o;
}

/* fieldGenerator(forFieldNumber:) */
static FieldGen *oneof_field_generator(const OneofGen *o, int number)
{
	for (int i = 0; i < o->fields.n; i++)
		if (VEC_AT(o->fields, FieldGen, i)->number == number)
			return o->fields.v[i];
	fail("Can't happen");
}

static bool oneof_first(const FieldGen *fg) { return fg->oneof->fields.n && fg->oneof->fields.v[0] == fg; }

static void oneof_generate_main_enum(SwPrinter *p, const OneofGen *o)
{
	const Gen *g = o->g;
	sw_print_blank(p);
	print_c(p, o->comments, "%s%snonisolated enum %s: Equatable, Sendable {", enum_annotation(g->opts), vis(g),
		o->swift_relative_name);
	sw_indent(p);
	for (int i = 0; i < o->fields.n; i++) {
		const FieldGen *f = o->fields.v[i];
		print_c(p, f->comments, "case %s(%s)", f->swift_name, f->swift_type);
	}
	Vec to_check = {0};
	for (int i = 0; i < o->fields.n; i++) {
		FieldGen *f = o->fields.v[i];
		if (is_group_or_message(f->fd) && contains_required_fields(f->fd->message_type))
			vec_push(&to_check, f);
	}
	if (to_check.n) {
		sw_print_blank(p);
		sw_print(p, "fileprivate var isInitialized: Bool {");
		sw_indent(p);
		if (to_check.n == 1) {
			const FieldGen *f = to_check.v[0];
			sw_printf(p, "guard case %s(let v) = self else {return true}", f->dotted_swift_name);
			sw_print(p, "return v.isInitialized");
		} else {
			sw_print(p, "// The use of inline closures is to circumvent an issue where the compiler\n"
				    "// allocates stack space for every case branch when no optimizations are\n"
				    "// enabled. https://github.com/apple/swift-protobuf/issues/1034\n"
				    "switch self {");
			for (int i = 0; i < to_check.n; i++) {
				const FieldGen *f = to_check.v[i];
				sw_printf(p, "case %s: return {", f->dotted_swift_name);
				print_indented2(p, xfmt("guard case %s(let v) = self else { preconditionFailure() }", f->dotted_swift_name),
						"return v.isInitialized");
				sw_print(p, "}()");
			}
			if (to_check.n != o->fields.n)
				sw_print(p, "default: return true");
			sw_print(p, "}");
		}
		sw_outdent(p);
		sw_print(p, "}");
	}
	sw_print_blank(p);
	sw_outdent(p);
	sw_print(p, "}");
}

/* gerenateOneofEnumProperty */
static void oneof_generate_enum_property(SwPrinter *p, const OneofGen *o)
{
	const char *v = vis(o->g);
	sw_print_blank(p);
	if (o->uses_heap_storage) {
		print_c(p, o->comments, "%svar %s: %s? {", v, o->swift_field_name, o->swift_relative_name);
		print_indented2(p, xfmt("get {return _storage.%s}", o->underscore_swift_field_name),
				xfmt("set {_uniqueStorage().%s = newValue}", o->underscore_swift_field_name));
		sw_print(p, "}");
	} else {
		print_c(p, o->comments, "%svar %s: %s? = nil", v, o->swift_field_name, o->swift_full_name);
	}
}

static void oneof_generate_interface(Gen *g, SwPrinter *p, const FieldGen *fg)
{
	const OneofGen *o = fg->oneof;
	if (oneof_first(fg))
		oneof_generate_enum_property(p, o);
	const char *getter = o->uses_heap_storage ? xcat("_storage.", o->underscore_swift_field_name) : o->swift_field_name;
	const char *setter = o->uses_heap_storage ? xcat("_uniqueStorage().", o->underscore_swift_field_name)
			     : str_eq(o->swift_field_name, "newValue") ? "self.newValue"
								     : o->swift_field_name;
	sw_print_blank(p);
	print_c(p, fg->comments, "%svar %s: %s {", vis(g), fg->swift_name, fg->swift_type);
	sw_indent(p);
	sw_print(p, "get {");
	print_indented2(p, xfmt("if case %s(let v)? = %s {return v}", fg->dotted_swift_name, getter),
			xfmt("return %s", fg->swift_default_value));
	sw_print(p, "}");
	sw_printf(p, "set {%s = %s(newValue)}", setter, fg->dotted_swift_name);
	sw_outdent(p);
	sw_print(p, "}");
}

static void oneof_generate_storage(SwPrinter *p, const FieldGen *fg)
{
	const OneofGen *o = fg->oneof;
	if (!oneof_first(fg))
		return;
	if (o->uses_heap_storage)
		sw_printf(p, "var %s: %s?", o->underscore_swift_field_name, o->swift_full_name);
}

static void oneof_generate_storage_class_clone(SwPrinter *p, const FieldGen *fg)
{
	const OneofGen *o = fg->oneof;
	if (!oneof_first(fg))
		return;
	sw_printf(p, "%s = source.%s", o->underscore_swift_field_name, o->underscore_swift_field_name);
}

static void oneof_generate_decode_field_case(SwPrinter *p, const FieldGen *fg)
{
	const OneofGen *o = fg->oneof;
	sw_printf(p, "case %d: try {", fg->number);
	sw_indent(p);
	const char *had_value_test;
	if (is_group_or_message(fg->fd)) {
		sw_printf(p, "var v: %s?", fg->swift_type);
		sw_print(p, "var hadOneofValue = false");
		sw_printf(p, "if let current = %s {", o->stored_property);
		print_indented2(p, "hadOneofValue = true", xfmt("if case %s(let m) = current {v = m}", fg->dotted_swift_name));
		sw_print(p, "}");
		had_value_test = "hadOneofValue";
	} else {
		sw_printf(p, "var v: %s?", fg->swift_type);
		had_value_test = xcat(o->stored_property, " != nil");
	}
	sw_printf(p, "try decoder.decodeSingular%sField(value: &v)", fg->proto_generic_type);
	sw_print(p, "if let v = v {");
	print_indented2(p, xfmt("if %s {try decoder.handleConflictingOneOf()}", had_value_test),
			xfmt("%s = %s(v)", o->stored_property, fg->dotted_swift_name));
	sw_print(p, "}");
	sw_outdent(p);
	sw_print(p, "}()");
}

static void oneof_generate_traverse(Gen *g, SwPrinter *p, const FieldGen *fg)
{
	(void)g;
	const OneofGen *o = fg->oneof;
	const Vec *group = o->field_sorted_grouped.v[fg->group];
	if (group->v[0] != fg)
		return;
	if (group->n == 1) {
		sw_printf(p, "try { if case %s(let v)? = %s {", fg->dotted_swift_name, o->stored_property);
		sw_indent(p);
		sw_printf(p, "try visitor.visitSingular%sField(value: v, fieldNumber: %d)", fg->proto_generic_type, fg->number);
		sw_outdent(p);
		sw_print(p, "} }()");
		return;
	}
	bool is_only_group = o->field_sorted_grouped.n == 1;
	sw_printf(p, "switch %s {", o->stored_property);
	for (int idx = 0; idx < group->n; idx++) {
		const FieldGen *f = group->v[idx];
		if (is_only_group && idx > 0 && idx % MAX_CASES_IN_SWITCH == 0) {
			sw_print(p, "default: break");
			sw_print(p, "}");
			sw_printf(p, "switch %s {", o->stored_property);
		}
		sw_printf(p, "case %s?: try {", f->dotted_swift_name);
		print_indented2(p, xfmt("guard case %s(let v)? = %s else { preconditionFailure() }", f->dotted_swift_name, o->stored_property),
				xfmt("try visitor.visitSingular%sField(value: v, fieldNumber: %d)", f->proto_generic_type, f->number));
		sw_print(p, "}()");
	}
	if (is_only_group && group->n <= MAX_CASES_IN_SWITCH)
		sw_print(p, "case nil: break");
	else
		sw_print(p, "default: break");
	sw_print(p, "}");
}

static void oneof_generate_field_comparison(SwPrinter *p, const FieldGen *fg)
{
	const OneofGen *o = fg->oneof;
	if (!oneof_first(fg))
		return;
	if (o->uses_heap_storage)
		sw_printf(p, "if _storage.%s != rhs_storage.%s {return false}", o->underscore_swift_field_name,
			  o->underscore_swift_field_name);
	else
		sw_printf(p, "if lhs.%s != rhs.%s {return false}", o->swift_field_name, o->swift_field_name);
}

static void oneof_generate_is_initialized_check(SwPrinter *p, const FieldGen *fg)
{
	const OneofGen *o = fg->oneof;
	if (!oneof_first(fg))
		return;
	for (int i = 0; i < o->fields.n; i++) {
		const FieldGen *f = o->fields.v[i];
		if (is_group_or_message(f->fd) && contains_required_fields(f->fd->message_type)) {
			sw_printf(p, "if let v = %s, !v.isInitialized {return false}", o->stored_property);
			return;
		}
	}
}
