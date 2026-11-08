// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* Port of src/google/protobuf/compiler/java/options.h, context.{h,cc} and field_common.{h,cc}.
 * The field and oneof names of the messages of the context's file are kept behind
 * ProtoMessage.user (upstream: maps by descriptor). */

JvOptions jv_default_options(void)
{
	JvOptions o = {0};
	o.annotation_list_file = "";
	o.output_list_file = "";
	o.jvm_dsl = true;
	return o;
}

/* ---- context.cc ---- */

/* EqualWithSuffix(name1, suffix, name2): name2 == name1 + suffix */
static bool equal_with_suffix(const char *name1, const char *suffix, const char *name2)
{
	if (!has_suffix(name2, suffix))
		return false;
	size_t n = strlen(name2) - strlen(suffix);
	return strlen(name1) == n && memcmp(name1, name2, n) == 0;
}

/* The conflict of a pair, rendered when needed (the reason strings of upstream). */
typedef enum { CONFLICT_NONE, CONFLICT_SAME_NAME, CONFLICT_COUNT, CONFLICT_LIST, CONFLICT_VALUE } ConflictKind;

typedef struct {
	ConflictKind kind;
	const LpField *field1, *field2; /* in the order of the reason text */
	const char *name1;
} Conflict;

/* IsConflictingOneWay */
static ConflictKind is_conflicting_one_way(const LpField *field1, const char *name1, const LpField *field2,
					   const char *name2)
{
	/* IsRepeatedFieldConflicting */
	if (proto_field_is_repeated(field1) && !proto_field_is_repeated(field2)) {
		if (equal_with_suffix(name1, "Count", name2))
			return CONFLICT_COUNT;
		if (equal_with_suffix(name1, "List", name2))
			return CONFLICT_LIST;
	}
	/* IsEnumFieldConflicting */
	if (field1->type == PT_ENUM && jv_support_unknown_enum_value(field1) && equal_with_suffix(name1, "Value", name2))
		return CONFLICT_VALUE;
	return CONFLICT_NONE;
}

static char *conflict_reason(const Conflict *c)
{
	switch (c->kind) {
	case CONFLICT_SAME_NAME:
		return xcat("capitalized name of field \"", c->field1->name, "\" conflicts with field \"", c->field2->name,
			    "\"");
	case CONFLICT_COUNT:
	case CONFLICT_LIST:
		return xcat("both repeated field \"", c->field1->name, "\" and singular ", "field \"", c->field2->name,
			    "\" generate the method \"", "get", c->name1, c->kind == CONFLICT_COUNT ? "Count()\"" : "List()\"");
	case CONFLICT_VALUE:
		return xcat("both enum field \"", c->field1->name, "\" and regular ", "field \"", c->field2->name,
			    "\" generate the method \"", "get", c->name1, "Value()\"");
	default: return "";
	}
}

/* InitializeFieldGeneratorInfoForFields. Upstream compares every pair of fields (quadratic in
 * time, as here); the names are computed once and the reasons are rendered at the end, so memory
 * stays linear. */
