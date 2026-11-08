/* Copyright (c) 2011 Petteri Aimonen <jpa at nanopb.mail.kapsi.fi>
 *
 * This software is provided 'as-is', without any express or
 * implied warranty. In no event will the authors be held liable
 * for any damages arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any
 * purpose, including commercial applications, and to alter it and
 * redistribute it freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you
 *    must not claim that you wrote the original software. If you use
 *    this software in a product, an acknowledgment in the product
 *    documentation would be appreciated but is not required.
 *
 * 2. Altered source versions must be plainly marked as such, and
 *    must not be misrepresented as being the original software.
 *
 * 3. This notice may not be removed or altered from any source
 *    distribution. */

/* parse_file: the -s settings, merged with text_format.Merge into the top-level NanoPBOptions. */

static const PypbEnumValue ev_n_fieldtype[] = {
	{"FT_DEFAULT", 0}, {"FT_CALLBACK", 1}, {"FT_POINTER", 4}, {"FT_STATIC", 2}, {"FT_IGNORE", 3}, {"FT_INLINE", 5},
};
static const PypbEnumValue ev_n_intsize[] = {{"IS_DEFAULT", 0}, {"IS_8", 8}, {"IS_16", 16}, {"IS_32", 32}, {"IS_64", 64}};
static const PypbEnumValue ev_n_mangling[] = {
	{"M_NONE", 0}, {"M_STRIP_PACKAGE", 1}, {"M_FLATTEN", 2}, {"M_PACKAGE_INITIALS", 3},
};
static const PypbEnumValue ev_n_dsize[] = {{"DS_AUTO", 0}, {"DS_1", 1}, {"DS_2", 2}, {"DS_4", 4}, {"DS_8", 8}};
static const PypbEnumValue ev_n_type[] = {
	{"TYPE_DOUBLE", 1}, {"TYPE_FLOAT", 2}, {"TYPE_INT64", 3}, {"TYPE_UINT64", 4}, {"TYPE_INT32", 5},
	{"TYPE_FIXED64", 6}, {"TYPE_FIXED32", 7}, {"TYPE_BOOL", 8}, {"TYPE_STRING", 9}, {"TYPE_GROUP", 10},
	{"TYPE_MESSAGE", 11}, {"TYPE_BYTES", 12}, {"TYPE_UINT32", 13}, {"TYPE_ENUM", 14}, {"TYPE_SFIXED32", 15},
	{"TYPE_SFIXED64", 16}, {"TYPE_SINT32", 17}, {"TYPE_SINT64", 18},
};
static const PypbEnumValue ev_n_label[] = {{"LABEL_OPTIONAL", 1}, {"LABEL_REPEATED", 3}, {"LABEL_REQUIRED", 2}};
#define PE(name, vals) {name, (int)(sizeof(vals) / sizeof(vals[0])), vals}
static const PypbEnum e_fieldtype = PE("FieldType", ev_n_fieldtype), e_intsize = PE("IntSize", ev_n_intsize),
	e_mangling = PE("TypenameMangling", ev_n_mangling), e_dsize = PE("DescriptorSize", ev_n_dsize),
	e_type = PE("google.protobuf.FieldDescriptorProto.Type", ev_n_type),
	e_label = PE("google.protobuf.FieldDescriptorProto.Label", ev_n_label);

/* The names of the NanoPBOptions fields by number. */
static const char nb_field_names[] =
	"max_size\0max_count\0type\0long_names\0packed_struct\0skip_message\0int_size\0no_unions\0msgid\0"
	"packed_enum\0anonymous_oneof\0proto3\0enum_to_string\0max_length\0fixed_length\0fixed_count\0"
	"mangle_names\0callback_datatype\0callback_function\0descriptorsize\0proto3_singular_msgs\0"
	"submsg_callback\0default_has\0include\0package\0exclude\0type_override\0sort_by_tag\0"
	"fallback_type\0initializer\0label_override\0enum_validate\0discard_unused_automatic_types\0"
	"enum_intsize\0discard_deprecated";

static const PypbEnum *nb_field_enum(int num)
{
	switch (num) {
	case NO_TYPE: case NO_FALLBACK_TYPE: return &e_fieldtype;
	case NO_INT_SIZE: case NO_ENUM_INTSIZE: return &e_intsize;
	case NO_MANGLE_NAMES: return &e_mangling;
	case NO_DESCRIPTORSIZE: return &e_dsize;
	case NO_TYPE_OVERRIDE: return &e_type;
	default: return &e_label;
	}
}

/* The NanoPBOptions message for text_format. */
static COLD PypbMessage nb_text_message(void)
{
	PypbField *f = xalloc(sizeof *f * NO_LAST);
	const char *name = nb_field_names;
	static const uint8_t types[] = {[K_INT32] = PT_INT32, [K_UINT32] = PT_UINT32, [K_BOOL] = PT_BOOL,
		[K_STRING] = PT_STRING, [K_ENUM] = PT_ENUM, [K_REPSTR] = PT_STRING};
	for (int num = 1; num <= NO_LAST; num++, name += strlen(name) + 1) {
		int k = no_kind[num];
		f[num - 1] = (PypbField){name, types[k], k == K_REPSTR, k == K_ENUM ? nb_field_enum(num) : NULL};
	}
	return (PypbMessage){"NanoPBOptions", NO_LAST, f};
}

static void set_setting(void *ctx, int f, int64_t v, Str s)
{
	NbOpts *o = ctx;
	int num = f + 1;
	if (no_kind[num] == K_REPSTR)
		vec_push(num == NO_INCLUDE ? &o->include : &o->exclude, (void *)arg_text(s));
	else if (no_kind[num] == K_STRING)
		opts_set_s(o, num, arg_text(s));
	else
		opts_set(o, num, v);
}

/* for s in options.settings: if ':' not in s and '=' in s: s = s.replace('=', ':');
 * text_format.Merge(s, toplevel_options) */
static COLD void merge_settings(void)
{
	if (!cmd.settings.n)
		return;
	PypbMessage m = nb_text_message();
	for (int i = 0; i < cmd.settings.n; i++) {
		const char *s = cmd.settings.v[i];
		if (!strchr(s, ':') && strchr(s, '='))
			s = str_replace_all(s, "=", ":");
		PypbError err;
		if (!pypb_text_merge(&m, str_c(s), set_setting, toplevel_options, &err))
			nb_raise_chained(err.chained, xcat(F_PARSE_FILE_MERGE, err.frames), err.exc);
	}
}
