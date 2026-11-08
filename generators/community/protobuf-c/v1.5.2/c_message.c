// Copyright 2008 Google Inc.  All rights reserved.
// Copyright (c) 2008-2025, Dave Benson and the protobuf-c authors.  All rights reserved.
// Use of this source code is governed by the BSD-style licenses reproduced in generator.c.

/* Port of protoc-gen-c/c_message.{h,cc} (protobuf-c v1.5.2): MessageGenerator. The
 * ExtensionGenerators of the messages (c_extension.cc) print nothing and are left out. */

static void message_generator_init(MessageGenerator *g, const LpMessage *descriptor, const char *dllexport_decl)
{
	g->descriptor = descriptor;
	g->dllexport_decl = dllexport_decl;
	field_generator_map_init(&g->field_generators, descriptor);
	g->nested_generators = xalloc(sizeof(MessageGenerator) * (size_t)(descriptor->n_nested + 1));
	for (int i = 0; i < descriptor->n_nested; i++)
		message_generator_init(&g->nested_generators[i], &descriptor->nested[i], dllexport_decl);
	g->enum_generators = xalloc(sizeof(EnumGenerator) * (size_t)(descriptor->n_enums + 1));
	for (int i = 0; i < descriptor->n_enums; i++)
		g->enum_generators[i] = (EnumGenerator){&descriptor->enums[i], dllexport_decl};
}

static void message_generate_struct_typedef(const MessageGenerator *g, LpPrinter3 *printer)
{
	const LpMessage *d = g->descriptor;
	lp3_print(printer, "typedef struct $classname$ $classname$;\n", "classname", full_name_to_c(d->full_name, d->file),
		  NULL);
	for (int i = 0; i < d->n_nested; i++)
		message_generate_struct_typedef(&g->nested_generators[i], printer);
}

static void message_generate_enum_definitions(const MessageGenerator *g, LpPrinter3 *printer)
{
	const LpMessage *d = g->descriptor;
	for (int i = 0; i < d->n_nested; i++)
		message_generate_enum_definitions(&g->nested_generators[i], printer);
	for (int i = 0; i < d->n_enums; i++)
		enum_generate_definition(&g->enum_generators[i], printer);
}

/* GetOneofUnionOrder */
static int get_oneof_union_order(const LpField *fd)
{
	switch (lp_field_cpp_type(fd)) {
	case LP_CPPTYPE_STRING:
		if (fd->type == PT_BYTES)
			return 1;
		if (fd->type == PT_STRING)
			return 4;
		fail("%s: Unhandled combination of CPPTYPE (%d) and protobuf type (%d)", fd->full_name, LP_CPPTYPE_STRING,
		     fd->type);
	case LP_CPPTYPE_DOUBLE: return 2;
	case LP_CPPTYPE_INT64:
	case LP_CPPTYPE_UINT64: return 3;
	case LP_CPPTYPE_MESSAGE: return 5;
	case LP_CPPTYPE_FLOAT: return 6;
	case LP_CPPTYPE_INT32:
	case LP_CPPTYPE_UINT32: return 7;
	case LP_CPPTYPE_ENUM: return 8;
	case LP_CPPTYPE_BOOL: return 9;
	default: fail("%s: Unhandled CPPTYPE %d", fd->full_name, lp_field_cpp_type(fd));
	}
}

typedef struct {
	int order;
	const char *name;
	const LpField *field;
} OneofOrder;

/* std::sort of std::tuple<int, std::string, const FieldDescriptor *> (names are unique) */
static int compare_oneof_order(const void *a, const void *b)
{
	const OneofOrder *x = a, *y = b;
	if (x->order != y->order)
		return x->order < y->order ? -1 : 1;
	return strcmp(x->name, y->name);
}

static void print_field_comments(LpPrinter3 *printer, const LpField *field)
{
	Str leading, trailing;
	source_comments(field, &leading, &trailing);
	print_comment(printer, leading);
	print_comment(printer, trailing);
}

