// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* Port of src/google/protobuf/compiler/csharp/csharp_enum.{h,cc} (protobuf v36.2): EnumGenerator.
 * The warning upstream logs for a duplicate value name is not printed. */

static void cs_enum_generate(const LpEnum *descriptor, const CsOptions *options, LpPrinter *printer)
{
	cs_write_doc_comment(printer, options, descriptor);
	if (pb_int(descriptor->options, ENUMOPT_DEPRECATED, 0))
		lp_print(printer, "[global::System.ObsoleteAttribute]\n", NULL);
	lp_print(printer, "$access_level$ enum $name$ {\n", "access_level", cs_class_access_level(options), "name",
		 descriptor->name, NULL);
	lp_indent(printer);
	Map used_names = {0};
	Map used_number = {0};
	Map tried_underscores = {0}; /* name -> size_t* */
	for (int i = 0; i < descriptor->n_values; i++) {
		const LpEnumValue *value = &descriptor->values[i];
		cs_write_doc_comment(printer, options, value);
		if (pb_int(value->options, ENUMVALOPT_DEPRECATED, 0))
			lp_print(printer, "[global::System.ObsoleteAttribute]\n", NULL);
		const char *original_name = value->name;
		char *name = cs_get_enum_value_name(descriptor->name, value->name);
		/* Make sure we don't get any duplicate names due to prefix removal: upstream appends
		 * "_" until the name is unused. The names only get used, so the search resumes at the
		 * underscores the previous search of the same name stopped at. */
		size_t *underscores = map_get(&tried_underscores, name);
		if (!underscores) {
			underscores = xalloc(sizeof *underscores);
			map_put(&tried_underscores, name, underscores);
		}
		Buf candidate = {0};
		buf_adds(&candidate, name);
		for (size_t k = 0; k < *underscores; k++)
			buf_addc(&candidate, '_');
		while (map_has(&used_names, buf_str(&candidate))) {
			buf_addc(&candidate, '_');
			++*underscores;
		}
		name = xstrdup(buf_str(&candidate));
		map_put(&used_names, name, name);
		char *number = cs_itoa(value->number);
		if (map_has(&used_number, number)) {
			lp_print(printer, "[pbr::OriginalName(\"$original_name$\", PreferredAlias = false)] $name$ = $number$,\n",
				 "original_name", original_name, "name", name, "number", number, NULL);
		} else {
			map_put(&used_number, number, number);
			lp_print(printer, "[pbr::OriginalName(\"$original_name$\")] $name$ = $number$,\n", "original_name",
				 original_name, "name", name, "number", number, NULL);
		}
	}
	lp_outdent(printer);
	lp_print(printer, "}\n", NULL);
	lp_print(printer, "\n", NULL);
}