static void initialize_field_generator_info_for_fields(JvContext *c, const LpMessage *message)
{
	int n = message->n_fields;
	const char **names = xalloc(sizeof(char *) * (size_t)(n + 1));
	size_t *lens = xalloc(sizeof(size_t) * (size_t)(n + 1));
	for (int i = 0; i < n; i++) {
		names[i] = jv_capitalized_field_name(&message->fields[i]);
		lens[i] = strlen(names[i]);
	}
	bool *is_conflict = xalloc((size_t)(n + 1));
	Conflict *reason = xalloc(sizeof(Conflict) * (size_t)(n + 1));
	for (int i = 0; i < n; i++) {
		const LpField *field = &message->fields[i];
		for (int j = i + 1; j < n; j++) {
			const LpField *other = &message->fields[j];
			/* the names are equal, or one is the other plus "Count", "List" or "Value" */
			size_t a = lens[i], b = lens[j];
			if (a != b && a != b + 4 && a != b + 5 && b != a + 4 && b != a + 5)
				continue;
			if (a == b && str_eq(names[i], names[j])) {
				is_conflict[i] = is_conflict[j] = true;
				reason[i] = reason[j] = (Conflict){CONFLICT_SAME_NAME, field, other, NULL};
				continue;
			}
			ConflictKind k = is_conflicting_one_way(field, names[i], other, names[j]);
			Conflict r = {k, field, other, names[i]};
			if (k == CONFLICT_NONE) {
				k = is_conflicting_one_way(other, names[j], field, names[i]);
				r = (Conflict){k, other, field, names[j]};
			}
			if (k != CONFLICT_NONE) {
				is_conflict[i] = is_conflict[j] = true;
				reason[i] = reason[j] = r;
			}
		}
		/* ABSL_LOG(WARNING) of the conflict */
	}
	JvMessageData *md = jv_message_data(message);
	md->field_info = xalloc(sizeof(JvFieldGeneratorInfo) * (size_t)(n + 1));
	for (int i = 0; i < n; i++) {
		const LpField *field = &message->fields[i];
		JvFieldGeneratorInfo *info = &md->field_info[i];
		info->name = jv_camel_case_field_name(field);
		info->capitalized_name = names[i];
		info->disambiguated_reason = "";
		if (is_conflict[i]) {
			info->name = xcat(info->name, jv_itoa(field->number));
			info->capitalized_name = xcat(info->capitalized_name, jv_itoa(field->number));
			info->disambiguated_reason = conflict_reason(&reason[i]);
		}
		info->options = &c->options;
	}
}

static void initialize_field_generator_info_for_message(JvContext *c, const LpMessage *message)
{
	for (int i = 0; i < message->n_nested; i++)
		initialize_field_generator_info_for_message(c, &message->nested[i]);
	initialize_field_generator_info_for_fields(c, message);
	JvMessageData *md = jv_message_data(message);
	md->oneof_info = xalloc(sizeof(JvOneofGeneratorInfo) * (size_t)(message->n_oneofs + 1));
	for (int i = 0; i < message->n_oneofs; i++) {
		const LpOneof *oneof = &message->oneofs[i];
		md->oneof_info[i].name = jv_underscores_to_camel_case(oneof->name, false);
		md->oneof_info[i].capitalized_name = jv_underscores_to_camel_case(oneof->name, true);
	}
}

JvContext *jv_context_new(const LpFile *file, const JvOptions *options)
{
	JvContext *c = xalloc(sizeof *c);
	c->options = *options;
	c->file = file;
	for (int i = 0; i < file->n_messages; i++)
		initialize_field_generator_info_for_message(c, &file->messages[i]);
	return c;
}

const JvFieldGeneratorInfo *jv_context_field_info(const JvContext *c, const LpField *field)
{
	(void)c;
	const LpMessage *m = field->containing_type;
	JvMessageData *md = m && !field->is_extension ? jv_message_data(m) : NULL;
	if (!md || !md->field_info)
		fail("Can not find FieldGeneratorInfo for field: %s", field->full_name);
	return &md->field_info[field->index];
}

const JvOneofGeneratorInfo *jv_context_oneof_info(const JvContext *c, const LpOneof *oneof)
{
	(void)c;
	JvMessageData *md = jv_message_data(oneof->containing_type);
	if (!md->oneof_info)
		fail("Can not find OneofGeneratorInfo for oneof: %s", oneof->name);
	return &md->oneof_info[oneof->index];
}

bool jv_context_enforce_lite(const JvContext *c) { return c->options.enforce_lite; }

bool jv_context_has_generated_methods(const JvContext *c, const LpMessage *descriptor)
{
	return c->options.enforce_lite || jv_file_optimize_for(descriptor->file) != JV_OPTIMIZE_CODE_SIZE;
}