static void message_generate_struct_definition(const MessageGenerator *g, LpPrinter3 *printer)
{
	const LpMessage *d = g->descriptor;
	for (int i = 0; i < d->n_nested; i++)
		message_generate_struct_definition(&g->nested_generators[i], printer);

	LpVars3 vars = {0};
	lp3_vars_set(&vars, "classname", full_name_to_c(d->full_name, d->file));
	lp3_vars_set(&vars, "lcclassname", full_name_to_lower(d->full_name, d->file));
	lp3_vars_set(&vars, "ucclassname", full_name_to_upper(d->full_name, d->file));
	lp3_vars_set(&vars, "field_count", simple_itoa(d->n_fields));
	lp3_vars_set(&vars, "dllexport", *g->dllexport_decl ? xcat(g->dllexport_decl, " ") : "");

	/* Generate the case enums for unions */
	for (int i = 0; i < d->n_oneofs; i++) {
		const LpOneof *oneof = &d->oneofs[i];
		lp3_vars_set(&vars, "opt_comma", ",");
		lp3_vars_set(&vars, "oneofname", camel_to_upper(oneof->name));
		lp3_vars_set(&vars, "foneofname", full_name_to_c(oneof->full_name, d->file));

		lp3_print(printer, "typedef enum {\n", NULL);
		lp3_indent(printer);
		lp3_print_vars(printer, &vars, "$ucclassname$__$oneofname$__NOT_SET = 0,\n");
		for (int j = 0; j < oneof->n_fields; j++) {
			const LpField *field = proto_oneof_field(oneof, j);
			lp3_vars_set(&vars, "fieldname", camel_to_upper(field->name));
			lp3_vars_set(&vars, "fieldnum", simple_itoa(field->number));
			if (j == oneof->n_fields - 1)
				lp3_vars_set(&vars, "opt_comma", "");
			lp3_print_vars(printer, &vars, "$ucclassname$__$oneofname$_$fieldname$ = $fieldnum$$opt_comma$\n");
		}
		lp3_print_vars(printer, &vars, "  PROTOBUF_C__FORCE_ENUM_TO_BE_INT_SIZE($ucclassname$__$oneofname$__CASE)\n");
		lp3_outdent(printer);
		lp3_print_vars(printer, &vars, "} $foneofname$Case;\n\n");
	}

	Str leading, trailing;
	source_comments(d, &leading, &trailing);
	print_comment(printer, leading);

	PbcMessageOptions opt = pbc_message_options(d);
	lp3_vars_set(&vars, "base", opt.base_field_name);

	lp3_print_vars(printer, &vars,
		       "struct $dllexport$ $classname$\n"
		       "{\n"
		       "  ProtobufCMessage $base$;\n");

	/* Generate fields. */
	lp3_indent(printer);
	for (int i = 0; i < d->n_fields; i++) {
		const LpField *field = &d->fields[i];
		if (!field->containing_oneof) {
			print_field_comments(printer, field);
			field_generate_struct_members(field_generator_map_get(&g->field_generators, field), printer);
		}
	}

	/* Generate unions from oneofs. */
	for (int i = 0; i < d->n_oneofs; i++) {
		const LpOneof *oneof = &d->oneofs[i];
		lp3_vars_set(&vars, "oneofname", camel_to_lower(oneof->name));
		lp3_vars_set(&vars, "foneofname", full_name_to_c(oneof->full_name, d->file));

		lp3_print_vars(printer, &vars, "$foneofname$Case $oneofname$_case;\n");
		lp3_print(printer, "union {\n", NULL);
		lp3_indent(printer);

		OneofOrder *sorted_fds = xalloc(sizeof(OneofOrder) * (size_t)(oneof->n_fields + 1));
		for (int j = 0; j < oneof->n_fields; j++) {
			const LpField *field = proto_oneof_field(oneof, j);
			sorted_fds[j] = (OneofOrder){get_oneof_union_order(field), field->name, field};
		}
		qsort(sorted_fds, (size_t)oneof->n_fields, sizeof(OneofOrder), compare_oneof_order);
		for (int j = 0; j < oneof->n_fields; j++) {
			const LpField *field = sorted_fds[j].field;
			print_field_comments(printer, field);
			field_generate_struct_members(field_generator_map_get(&g->field_generators, field), printer);
		}

		lp3_outdent(printer);
		lp3_print_vars(printer, &vars, "};\n");
	}
	lp3_outdent(printer);

	lp3_print_vars(printer, &vars, "};\n");

	for (int i = 0; i < d->n_fields; i++) {
		const LpField *field = &d->fields[i];
		if (field->has_default_value)
			field_generate_default_value_declarations(field_generator_map_get(&g->field_generators, field),
								  printer);
	}

	lp3_print_vars(printer, &vars,
		       "#define $ucclassname$__INIT \\\n"
		       " { PROTOBUF_C_MESSAGE_INIT (&$lcclassname$__descriptor) \\\n    ");

	for (int i = 0; i < d->n_fields; i++) {
		const LpField *field = &d->fields[i];
		if (!field->containing_oneof) {
			lp3_print(printer, ", ", NULL);
			field_generate_static_init(field_generator_map_get(&g->field_generators, field), printer);
		}
	}

	for (int i = 0; i < d->n_oneofs; i++) {
		const LpOneof *oneof = &d->oneofs[i];
		lp3_vars_set(&vars, "foneofname", full_name_to_upper(oneof->full_name, d->file));

		/* Initialize the case enum */
		lp3_print_vars(printer, &vars, ", $foneofname$__NOT_SET");

		/* Initialize the union */
		bool want_extra_braces = false;
		for (int j = 0; j < oneof->n_fields; j++) {
			const LpField *field = proto_oneof_field(oneof, j);
			if (lp_field_cpp_type(field) == LP_CPPTYPE_STRING && field->type == PT_BYTES)
				want_extra_braces = true;
		}
		lp3_print(printer, want_extra_braces ? ", { {0} }" : ", {0}", NULL);
	}

	lp3_print(printer, " }\n\n\n", NULL);
}

