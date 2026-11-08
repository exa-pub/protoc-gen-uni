// Protocol Buffers - Google's data interchange format
// Copyright 2023 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* Port of src/google/protobuf/feature_resolver.cc: FeatureResolver::CompileDefaults (from the
 * FeatureSet descriptor of the generated pool and the generator's feature extensions),
 * FeatureResolver::Create, MergeFeatures, ValidateFeatureLifetimes and
 * internal::GetEditionFeatureSetDefaults, with their validation and errors over the resolution of
 * pkg/proto (proto_edition_features, proto_merge_features). The descriptor validation of
 * CompileDefaults (ValidateDescriptor, ValidateExtension) is not ported: its inputs are compiled
 * in. */

static const char *feature_names[10] = {
	NULL, "field_presence", "enum_type", "repeated_field_encoding", "utf8_validation", "message_encoding",
	"json_format", "enforce_naming_style", "default_symbol_visibility", "enforce_proto_limits",
};
static const char *feature_unknown_names[10] = {
	NULL, "FIELD_PRESENCE_UNKNOWN", "ENUM_TYPE_UNKNOWN", "REPEATED_FIELD_ENCODING_UNKNOWN", "UTF8_VALIDATION_UNKNOWN",
	"MESSAGE_ENCODING_UNKNOWN", "JSON_FORMAT_UNKNOWN", "ENFORCE_NAMING_STYLE_UNKNOWN",
	"DEFAULT_SYMBOL_VISIBILITY_UNKNOWN", "PROTO_LIMITS_UNKNOWN",
};

/* ValidateMergedFeatures: unknown values of the closed enums are unknown fields, so a field only
 * fails when it is unset or explicitly 0 (*_UNKNOWN). It checks the global features of the
 * version's FeatureSet (33.4 has no enforce_proto_limits). */
static const char *validate_merged_features(const PbMsg *features)
{
	for (uint32_t n = 1; n <= 9; n++)
		if (proto_msg_field_index(features->def, n) >= 0 && pb_int(features, n, 0) == 0)
			return xcat("Feature field `", feature_names[n], "` must resolve to a known value, found ",
				    feature_unknown_names[n]);
	return NULL;
}

/* internal::GetEditionFeatureSetDefaults, with upstream's error */
static PbMsg *edition_feature_set_defaults(int edition, const PbMsg *defaults, const PbMsgDef *feature_set_def,
					   const char **error)
{
	PbMsg *features = proto_edition_features(defaults, edition, feature_set_def);
	if (!features)
		*error = xcat("No valid default found for edition ", lp_edition_name(edition));
	return features;
}

/* FeatureResolver::Create */
static const char *lp_feature_resolver_create(LpFeatureResolver *r, int edition, const PbMsg *defaults,
					      const PbMsgDef *feature_set_def)
{
	r->defaults = NULL;
	int min = (int)pb_int(defaults, PROTO_FSD_MINIMUM_EDITION, 0), max = (int)pb_int(defaults, PROTO_FSD_MAXIMUM_EDITION, 0);
	if (edition < min)
		return xcat("Edition ", lp_edition_name(edition), " is earlier than the minimum supported edition ",
			    lp_edition_name(min));
	if (max < edition && edition != LP_EDITION_UNSTABLE)
		return xcat("Edition ", lp_edition_name(edition), " is later than the maximum supported edition ",
			    lp_edition_name(max));
	int prev = LP_EDITION_UNKNOWN;
	for (int i = 0; i < pb_count(defaults, PROTO_FSD_DEFAULTS); i++) {
		const PbMsg *d = pb_msg_at(defaults, PROTO_FSD_DEFAULTS, i);
		int e = (int)pb_int(d, PROTO_FSED_EDITION, 0);
		if (e == LP_EDITION_UNKNOWN)
			return xcat("Invalid edition ", lp_edition_name(e), " specified.");
		if (prev != LP_EDITION_UNKNOWN && e <= prev)
			return xcat("Feature set defaults are not strictly increasing.  Edition ", lp_edition_name(prev),
				    " is greater than or equal to edition ", lp_edition_name(e), ".");
		const char *err = validate_merged_features(proto_edition_default_features(d, feature_set_def));
		if (err)
			return err;
		prev = e;
	}
	const char *err = NULL;
	r->defaults = edition_feature_set_defaults(edition, defaults, feature_set_def, &err);
	return err;
}

