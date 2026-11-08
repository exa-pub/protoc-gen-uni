/* Copyright (c) 2020 InfluxData. Licensed under the MIT License (full text in pbjson_build.c). */

/* Port of Builder of src/lib.rs of pbjson-build 0.8.0 (without build() and its file output). The
 * options are the fields of PbjsonBuilder (pbjson_build.h). */

PbjsonBuilder *pbjson_builder_new(void)
{
	PbjsonBuilder *b = xalloc(sizeof *b);
	b->descriptors = xalloc(sizeof *b->descriptors);
	return b;
}

/* Builder::register_file_descriptor */
void pbjson_builder_register_file_descriptor(PbjsonBuilder *b, PbMsg *file)
{
	register_file_descriptor(b->descriptors, file);
}

static bool any_prefix_match(const TypePath *t, const Vec *prefixes)
{
	for (int i = 0; i < prefixes->n; i++)
		if (prefix_match(t, prefixes->v[i]) >= 0)
			return true;
	return false;
}

/* Builder::generate: the code of all registered types that a prefix matches and no exclude
 * prefix does, one output per package in type path order. */
Vec pbjson_builder_generate(PbjsonBuilder *b, const Vec *prefixes)
{
	DescriptorSet *set = b->descriptors;
	descriptor_set_sort(set);
	MessageOptions mo = {b->ignore_unknown_fields, b->emit_fields, b->preserve_proto_field_names, &b->btree_map_paths};
	Vec ret = {0}; /* PbjsonPackageOutput * */
	const TypePath *package = NULL;
	for (int i = 0; i < set->v.n; i++) {
		const Descriptor *d = set->v.v[i];
		bool exclude = any_prefix_match(d->path, &b->exclude);
		bool include = any_prefix_match(d->path, prefixes);
		if (!include || exclude)
			continue;
		/* Exploit the fact descriptors is ordered to group together types from the same package */
		if (!package || !package_eq(package, d->path)) {
			package = d->path;
			PbjsonPackageOutput *out = xalloc(sizeof *out);
			out->package = package_display(package);
			vec_push(&ret, out);
		}
		PbjsonPackageOutput *out = ret.v[ret.n - 1];
		Resolver r = {&b->extern_paths, d->path, b->retain_enum_prefix, &set->shared_parts};
		if (d->kind == DESC_ENUM) {
			generate_enum(&r, d->path, d->proto, &out->code, b->use_integers_for_enums);
		} else {
			Message *m = resolve_message(set, d);
			if (m)
				generate_message(&r, m, &out->code, &mo);
		}
	}
	return ret;
}
