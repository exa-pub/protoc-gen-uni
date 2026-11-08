/* Port of src/lib.rs of protoc-gen-tonic 0.5.0 (https://github.com/neoeinstein/protoc-gen-prost,
 * Apache-2.0): execute and Parameters. */

typedef struct {
	const char *default_package_filename; /* NULL: "_" */
	Vec extern_path;                      /* pairs */
	TonicAttributes server_attributes, client_attributes;
	bool compile_well_known_types, disable_package_emission, no_server, no_client, no_transport, no_include;
	bool flat_output_dir;
} Parameters;

/* attribute.replace(r"\,", ",").replace(r"\\", r"\") */
static const char *unescape_attribute(const char *attribute)
{
	return str_replace_all(str_replace_all(attribute, "\\,", ","), "\\\\", "\\");
}

/* Parameters::from_str: NULL or the error. */
static const char *parse_parameters(Parameters *o, Str s)
{
	ProstParam *params;
	int n = prost_params_parse(s, &params);
	for (int i = 0; i < n; i++) {
		const ProstParam *p = &params[i];
		bool kv = p->kind == PROST_KEY_VALUE;
		if (str_eq(p->param, "default_package_filename") && !kv)
			o->default_package_filename = prost_param_value(p);
		else if (kv && str_eq(p->param, "extern_path")) {
			vec_push(&o->extern_path, (void *)p->key);
			vec_push(&o->extern_path, (void *)p->value);
		} else if (prost_param_flag(p, "compile_well_known_types", &o->compile_well_known_types) ||
			   prost_param_flag(p, "disable_package_emission", &o->disable_package_emission) ||
			   prost_param_flag(p, "no_server", &o->no_server) || prost_param_flag(p, "no_client", &o->no_client) ||
			   prost_param_flag(p, "no_transport", &o->no_transport) ||
			   prost_param_flag(p, "no_include", &o->no_include))
			;
		else if (kv && str_eq(p->param, "client_mod_attribute"))
			tonic_attributes_push_mod(&o->client_attributes, p->key, unescape_attribute(p->value));
		else if (kv && str_eq(p->param, "client_attribute"))
			tonic_attributes_push_struct(&o->client_attributes, p->key, unescape_attribute(p->value));
		else if (kv && str_eq(p->param, "server_mod_attribute"))
			tonic_attributes_push_mod(&o->server_attributes, p->key, unescape_attribute(p->value));
		else if (kv && str_eq(p->param, "server_attribute"))
			tonic_attributes_push_struct(&o->server_attributes, p->key, unescape_attribute(p->value));
		else if (prost_param_flag(p, "flat_output_dir", &o->flat_output_dir))
			;
		else
			return prost_invalid_parameter(p);
	}
	return NULL;
}

typedef struct {
	Parameters params;
	PbMsg *req;
	Str raw;
	Vec files; /* ProstFile * */
	const char *error;
} Run;

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
	ProstModuleRequestSet *set =
		prost_module_request_set_new(run->req, run->raw, o->default_package_filename, o->flat_output_dir);
	TonicGenerator g = {
		.generate_server = !o->no_server,
		.generate_client = !o->no_client,
		.generate_transport = !o->no_transport,
		.server_attributes = o->server_attributes,
		.client_attributes = o->client_attributes,
		.emit_package = !o->disable_package_emission,
		.insert_include = !o->no_include,
	};
	resolver_init(&g.resolver, &o->extern_path, o->compile_well_known_types);
	tonic_generator_generate(&g, set, &run->files);
}