/* FeatureResolver::MergeFeatures */
static const char *lp_merge_features(const LpFeatureResolver *r, const PbMsg *parent, const PbMsg *child, PbMsg **out)
{
	PbMsg *merged = proto_merge_features(r->defaults, parent, child);
	const char *err = validate_merged_features(merged);
	if (err)
		return err;
	*out = merged;
	return NULL;
}

/* ---- CompileDefaults ---- */

static const PbMsg *field_feature_support(const LpField *f) { return pb_msg(f->options, FOPT_FEATURE_SUPPORT); }

static void insert_edition(int *eds, int *n, int edition, int maximum_edition)
{
	if (!(edition <= maximum_edition || edition == LP_EDITION_UNSTABLE))
		return;
	for (int i = 0; i < *n; i++)
		if (eds[i] == edition)
			return;
	int i = *n;
	while (i > 0 && eds[i - 1] > edition) {
		eds[i] = eds[i - 1];
		i--;
	}
	eds[i] = edition;
	(*n)++;
}

/* CollectEditions */
static void collect_editions(const LpMessage *m, int maximum_edition, int *eds, int *n)
{
	for (int i = 0; i < m->n_fields; i++) {
		const LpField *f = &m->fields[i];
		const PbMsg *fs = field_feature_support(f);
		insert_edition(eds, n, (int)pb_int(fs, FS_EDITION_INTRODUCED, 0), maximum_edition);
		if (pb_has(fs, FS_EDITION_REMOVED))
			insert_edition(eds, n, (int)pb_int(fs, FS_EDITION_REMOVED, 0), maximum_edition);
		for (int j = 0; j < pb_count(f->options, FO_EDITION_DEFAULTS); j++)
			insert_edition(eds, n, (int)pb_int(pb_msg_at(f->options, FO_EDITION_DEFAULTS, j), ED_EDITION, 0),
				       maximum_edition);
	}
}

/* TextFormat::ParseFieldValueFromString for the bool and enum feature fields. */
static bool parse_feature_value(const LpField *f, const char *text, uint64_t *out)
{
	if (f->type == PT_BOOL) {
		if (str_eq(text, "true") || str_eq(text, "t") || str_eq(text, "1")) {
			*out = 1;
			return true;
		}
		if (str_eq(text, "false") || str_eq(text, "f") || str_eq(text, "0")) {
			*out = 0;
			return true;
		}
		return false;
	}
	if (f->type == PT_ENUM && f->enum_type) {
		for (int i = 0; i < f->enum_type->n_values; i++)
			if (str_eq(f->enum_type->values[i].name, text)) {
				*out = (uint64_t)(int64_t)f->enum_type->values[i].number;
				return true;
			}
	}
	return false;
}

/* FillDefaults(edition, fixed, overridable) */
static const char *fill_defaults(int edition, const LpMessage *desc, PbMsg *fixed, PbMsg *overridable)
{
	for (int i = 0; i < desc->n_fields; i++) {
		const LpField *field = &desc->fields[i];
		PbMsg *msg = overridable;
		const PbMsg *fs = field_feature_support(field);
		if (fs && ((pb_has(fs, FS_EDITION_INTRODUCED) && edition < pb_int(fs, FS_EDITION_INTRODUCED, 0)) ||
			   (pb_has(fs, FS_EDITION_REMOVED) && edition >= pb_int(fs, FS_EDITION_REMOVED, 0))))
			msg = fixed;
		proto_msg_clear(msg, (uint32_t)field->number);
		/* the default of the latest edition <= edition (stable order for equal editions) */
		const PbMsg *best = NULL;
		int64_t best_edition = 0;
		for (int j = 0; j < pb_count(field->options, FO_EDITION_DEFAULTS); j++) {
			const PbMsg *d = pb_msg_at(field->options, FO_EDITION_DEFAULTS, j);
			int64_t e = pb_int(d, ED_EDITION, 0);
			if (e <= edition && (!best || e >= best_edition))
				best = d, best_edition = e;
		}
		if (!best)
			return xcat("No valid default found for edition ", lp_edition_name(edition), " in feature field ",
				    field->full_name);
		uint64_t v;
		const char *text = pb_str(best, ED_VALUE);
		if (!parse_feature_value(field, text, &v))
			return xcat("Parsing error in edition_defaults for feature field ", field->full_name,
				    ". Could not parse: ", text);
		proto_msg_set_u(msg, (uint32_t)field->number, v);
	}
	return NULL;
}

