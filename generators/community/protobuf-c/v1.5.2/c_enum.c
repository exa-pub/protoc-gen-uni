// Copyright 2008 Google Inc.  All rights reserved.
// Copyright (c) 2008-2025, Dave Benson and the protobuf-c authors.  All rights reserved.
// Use of this source code is governed by the BSD-style licenses reproduced in generator.c.

/* Port of protoc-gen-c/c_enum.{h,cc} (protobuf-c v1.5.2): EnumGenerator. */

static void enum_generate_definition(const EnumGenerator *g, LpPrinter3 *printer)
{
	const LpEnum *d = g->descriptor;
	LpVars3 vars = {0};
	lp3_vars_set(&vars, "classname", full_name_to_c(d->full_name, d->file));
	lp3_vars_set(&vars, "shortname", d->name);
	lp3_vars_set(&vars, "uc_name", full_name_to_upper(d->full_name, d->file));

	Str leading, trailing;
	source_comments(d, &leading, &trailing);
	print_comment(printer, leading);

	lp3_print_vars(printer, &vars, "typedef enum _$classname$ {\n");
	lp3_indent(printer);

	lp3_vars_set(&vars, "opt_comma", ",");
	lp3_vars_set(&vars, "prefix", xcat(full_name_to_upper(d->full_name, d->file), "__"));
	for (int i = 0; i < d->n_values; i++) {
		const LpEnumValue *v = &d->values[i];
		lp3_vars_set(&vars, "name", v->name);
		lp3_vars_set(&vars, "number", simple_itoa(v->number));
		if (i + 1 == d->n_values)
			lp3_vars_set(&vars, "opt_comma", "");
		source_comments(v, &leading, &trailing);
		print_comment(printer, leading);
		print_comment(printer, trailing);
		lp3_print_vars(printer, &vars, "$prefix$$name$ = $number$$opt_comma$\n");
	}

	lp3_print_vars(printer, &vars, "  PROTOBUF_C__FORCE_ENUM_TO_BE_INT_SIZE($uc_name$)\n");
	lp3_outdent(printer);
	lp3_print_vars(printer, &vars, "} $classname$;\n");
}

static void enum_generate_descriptor_declarations(const EnumGenerator *g, LpPrinter3 *printer)
{
	const LpEnum *d = g->descriptor;
	LpVars3 vars = {0};
	lp3_vars_set(&vars, "dllexport", *g->dllexport_decl ? xcat(g->dllexport_decl, " ") : "");
	lp3_vars_set(&vars, "classname", full_name_to_c(d->full_name, d->file));
	lp3_vars_set(&vars, "lcclassname", full_name_to_lower(d->full_name, d->file));
	lp3_print_vars(printer, &vars, "extern $dllexport$const ProtobufCEnumDescriptor    $lcclassname$__descriptor;\n");
}

typedef struct {
	int value;
	unsigned index;
	unsigned final_index; /* index in uniqified array of values */
	const char *name;
} ValueIndex;

/* GenerateValueInitializer */
static void enum_generate_value_initializer(const EnumGenerator *g, LpPrinter3 *printer, int index)
{
	const LpEnum *d = g->descriptor;
	const LpEnumValue *vd = &d->values[index];
	LpVars3 vars = {0};
	lp3_vars_set(&vars, "enum_value_name", vd->name);
	lp3_vars_set(&vars, "c_enum_value_name", xcat(full_name_to_upper(d->full_name, d->file), "__", vd->name));
	lp3_vars_set(&vars, "value", simple_itoa(vd->number));
	if (optimize_code_size(d->file))
		lp3_print_vars(printer, &vars, "  { NULL, NULL, $value$ }, /* CODE_SIZE */\n");
	else
		lp3_print_vars(printer, &vars, "  { \"$enum_value_name$\", \"$c_enum_value_name$\", $value$ },\n");
}

static int compare_value_indices_by_value_then_index(const void *a, const void *b)
{
	const ValueIndex *vi_a = a, *vi_b = b;
	if (vi_a->value < vi_b->value)
		return -1;
	if (vi_a->value > vi_b->value)
		return +1;
	if (vi_a->index < vi_b->index)
		return -1;
	if (vi_a->index > vi_b->index)
		return +1;
	return 0;
}

/* std::string::compare */
static int compare_value_indices_by_name(const void *a, const void *b)
{
	return strcmp(((const ValueIndex *)a)->name, ((const ValueIndex *)b)->name);
}