static void maybe_print_generated_annotation(JvContext *c, LpPrinter *p, bool is_own_file, const char *name,
					     const char *suffix)
{
	if (is_own_file)
		jv_print_generated_annotation(p, '$', c->options.annotate_code ? jv_annotation_file_name(name, suffix) : "",
					      &c->options);
}

void jv_maybe_print_generated_annotation_message(JvContext *c, LpPrinter *p, const LpMessage *d, bool immutable,
						 const char *suffix)
{
	maybe_print_generated_annotation(c, p, jv_is_own_file_message(d, immutable), d->name, suffix);
}

void jv_maybe_print_generated_annotation_enum(JvContext *c, LpPrinter *p, const LpEnum *d, bool immutable,
					      const char *suffix)
{
	maybe_print_generated_annotation(c, p, jv_is_own_file_enum(d, immutable), d->name, suffix);
}

void jv_maybe_print_generated_annotation_service(JvContext *c, LpPrinter *p, const LpService *d, bool immutable,
						 const char *suffix)
{
	maybe_print_generated_annotation(c, p, jv_is_own_file_service(d, immutable), d->name, suffix);
}

bool jv_get_source_location(const ProtoDesc *d, LpSourceLocation *out)
{
	const LpFile *f = proto_desc_file(d);
	JvFileData *fd = jv_file_data(f);
	if (!fd->locations)
		fd->locations = lp_source_locations(f);
	return lp_get_source_location(fd->locations, d, out);
}

/* ---- field_common.cc ---- */

static bool fc_is_upper(char c) { return (unsigned int)(c - 'A') <= 'Z' - 'A'; }
static char fc_to_lower(char c) { return fc_is_upper(c) ? (char)(c - 'A' + 'a') : c; }

char *jv_get_kotlin_property_name(const char *capitalized_name)
{
	char *kt_property_name = xstrdup(capitalized_name);
	size_t n = strlen(capitalized_name);
	size_t first_non_capital;
	for (first_non_capital = 0; first_non_capital < n && fc_is_upper(capitalized_name[first_non_capital]);
	     first_non_capital++) {
	}
	size_t stop = first_non_capital;
	if (stop > 1 && stop < n)
		stop--;
	for (size_t i = 0; i < stop; i++)
		kt_property_name[i] = fc_to_lower(kt_property_name[i]);
	return kt_property_name;
}

void jv_set_common_field_variables(const LpField *descriptor, const JvFieldGeneratorInfo *info, LpVars *variables)
{
	lp_vars_set(variables, "field_name", descriptor->name);
	lp_vars_set(variables, "name", info->name);
	lp_vars_set(variables, "classname", descriptor->containing_type->name);
	lp_vars_set(variables, "capitalized_name", info->capitalized_name);
	lp_vars_set(variables, "disambiguated_reason", info->disambiguated_reason);
	lp_vars_set(variables, "constant_name", jv_field_constant_name(descriptor));
	lp_vars_set(variables, "number", jv_itoa(descriptor->number));
	lp_vars_set(variables, "kt_dsl_builder", "_builder");
	lp_vars_set(variables, "{", "");
	lp_vars_set(variables, "}", "");
	lp_vars_set(variables, "kt_name", jv_is_forbidden_kotlin(info->name) ? xcat(info->name, "_") : info->name);
	char *kt_property_name = jv_get_kotlin_property_name(info->capitalized_name);
	lp_vars_set(variables, "kt_property_name", kt_property_name);
	lp_vars_set(variables, "kt_safe_name",
		    jv_is_forbidden_kotlin(kt_property_name) ? xcat("`", kt_property_name, "`") : kt_property_name);
	lp_vars_set(variables, "kt_capitalized_name",
		    jv_is_forbidden_kotlin(info->name) ? xcat(info->capitalized_name, "_") : info->capitalized_name);
	lp_vars_set(variables, "jvm_synthetic", jv_jvm_synthetic(info->options->jvm_dsl));
	if (!proto_field_is_repeated(descriptor)) {
		lp_vars_set(variables, "annotation_field_type", jv_field_type_name(descriptor->type));
	} else if (jv_get_java_type(descriptor) == JAVATYPE_MESSAGE && jv_is_map_entry(descriptor->message_type)) {
		lp_vars_set(variables, "annotation_field_type", xcat(jv_field_type_name(descriptor->type), "MAP"));
	} else {
		lp_vars_set(variables, "annotation_field_type", xcat(jv_field_type_name(descriptor->type), "_LIST"));
		if (proto_field_is_packed(descriptor))
			lp_vars_insert(variables, "annotation_field_type",
				       xcat(jv_field_type_name(descriptor->type), "_LIST_PACKED"));
	}
}