/* FeatureResolver::CompileDefaults(FeatureSet::descriptor(), extensions, min, max), as a
 * FeatureSetDefaults message of the builtin pool. extensions: full names of FeatureSet
 * extensions of the builtin pool. */
static PbMsg *lp_compile_defaults(LpPool *builtin, const char *const *extensions, int min_edition, int max_edition,
				  const char **error)
{
	*error = NULL;
	if (min_edition > max_edition) {
		*error = xcat("Invalid edition range, edition ", lp_edition_name(min_edition), " is newer than edition ",
			      lp_edition_name(max_edition));
		return NULL;
	}
	const LpMessage *feature_set = lp_find_message(builtin, "google.protobuf.FeatureSet");
	int n_ext = 0;
	while (extensions && extensions[n_ext])
		n_ext++;
	const LpField **exts = xalloc(sizeof(LpField *) * (size_t)(n_ext + 1));
	for (int i = 0; i < n_ext; i++) {
		LpSymbol *s = lp_find_symbol(builtin, extensions[i]);
		if (!s || s->kind != PROTO_FIELD) {
			*error = xcat("Unknown extension of ", feature_set->full_name, ".");
			return NULL;
		}
		exts[i] = s->desc;
	}
	int eds[64], n = 0;
	collect_editions(feature_set, max_edition, eds, &n);
	for (int i = 0; i < n_ext && n < 48; i++)
		collect_editions(exts[i]->message_type, max_edition, eds, &n);
	if (eds[0] != LP_EDITION_LEGACY) {
		*error = xcat("Minimum edition ", lp_edition_name(eds[0]), " is not EDITION_LEGACY");
		return NULL;
	}
	if (eds[0] > min_edition) {
		*error = xcat("Minimum edition ", lp_edition_name(min_edition), " is earlier than the oldest valid edition ",
			      lp_edition_name(eds[0]));
		return NULL;
	}
	const LpMsgDef *fsd_def = lp_msgdef_by_name(builtin, "google.protobuf.FeatureSetDefaults");
	const LpMsgDef *fs_def = lp_msgdef(builtin, feature_set);
	PbMsg *defaults = pb_new(&fsd_def->pb);
	proto_msg_set_u(defaults, PROTO_FSD_MINIMUM_EDITION, (uint64_t)(int64_t)min_edition);
	proto_msg_set_u(defaults, PROTO_FSD_MAXIMUM_EDITION, (uint64_t)(int64_t)max_edition);
	for (int k = 0; k < n; k++) {
		PbMsg *fixed = pb_new(&fs_def->pb), *overridable = pb_new(&fs_def->pb);
		*error = fill_defaults(eds[k], feature_set, fixed, overridable);
		for (int i = 0; i < n_ext && !*error; i++)
			*error = fill_defaults(eds[k], exts[i]->message_type, proto_msg_mutable(fixed, (uint32_t)exts[i]->number),
					       proto_msg_mutable(overridable, (uint32_t)exts[i]->number));
		if (*error)
			return NULL;
		PbMsg *ed = proto_msg_add(defaults, PROTO_FSD_DEFAULTS);
		proto_msg_set_u(ed, PROTO_FSED_EDITION, (uint64_t)(int64_t)eds[k]);
		proto_msg_set_msg(ed, PROTO_FSED_FIXED, fixed);
		proto_msg_set_msg(ed, PROTO_FSED_OVERRIDABLE, overridable);
	}
	return defaults;
}

/* ---- ValidateFeatureLifetimes ---- */

/* ValidateSingleFeatureLifetimes (errors only; warnings are only logged upstream); legacy: the
 * texts before 34 (LpVersion.legacy_feature_lifetimes). */