static void enum_generate_enum_descriptor(const EnumGenerator *g, LpPrinter3 *printer)
{
	const LpEnum *d = g->descriptor;
	int value_count = d->n_values;
	LpVars3 vars = {0};
	lp3_vars_set(&vars, "fullname", d->full_name);
	lp3_vars_set(&vars, "lcclassname", full_name_to_lower(d->full_name, d->file));
	lp3_vars_set(&vars, "cname", full_name_to_c(d->full_name, d->file));
	lp3_vars_set(&vars, "shortname", d->name);
	lp3_vars_set(&vars, "packagename", d->file->package);
	lp3_vars_set(&vars, "value_count", simple_itoa(value_count));

	bool code_size = optimize_code_size(d->file);

	/* Sort by name and value, dropping duplicate values if they appear later. */
	ValueIndex *value_index = xalloc(sizeof(ValueIndex) * (size_t)(value_count + 1));
	for (int j = 0; j < value_count; j++)
		value_index[j] = (ValueIndex){d->values[j].number, (unsigned)j, 0, d->values[j].name};
	qsort(value_index, (size_t)value_count, sizeof(ValueIndex), compare_value_indices_by_value_then_index);

	/* only record unique values */
	int n_unique_values = 0;
	if (value_count > 0) {
		n_unique_values = 1;
		value_index[0].final_index = 0;
		for (int j = 1; j < value_count; j++) {
			if (value_index[j - 1].value != value_index[j].value)
				value_index[j].final_index = (unsigned)n_unique_values++;
			else
				value_index[j].final_index = (unsigned)n_unique_values - 1;
		}
	}

	lp3_vars_set(&vars, "unique_value_count", simple_itoa(n_unique_values));
	lp3_print_vars(printer, &vars,
		       "static const ProtobufCEnumValue $lcclassname$__enum_values_by_number[$unique_value_count$] =\n"
		       "{\n");
	if (value_count > 0) {
		enum_generate_value_initializer(g, printer, (int)value_index[0].index);
		for (int j = 1; j < value_count; j++)
			if (value_index[j - 1].value != value_index[j].value)
				enum_generate_value_initializer(g, printer, (int)value_index[j].index);
	}
	lp3_print_vars(printer, &vars, "};\n");
	lp3_print_vars(printer, &vars, "static const ProtobufCIntRange $lcclassname$__value_ranges[] = {\n");
	unsigned n_ranges = 0;
	if (value_count > 0) {
		unsigned range_start = 0;
		unsigned range_len = 1;
		int range_start_value = value_index[0].value;
		int last_value = range_start_value;
		for (int j = 1; j < value_count; j++) {
			if (value_index[j - 1].value != value_index[j].value) {
				if ((int64_t)last_value + 1 == value_index[j].value) { /* no overflow */
					range_len++;
				} else {
					/* output range */
					lp3_vars_set(&vars, "range_start_value", simple_itoa(range_start_value));
					lp3_vars_set(&vars, "orig_index", simple_utoa(range_start));
					lp3_print_vars(printer, &vars, "{$range_start_value$, $orig_index$},");
					range_start_value = value_index[j].value;
					range_start += range_len;
					range_len = 1;
					n_ranges++;
				}
				last_value = value_index[j].value;
			}
		}
		lp3_vars_set(&vars, "range_start_value", simple_itoa(range_start_value));
		lp3_vars_set(&vars, "orig_index", simple_utoa(range_start));
		lp3_print_vars(printer, &vars, "{$range_start_value$, $orig_index$},");
		range_start += range_len;
		n_ranges++;
		lp3_vars_set(&vars, "range_start_value", simple_itoa(0));
		lp3_vars_set(&vars, "orig_index", simple_utoa(range_start));
		lp3_print_vars(printer, &vars, "{$range_start_value$, $orig_index$}\n};\n");
	}
	lp3_vars_set(&vars, "n_ranges", simple_utoa(n_ranges));

	if (!code_size) {
		qsort(value_index, (size_t)value_count, sizeof(ValueIndex), compare_value_indices_by_name);
		lp3_print_vars(printer, &vars,
			       "static const ProtobufCEnumValueIndex $lcclassname$__enum_values_by_name[$value_count$] =\n"
			       "{\n");
		for (int j = 0; j < value_count; j++) {
			lp3_vars_set(&vars, "index", simple_utoa(value_index[j].final_index));
			lp3_vars_set(&vars, "name", value_index[j].name);
			lp3_print_vars(printer, &vars, "  { \"$name$\", $index$ },\n");
		}
		lp3_print_vars(printer, &vars, "};\n");
	}

	if (code_size)
		lp3_print_vars(printer, &vars,
			       "const ProtobufCEnumDescriptor $lcclassname$__descriptor =\n"
			       "{\n"
			       "  PROTOBUF_C__ENUM_DESCRIPTOR_MAGIC,\n"
			       "  NULL,NULL,NULL,NULL, /* CODE_SIZE */\n"
			       "  $unique_value_count$,\n"
			       "  $lcclassname$__enum_values_by_number,\n"
			       "  0, NULL, /* CODE_SIZE */\n"
			       "  $n_ranges$,\n"
			       "  $lcclassname$__value_ranges,\n"
			       "  NULL,NULL,NULL,NULL   /* reserved[1234] */\n"
			       "};\n");
	else
		lp3_print_vars(printer, &vars,
			       "const ProtobufCEnumDescriptor $lcclassname$__descriptor =\n"
			       "{\n"
			       "  PROTOBUF_C__ENUM_DESCRIPTOR_MAGIC,\n"
			       "  \"$fullname$\",\n"
			       "  \"$shortname$\",\n"
			       "  \"$cname$\",\n"
			       "  \"$packagename$\",\n"
			       "  $unique_value_count$,\n"
			       "  $lcclassname$__enum_values_by_number,\n"
			       "  $value_count$,\n"
			       "  $lcclassname$__enum_values_by_name,\n"
			       "  $n_ranges$,\n"
			       "  $lcclassname$__value_ranges,\n"
			       "  NULL,NULL,NULL,NULL   /* reserved[1234] */\n"
			       "};\n");
}