static void message_generate_helper_function_declarations(const MessageGenerator *g, LpPrinter3 *printer,
							  bool is_pack_deep, bool gen_pack, bool gen_init)
{
	const LpMessage *d = g->descriptor;
	PbcMessageOptions opt = pbc_message_options(d);

	/* Override parent settings, if needed */
	if (opt.has_gen_pack_helpers)
		gen_pack = opt.gen_pack_helpers;
	if (opt.has_gen_init_helpers)
		gen_init = opt.gen_init_helpers;

	for (int i = 0; i < d->n_nested; i++) {
		bool nested_pack = !is_pack_deep ? opt.gen_pack_helpers : gen_pack;
		message_generate_helper_function_declarations(&g->nested_generators[i], printer, true, nested_pack,
							      gen_init);
	}

	LpVars3 vars = {0};
	lp3_vars_set(&vars, "classname", full_name_to_c(d->full_name, d->file));
	lp3_vars_set(&vars, "lcclassname", full_name_to_lower(d->full_name, d->file));
	if (gen_init)
		lp3_print_vars(printer, &vars,
			       "/* $classname$ methods */\n"
			       "void   $lcclassname$__init\n"
			       "                     ($classname$         *message);\n");
	if (gen_pack)
		lp3_print_vars(printer, &vars,
			       "size_t $lcclassname$__get_packed_size\n"
			       "                     (const $classname$   *message);\n"
			       "size_t $lcclassname$__pack\n"
			       "                     (const $classname$   *message,\n"
			       "                      uint8_t             *out);\n"
			       "size_t $lcclassname$__pack_to_buffer\n"
			       "                     (const $classname$   *message,\n"
			       "                      ProtobufCBuffer     *buffer);\n"
			       "$classname$ *\n"
			       "       $lcclassname$__unpack\n"
			       "                     (ProtobufCAllocator  *allocator,\n"
			       "                      size_t               len,\n"
			       "                      const uint8_t       *data);\n"
			       "void   $lcclassname$__free_unpacked\n"
			       "                     ($classname$ *message,\n"
			       "                      ProtobufCAllocator *allocator);\n");
}

static void message_generate_descriptor_declarations(const MessageGenerator *g, LpPrinter3 *printer)
{
	const LpMessage *d = g->descriptor;
	lp3_print(printer, "extern const ProtobufCMessageDescriptor $name$__descriptor;\n", "name",
		  full_name_to_lower(d->full_name, d->file), NULL);
	for (int i = 0; i < d->n_nested; i++)
		message_generate_descriptor_declarations(&g->nested_generators[i], printer);
	for (int i = 0; i < d->n_enums; i++)
		enum_generate_descriptor_declarations(&g->enum_generators[i], printer);
}

