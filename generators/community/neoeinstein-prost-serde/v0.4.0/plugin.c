/* Ports of protoc-gen-prost-serde 0.4.0 (https://github.com/neoeinstein/protoc-gen-prost,
 * Apache-2.0): execute and Parameters (src/lib.rs) and PbJsonGenerator (src/generator.rs). */

typedef struct {
	const char *default_package_filename; /* NULL: "_" */
	Vec extern_path;                      /* pairs */
	bool retain_enum_prefix, preserve_proto_field_names, ignore_unknown_fields, emit_fields;
	bool use_integers_for_enums, no_include, flat_output_dir;
	Vec btree_map, exclude; /* const char * */
} Parameters;

static bool is_value(const ProstParam *p, const char *name) { return p->kind == PROST_VALUE && str_eq(p->param, name); }

/* `name` or `name=true`; unlike prost_param_flag, `name=false` is an invalid parameter. */
static bool true_flag(const ProstParam *p, const char *name, bool *on)
{
	if (!str_eq(p->param, name) ||
	    !(p->kind == PROST_PARAMETER || (p->kind == PROST_VALUE && str_eq(p->value, "true"))))
		return false;
	*on = true;
	return true;
}

/* Parameters::from_str: NULL or the error. */
static const char *parse_parameters(Parameters *o, Str s)
{
	ProstParam *params;
	int n = prost_params_parse(s, &params);
	for (int i = 0; i < n; i++) {
		const ProstParam *p = &params[i];
		if (str_eq(p->param, "default_package_filename") && p->kind != PROST_KEY_VALUE)
			o->default_package_filename = prost_param_value(p);
		else if (prost_param_flag(p, "retain_enum_prefix", &o->retain_enum_prefix))
			;
		else if (prost_param_flag(p, "preserve_proto_field_names", &o->preserve_proto_field_names))
			;
		else if (true_flag(p, "ignore_unknown_fields", &o->ignore_unknown_fields))
			;
		else if (true_flag(p, "emit_fields", &o->emit_fields))
			;
		else if (true_flag(p, "use_integers_for_enums", &o->use_integers_for_enums))
			;
		else if (prost_param_flag(p, "no_include", &o->no_include))
			;
		else if (p->kind == PROST_KEY_VALUE && str_eq(p->param, "extern_path")) {
			vec_push(&o->extern_path, (void *)p->key);
			vec_push(&o->extern_path, (void *)p->value);
		} else if (is_value(p, "btree_map"))
			vec_push(&o->btree_map, (void *)p->value);
		else if (prost_param_flag(p, "flat_output_dir", &o->flat_output_dir))
			;
		else if (is_value(p, "exclude"))
			vec_push(&o->exclude, (void *)p->value);
		else
			return prost_invalid_parameter(p);
	}
	return NULL;
}

/* Parameters::to_pbjson_builder. Upstream adds the btree_map paths twice, which only matters
 * for the order of panics. */
static void to_pbjson_builder(const Parameters *o, PbjsonBuilder *b)
{
	b->extern_paths = o->extern_path;
	b->retain_enum_prefix = o->retain_enum_prefix;
	b->preserve_proto_field_names = o->preserve_proto_field_names;
	b->ignore_unknown_fields = o->ignore_unknown_fields;
	b->emit_fields = o->emit_fields;
	b->use_integers_for_enums = o->use_integers_for_enums;
	for (int k = 0; k < (o->btree_map.n ? 2 : 0); k++)
		for (int i = 0; i < o->btree_map.n; i++)
			vec_push(&b->btree_map_paths, o->btree_map.v[i]);
	b->exclude = o->exclude;
}

typedef struct {
	Parameters params;
	PbMsg *req;
	Str raw;
	Vec files; /* ProstFile * */
	const char *error;
} Run;

/* PbJsonGenerator::generate */
static void pbjson_generate(PbjsonBuilder *b, const ProstModuleRequestSet *set, bool insert_include, Vec *files)
{
	Vec prefixes = {0};
	vec_push(&prefixes, ".");
	Vec results = pbjson_builder_generate(b, &prefixes);
	for (int i = 0; i < results.n; i++) {
		PbjsonPackageOutput *out = results.v[i];
		ProstModule *module = prost_module_from_package(str_replace_all(out->package, "r#", ""));
		const ProstModuleRequest *request = prost_module_request_set_for_module(set, module);
		if (!request)
			continue;
		char *output_filename = xcat(request->proto_package_name, ".serde.rs");
		ProstFile *include = NULL;
		if (insert_include) {
			include = prost_append_to_file(request, str_c(xcat("include!(\"", output_filename, "\");\n")));
			if (!include)
				continue;
			vec_push(files, include);
		}
		Buf content = {0};
		buf_adds(&content, "// @generated\n");
		buf_add_str(&content, buf_view(&out->code));
		ProstFile *f = xalloc(sizeof *f);
		*f = (ProstFile){xcat(prost_module_request_output_dir(request), output_filename), NULL, buf_view(&content)};
		vec_push(files, f);
	}
}

/* execute */
static void execute(void *arg)
{
	Run *run = arg;
	Parameters *o = &run->params;
	/* CodeGeneratorRequest::decode: the errors of prost, which pkg/pb does not have (invalid
	 * UTF-8, known fields with another wire type, its recursion limit). */
	run->error = prost_decode_request(run->raw);
	if (run->error)
		return;
	assert_no_nul(pb_bytes(run->req, CGR_PARAMETER), "the parameter");
	run->error = parse_parameters(o, pb_bytes(run->req, CGR_PARAMETER));
	if (run->error)
		return;
	assert_supported(run->req);
	PbjsonBuilder *b = pbjson_builder_new();
	to_pbjson_builder(o, b);
	for (int i = 0; i < pb_count(run->req, CGR_PROTO_FILE); i++)
		pbjson_builder_register_file_descriptor(b, pb_msg_at(run->req, CGR_PROTO_FILE, i));
	ProstModuleRequestSet *set =
		prost_module_request_set_new(run->req, run->raw, o->default_package_filename, o->flat_output_dir);
	pbjson_generate(b, set, !o->no_include, &run->files);
}