void jv_set_common_oneof_variables(const LpField *descriptor, const JvOneofGeneratorInfo *info, LpVars *variables)
{
	char *number = jv_itoa(descriptor->number);
	lp_vars_set(variables, "oneof_name", info->name);
	lp_vars_set(variables, "oneof_capitalized_name", info->capitalized_name);
	lp_vars_set(variables, "oneof_index", jv_itoa(descriptor->containing_oneof->index));
	lp_vars_set(variables, "oneof_stored_type", jv_get_oneof_stored_type(descriptor));
	lp_vars_set(variables, "set_oneof_case_message", xcat(info->name, "Case_ = ", number));
	lp_vars_set(variables, "clear_oneof_case_message", xcat(info->name, "Case_ = 0"));
	lp_vars_set(variables, "has_oneof_case_message", xcat(info->name, "Case_ == ", number));
	lp_vars_set(variables, "negated_has_oneof_case_message", xcat(info->name, "Case_ != ", number));
}

void jv_print_extra_field_info(const LpVars *variables, LpPrinter *p)
{
	const char *reason = lp_vars_get(variables, "disambiguated_reason");
	if (reason && *reason)
		lp_print_vars(p, variables,
			      "// An alternative name is used for field \"$field_name$\" because:\n"
			      "//     $disambiguated_reason$\n");
}

/* ---- message_serialization.{h,cc} ---- */

void jv_generate_serialize_extension_range(LpPrinter *p, const LpExtensionRange *range)
{
	lp_print(p, "extensionWriter.writeUntil($end$, output);\n", "end", jv_itoa(range->end), NULL);
}

static int cmp_range_start(const void *a, const void *b)
{
	const LpExtensionRange *x = *(const LpExtensionRange *const *)a, *y = *(const LpExtensionRange *const *)b;
	return x->start < y->start ? -1 : x->start > y->start;
}

void jv_generate_serialize_fields_and_extensions(LpPrinter *p, const void *const *gens, JvSerializeFn serialize,
						 const LpMessage *descriptor, const LpField **sorted_fields)
{
	int nr = descriptor->n_extension_ranges;
	const LpExtensionRange **sorted_extensions = xalloc(sizeof(LpExtensionRange *) * (size_t)(nr + 1));
	for (int i = 0; i < nr; i++)
		sorted_extensions[i] = &descriptor->extension_ranges[i];
	qsort(sorted_extensions, (size_t)nr, sizeof *sorted_extensions, cmp_range_start);
	int range_idx = 0;
	for (int i = 0; i < descriptor->n_fields; i++) {
		const LpField *field = sorted_fields[i];
		const LpExtensionRange *range = NULL;
		while (range_idx < nr && sorted_extensions[range_idx]->end <= field->number)
			range = sorted_extensions[range_idx++];
		if (range)
			jv_generate_serialize_extension_range(p, range);
		serialize(gens[field->index], p);
	}
	if (range_idx < nr)
		jv_generate_serialize_extension_range(p, sorted_extensions[nr - 1]);
}