static void message_generate_closure_typedef(const MessageGenerator *g, LpPrinter3 *printer)
{
	const LpMessage *d = g->descriptor;
	for (int i = 0; i < d->n_nested; i++)
		message_generate_closure_typedef(&g->nested_generators[i], printer);
	LpVars3 vars = {0};
	lp3_vars_set(&vars, "name", full_name_to_c(d->full_name, d->file));
	lp3_print_vars(printer, &vars,
		       "typedef void (*$name$_Closure)\n"
		       "                 (const $name$ *message,\n"
		       "                  void *closure_data);\n");
}

static int compare_pfields_by_number(const void *a, const void *b)
{
	const LpField *pa = *(const LpField *const *)a, *pb = *(const LpField *const *)b;
	if (pa->number < pb->number)
		return -1;
	if (pa->number > pb->number)
		return +1;
	return 0;
}

static void message_generate_helper_function_definitions(const MessageGenerator *g, LpPrinter3 *printer,
							 bool is_pack_deep, bool gen_pack, bool gen_init)
{
	const LpMessage *d = g->descriptor;
	PbcMessageOptions opt = pbc_message_options(d);

	/* Override parent settings, if needed */
	if (opt.has_gen_pack_helpers)
		gen_pack = opt.gen_pack_helpers;
	if (opt.has_gen_init_helpers)
		gen_init = opt.gen_init_helpers;

	for (int i = 0; i < d->n_nested; i++) {
		bool nested_pack = !is_pack_deep ? opt.gen_pack_helpers : gen_pack;
		message_generate_helper_function_definitions(&g->nested_generators[i], printer, true, nested_pack,
							     gen_init);
	}

	LpVars3 vars = {0};
	lp3_vars_set(&vars, "classname", full_name_to_c(d->full_name, d->file));
	lp3_vars_set(&vars, "lcclassname", full_name_to_lower(d->full_name, d->file));
	lp3_vars_set(&vars, "ucclassname", full_name_to_upper(d->full_name, d->file));
	lp3_vars_set(&vars, "base", opt.base_field_name);
	if (gen_init)
		lp3_print_vars(printer, &vars,
			       "void   $lcclassname$__init\n"
			       "                     ($classname$         *message)\n"
			       "{\n"
			       "  static const $classname$ init_value = $ucclassname$__INIT;\n"
			       "  *message = init_value;\n"
			       "}\n");
	if (gen_pack)
		lp3_print_vars(printer, &vars,
			       "size_t $lcclassname$__get_packed_size\n"
			       "                     (const $classname$ *message)\n"
			       "{\n"
			       "  assert(message->$base$.descriptor == &$lcclassname$__descriptor);\n"
			       "  return protobuf_c_message_get_packed_size ((const ProtobufCMessage*)(message));\n"
			       "}\n"
			       "size_t $lcclassname$__pack\n"
			       "                     (const $classname$ *message,\n"
			       "                      uint8_t       *out)\n"
			       "{\n"
			       "  assert(message->$base$.descriptor == &$lcclassname$__descriptor);\n"
			       "  return protobuf_c_message_pack ((const ProtobufCMessage*)message, out);\n"
			       "}\n"
			       "size_t $lcclassname$__pack_to_buffer\n"
			       "                     (const $classname$ *message,\n"
			       "                      ProtobufCBuffer *buffer)\n"
			       "{\n"
			       "  assert(message->$base$.descriptor == &$lcclassname$__descriptor);\n"
			       "  return protobuf_c_message_pack_to_buffer ((const ProtobufCMessage*)message, buffer);\n"
			       "}\n"
			       "$classname$ *\n"
			       "       $lcclassname$__unpack\n"
			       "                     (ProtobufCAllocator  *allocator,\n"
			       "                      size_t               len,\n"
			       "                      const uint8_t       *data)\n"
			       "{\n"
			       "  return ($classname$ *)\n"
			       "     protobuf_c_message_unpack (&$lcclassname$__descriptor,\n"
			       "                                allocator, len, data);\n"
			       "}\n"
			       "void   $lcclassname$__free_unpacked\n"
			       "                     ($classname$ *message,\n"
			       "                      ProtobufCAllocator *allocator)\n"
			       "{\n"
			       "  if(!message)\n"
			       "    return;\n"
			       "  assert(message->$base$.descriptor == &$lcclassname$__descriptor);\n"
			       "  protobuf_c_message_free_unpacked ((ProtobufCMessage*)message, allocator);\n"
			       "}\n");
}

typedef struct {
	unsigned index;
	const char *name;
} NameIndex;

