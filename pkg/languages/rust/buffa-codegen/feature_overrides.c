/* Copyright Anthropic, PBC. Licensed under the Apache License, Version 2.0.
 *
 * Port of buffa-codegen 0.9.2 src/feature_overrides.rs: `enum_type:OPEN` overrides (plugin options
 * open_enums_in / override_feature_in) applied to the descriptors before generation, so feature
 * resolution, every generation path and the embedded reflection descriptor set see them. The only
 * override kind is EnumType::Open; the unmatched-rule warnings go to stderr upstream. */

static void set_enum_type_open(PbMsg *owner, uint32_t options_field, uint32_t features_field)
{
	PbMsg *opts = proto_msg_mutable(owner, options_field);
	PbMsg *fs = proto_msg_mutable(opts, features_field);
	proto_msg_set_u(fs, PROTO_FEATURE_ENUM_TYPE, PROTO_ENUM_TYPE_OPEN);
}

static bool rule_matches_one(const char *rule_in, const char *fqn_dotted)
{
	Str r = rust_trim(str_c(rule_in));
	char *rule = xstrndup(r.p, r.n);
	if (str_eq(rule, "."))
		return true;
	size_t n = strlen(rule);
	while (n && rule[n - 1] == '.')
		rule[--n] = 0;
	if (!n)
		return false;
	if (rule[0] == '.')
		return matches_proto_prefix(rule, fqn_dotted);
	if (fqn_dotted[0] == '.')
		return matches_proto_prefix(rule, fqn_dotted + 1);
	return matches_proto_prefix(rule, fqn_dotted);
}

typedef struct {
	const Vec *rules; /* char * paths */
	Map local_enums;
} Overrides;

static bool override_matches(const Overrides *o, const char *fqn)
{
	bool any = false;
	for (int i = 0; i < o->rules->n; i++)
		any |= rule_matches_one(o->rules->v[i], fqn);
	return any;
}

static void collect_local_enums(Map *set, const char *prefix, const PbMsg *parent, int enum_field, int nested_field, int depth)
{
	if (depth > 100)
		fail("message nesting too deep");
	int n = pb_count(parent, enum_field);
	for (int i = 0; i < n; i++) {
		const PbMsg *e = pb_msg_at(parent, enum_field, i);
		if (pb_has(e, EDP_NAME))
			map_put(set, xcat(prefix, dname(e)), (void *)1);
	}
	n = pb_count(parent, nested_field);
	for (int i = 0; i < n; i++) {
		const PbMsg *m = pb_msg_at(parent, nested_field, i);
		if (pb_has(m, DP_NAME))
			collect_local_enums(set, xcat(prefix, dname(m), "."), m, DP_ENUM_TYPE, DP_NESTED_TYPE, depth + 1);
	}
}

static void open_enum_if_matched(const Overrides *o, PbMsg *e, const char *prefix)
{
	if (pb_has(e, EDP_NAME) && override_matches(o, xcat(prefix, dname(e))))
		set_enum_type_open(e, EDP_OPTIONS, ENUMOPT_FEATURES);
}

static void apply_to_message(const Overrides *o, PbMsg *msg, const char *prefix, int depth)
{
	if (!pb_has(msg, DP_NAME))
		return;
	char *msg_prefix = xcat(prefix, dname(msg), ".");
	Vec entries = {0}; /* map entry names whose map field matched */
	for (int i = 0; i < n_fields(msg); i++) {
		const PbMsg *f = field_at(msg, i);
		const PbMsg *entry;
		if (!pb_has(f, FD_NAME) || !(entry = find_map_entry(msg, f)))
			continue;
		if (override_matches(o, xcat(msg_prefix, dname(f))) && pb_has(entry, DP_NAME))
			vec_push(&entries, (void *)dname(entry));
	}
	for (int i = 0; i < n_fields(msg); i++) {
		PbMsg *f = (PbMsg *)field_at(msg, i);
		if (fd_type(f) != PT_ENUM || !pb_has(f, FD_NAME) || !fd_type_name(f))
			continue;
		const char *tn = fd_type_name(f);
		bool field_match = override_matches(o, xcat(msg_prefix, dname(f)));
		bool local = map_has(&o->local_enums, tn);
		bool type_match = override_matches(o, tn);
		if ((field_match || (!local && type_match)) && !(local && type_match))
			set_enum_type_open(f, FD_OPTIONS, FLDOPT_FEATURES);
	}
	int n = pb_count(msg, DP_ENUM_TYPE);
	for (int i = 0; i < n; i++)
		open_enum_if_matched(o, pb_msg_at(msg, DP_ENUM_TYPE, i), msg_prefix);
	n = pb_count(msg, DP_NESTED_TYPE);
	for (int i = 0; i < n; i++) {
		PbMsg *nested = pb_msg_at(msg, DP_NESTED_TYPE, i);
		bool entry_matched = false;
		for (int k = 0; k < entries.n && pb_has(nested, DP_NAME); k++)
			entry_matched |= str_eq(entries.v[k], dname(nested));
		if (entry_matched)
			for (int k = 0; k < n_fields(nested); k++) {
				PbMsg *f = (PbMsg *)field_at(nested, k);
				if (pb_has(f, FD_NUMBER) && fd_number(f) == 2 && fd_type(f) == PT_ENUM) {
					const char *tn = fd_type_name(f);
					if (!(tn && map_has(&o->local_enums, tn) && override_matches(o, tn)))
						set_enum_type_open(f, FD_OPTIONS, FLDOPT_FEATURES);
				}
			}
		if (depth > 100)
			fail("message nesting too deep");
		apply_to_message(o, nested, msg_prefix, depth + 1);
	}
}

/* apply_feature_overrides over the request's files (mutated in place: the request is ours) */
static void apply_feature_overrides(const PbMsg *req, const BcConfig *c)
{
	if (!c->feature_overrides.n)
		return;
	Vec rules = {0};
	for (int i = 0; i < c->feature_overrides.n; i++)
		vec_push(&rules, (void *)((BcPair *)c->feature_overrides.v[i])->key);
	Overrides o = {&rules, {0}};
	int nf = pb_count(req, CGR_PROTO_FILE);
	for (int i = 0; i < nf; i++) {
		const PbMsg *file = pb_msg_at(req, CGR_PROTO_FILE, i);
		const char *pkg = pb_str(file, FDP_PACKAGE);
		collect_local_enums(&o.local_enums, *pkg ? xcat(".", pkg, ".") : ".", file, FDP_ENUM_TYPE, FDP_MESSAGE_TYPE, 0);
	}
	for (int i = 0; i < nf; i++) {
		PbMsg *file = pb_msg_at(req, CGR_PROTO_FILE, i);
		const char *pkg = pb_str(file, FDP_PACKAGE);
		char *prefix = *pkg ? xcat(".", pkg, ".") : ".";
		int n = pb_count(file, FDP_ENUM_TYPE);
		for (int k = 0; k < n; k++)
			open_enum_if_matched(&o, pb_msg_at(file, FDP_ENUM_TYPE, k), prefix);
		n = pb_count(file, FDP_MESSAGE_TYPE);
		for (int k = 0; k < n; k++)
			apply_to_message(&o, pb_msg_at(file, FDP_MESSAGE_TYPE, k), prefix, 0);
	}
}