static void validate_single_feature_lifetimes(int edition, const char *full_name, const PbMsg *fs, bool legacy,
					      Vec *errors)
{
	if (!fs)
		return;
	const char *prefix = legacy ? "Feature " : "";
	if (edition < pb_int(fs, FS_EDITION_INTRODUCED, 0))
		vec_push(errors, xcat(prefix, full_name, " wasn't introduced until edition ",
				      lp_edition_name((int)pb_int(fs, FS_EDITION_INTRODUCED, 0)), " and can't be used in edition ",
				      lp_edition_name(edition)));
	if (pb_has(fs, FS_EDITION_REMOVED) && edition >= pb_int(fs, FS_EDITION_REMOVED, 0))
		vec_push(errors, legacy ? xcat(prefix, full_name, " has been removed in edition ",
					       lp_edition_name((int)pb_int(fs, FS_EDITION_REMOVED, 0)),
					       " and can't be used in edition ", lp_edition_name(edition))
					: xcat(full_name, " has been removed in edition ",
					       lp_edition_name((int)pb_int(fs, FS_EDITION_REMOVED, 0)),
					       pb_has(fs, FS_REMOVAL_ERROR) ? xcat(": ", pb_str(fs, FS_REMOVAL_ERROR)) : ""));
}

/* ValidateFeatureLifetimesImpl; legacy: before 34, only singular extension messages are recursed
 * into (and not validated themselves), and an enum value must be known. */
static void validate_feature_lifetimes_impl(int edition, const PbMsg *message, bool legacy, Vec *errors)
{
	const LpMsgDef *d = (const LpMsgDef *)message->def;
	for (int i = 0; i < d->pb.n; i++) {
		const PbSlot *s = &message->slots[i];
		if (!s->n)
			continue;
		const LpField *field = d->fields[i];
		int type = d->pb.f[i].type;
		if (legacy) {
			if (field->is_extension && (type == PT_MESSAGE || type == PT_GROUP)) {
				validate_feature_lifetimes_impl(edition, s->v[s->n - 1].m, legacy, errors);
				continue;
			}
		} else if (type == PT_MESSAGE || type == PT_GROUP) {
			for (int j = 0; j < s->n; j++)
				validate_feature_lifetimes_impl(edition, s->v[j].m, legacy, errors);
		}
		if (field->enum_type) {
			bool unknown = false;
			for (int j = 0; j < s->n; j++) {
				const LpEnumValue *value = proto_enum_value_by_number(field->enum_type, (int64_t)s->v[j].u);
				if (!value) {
					unknown = true;
					if (legacy)
						vec_push(errors, xfmt("Feature %s has no known value %lld", field->full_name,
								      (long long)(int32_t)s->v[j].u));
					continue;
				}
				validate_single_feature_lifetimes(edition, value->full_name,
								  pb_msg(value->options, EVOPT_FEATURE_SUPPORT), legacy, errors);
			}
			if (unknown && legacy)
				continue;
		}
		validate_single_feature_lifetimes(edition, field->full_name, field_feature_support(field), legacy, errors);
	}
}

/* FeatureResolver::ValidateFeatureLifetimes(edition, option, pool descriptor): the option is
 * reparsed with the pool's definition of its type when the pool has one (custom options and
 * feature extensions become known), else validated with the generated one. */
static void lp_validate_feature_lifetimes(int edition, const PbMsg *option, LpPool *pool, Vec *errors)
{
	const LpMsgDef *od = (const LpMsgDef *)option->def;
	const LpMessage *pool_desc = lp_find_message(pool, od->desc->full_name);
	const PbMsg *m = option;
	if (pool_desc && pool->builtin) {
		PbMsg *dyn = pb_new(&lp_msgdef(pool, pool_desc)->pb);
		Str bytes = lp_serialize_str(option);
		lp_decode(dyn, (const uint8_t *)bytes.p, bytes.n); /* a failed parse keeps what was parsed */
		m = dyn;
	}
	validate_feature_lifetimes_impl(edition, m, pool->version && pool->version->legacy_feature_lifetimes, errors);
}