/* compare_name_indices_by_name (std::string::compare) */
static int compare_name_indices_by_name(const void *a, const void *b)
{
	return strcmp(((const NameIndex *)a)->name, ((const NameIndex *)b)->name);
}

static void message_generate_message_descriptor(const MessageGenerator *g, LpPrinter3 *printer, bool gen_init)
{
	const LpMessage *d = g->descriptor;
	LpVars3 vars = {0};
	lp3_vars_set(&vars, "fullname", d->full_name);
	lp3_vars_set(&vars, "classname", full_name_to_c(d->full_name, d->file));
	char *lcclassname = full_name_to_lower(d->full_name, d->file);
	lp3_vars_set(&vars, "lcclassname", lcclassname);
	lp3_vars_set(&vars, "shortname", to_camel(d->name));
	lp3_vars_set(&vars, "n_fields", simple_itoa(d->n_fields));
	lp3_vars_set(&vars, "packagename", d->file->package);

	bool code_size = optimize_code_size(d->file);

	PbcMessageOptions opt = pbc_message_options(d);
	/* Override parent settings, if needed */
	if (opt.has_gen_init_helpers)
		gen_init = opt.gen_init_helpers;

	for (int i = 0; i < d->n_nested; i++)
		message_generate_message_descriptor(&g->nested_generators[i], printer, gen_init);
	for (int i = 0; i < d->n_enums; i++)
		enum_generate_enum_descriptor(&g->enum_generators[i], printer);

	for (int i = 0; i < d->n_fields; i++) {
		const LpField *fd = &d->fields[i];
		if (fd->has_default_value)
			field_generate_default_value_implementations(field_generator_map_get(&g->field_generators, fd),
								     printer);
	}

	for (int i = 0; i < d->n_fields; i++) {
		const LpField *fd = &d->fields[i];
		if (!fd->has_default_value)
			continue;
		bool already_defined = false;
		lp3_vars_set(&vars, "name", fd->name);
		lp3_vars_set(&vars, "lcname", camel_to_lower(fd->name));
		lp3_vars_set(&vars, "maybe_static", "static ");
		lp3_vars_set(&vars, "field_dv_ctype_suffix", "");
		lp3_vars_set(&vars, "default_value",
			     field_get_default_value(field_generator_map_get(&g->field_generators, fd)));
		switch (lp_field_cpp_type(fd)) {
		case LP_CPPTYPE_INT32: lp3_vars_set(&vars, "field_dv_ctype", "int32_t"); break;
		case LP_CPPTYPE_INT64: lp3_vars_set(&vars, "field_dv_ctype", "int64_t"); break;
		case LP_CPPTYPE_UINT32: lp3_vars_set(&vars, "field_dv_ctype", "uint32_t"); break;
		case LP_CPPTYPE_UINT64: lp3_vars_set(&vars, "field_dv_ctype", "uint64_t"); break;
		case LP_CPPTYPE_FLOAT: lp3_vars_set(&vars, "field_dv_ctype", "float"); break;
		case LP_CPPTYPE_DOUBLE: lp3_vars_set(&vars, "field_dv_ctype", "double"); break;
		case LP_CPPTYPE_BOOL: lp3_vars_set(&vars, "field_dv_ctype", "protobuf_c_boolean"); break;
		case LP_CPPTYPE_MESSAGE: fail("Messages can't have default values!"); /* NOTE: not supported by protobuf */
		case LP_CPPTYPE_STRING:
			if (fd->type == PT_BYTES || pbc_field_options(fd).string_as_bytes) {
				lp3_vars_set(&vars, "field_dv_ctype", "ProtobufCBinaryData");
			} else {
				/* STRING type */
				already_defined = true;
				lp3_vars_set(&vars, "maybe_static", "");
				lp3_vars_set(&vars, "field_dv_ctype", "char");
				lp3_vars_set(&vars, "field_dv_ctype_suffix", "[]");
			}
			break;
		case LP_CPPTYPE_ENUM: {
			const LpEnumValue *vd = fd->default_value_enum;
			lp3_vars_set(&vars, "field_dv_ctype", full_name_to_c(vd->type->full_name, vd->type->file));
			break;
		}
		default: fail("Unknown CPPTYPE");
		}
		if (!already_defined)
			lp3_print_vars(printer, &vars,
				       "$maybe_static$const $field_dv_ctype$ $lcclassname$__$lcname$__default_value"
				       "$field_dv_ctype_suffix$ = $default_value$;\n");
	}

	if (d->n_fields) {
		lp3_print_vars(printer, &vars,
			       "static const ProtobufCFieldDescriptor $lcclassname$__field_descriptors[$n_fields$] =\n"
			       "{\n");
		lp3_indent(printer);
		const LpField **sorted_fields = xalloc(sizeof(LpField *) * (size_t)d->n_fields);
		for (int i = 0; i < d->n_fields; i++)
			sorted_fields[i] = &d->fields[i];
		qsort(sorted_fields, (size_t)d->n_fields, sizeof(LpField *), compare_pfields_by_number);
		for (int i = 0; i < d->n_fields; i++)
			field_generate_descriptor_initializer(field_generator_map_get(&g->field_generators, sorted_fields[i]),
							      printer);
		lp3_outdent(printer);
		lp3_print_vars(printer, &vars, "};\n");

		if (!code_size) {
			NameIndex *field_indices = xalloc(sizeof(NameIndex) * (size_t)d->n_fields);
			for (int i = 0; i < d->n_fields; i++)
				field_indices[i] = (NameIndex){(unsigned)i, sorted_fields[i]->name};
			qsort(field_indices, (size_t)d->n_fields, sizeof(NameIndex), compare_name_indices_by_name);
			lp3_print_vars(printer, &vars, "static const unsigned $lcclassname$__field_indices_by_name[] = {\n");
			for (int i = 0; i < d->n_fields; i++) {
				lp3_vars_set(&vars, "index", simple_utoa(field_indices[i].index));
				lp3_vars_set(&vars, "name", field_indices[i].name);
				lp3_print_vars(printer, &vars, "  $index$,   /* field[$index$] = $name$ */\n");
			}
			lp3_print(printer, "};\n", NULL);
		}

		/* create range initializers */
		int *values = xalloc(sizeof(int) * (size_t)d->n_fields);
		for (int i = 0; i < d->n_fields; i++)
			values[i] = sorted_fields[i]->number;
		unsigned n_ranges = write_int_ranges(printer, d->n_fields, values, xcat(lcclassname, "__number_ranges"));
		lp3_vars_set(&vars, "n_ranges", simple_utoa(n_ranges));
	} else {
		/* MS compiler can't handle arrays with zero size and empty initialization list.
		 * Furthermore it is an extension of GCC only but not a standard. */
		lp3_vars_set(&vars, "n_ranges", "0");
		lp3_print_vars(printer, &vars,
			       "#define $lcclassname$__field_descriptors NULL\n"
			       "#define $lcclassname$__field_indices_by_name NULL\n"
			       "#define $lcclassname$__number_ranges NULL\n");
	}

	lp3_print_vars(printer, &vars,
		       "const ProtobufCMessageDescriptor $lcclassname$__descriptor =\n"
		       "{\n"
		       "  PROTOBUF_C__MESSAGE_DESCRIPTOR_MAGIC,\n");
	if (code_size)
		lp3_print(printer, "  NULL,NULL,NULL,NULL, /* CODE_SIZE */\n", NULL);
	else
		lp3_print_vars(printer, &vars,
			       "  \"$fullname$\",\n"
			       "  \"$shortname$\",\n"
			       "  \"$classname$\",\n"
			       "  \"$packagename$\",\n");
	lp3_print_vars(printer, &vars,
		       "  sizeof($classname$),\n"
		       "  $n_fields$,\n"
		       "  $lcclassname$__field_descriptors,\n");
	if (code_size)
		lp3_print(printer, "  NULL, /* CODE_SIZE */\n", NULL);
	else
		lp3_print_vars(printer, &vars, "  $lcclassname$__field_indices_by_name,\n");
	lp3_print_vars(printer, &vars,
		       "  $n_ranges$,"
		       "  $lcclassname$__number_ranges,\n");
	if (gen_init)
		lp3_print_vars(printer, &vars, "  (ProtobufCMessageInit) $lcclassname$__init,\n");
	else
		lp3_print_vars(printer, &vars, "  NULL, /* gen_init_helpers = false */\n");
	lp3_print_vars(printer, &vars,
		       "  NULL,NULL,NULL    /* reserved[123] */\n"
		       "};\n");
}
