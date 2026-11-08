/* Port of src/generator.rs of protoc-gen-tonic 0.5.0 (https://github.com/neoeinstein/protoc-gen-prost,
 * Apache-2.0): TonicGenerator, and ProstService / ProstMethod as tonic_build Service / Method. */

typedef struct {
	Resolver resolver;
	bool generate_server, generate_client, generate_transport;
	TonicAttributes server_attributes, client_attributes;
	bool emit_package, insert_include;
} TonicGenerator;

/* prost_build::Method: the fields that request_response_name reads. */
typedef struct {
	const char *input_type, *output_type;             /* Rust */
	const char *input_proto_type, *output_proto_type; /* .pkg.Msg */
} ProstMethod;

/* Non-path Rust types allowed for request/response types. */
static bool non_path_type(const char *rust_type) { return str_eq(rust_type, "()"); }

static PpNode *convert_type(const char *proto_type, const char *rust_type, const char *proto_path,
			    bool compile_well_known_types)
{
	bool is_google_type = has_prefix(proto_type, ".google.protobuf");
	if ((is_google_type && !compile_well_known_types) || has_prefix(rust_type, "::") || non_path_type(rust_type))
		return syn_parse_type(str_c(rust_type), false);
	if (has_prefix(rust_type, "crate::"))
		return syn_parse_type(str_c(rust_type), true);
	return syn_parse_type(str_c(xcat(proto_path, "::", rust_type)), true);
}

/* request_response_name: the implementation copied from tonic-build's prost.rs. */
static void request_response_name(const TonicMethod *m, const char *proto_path, bool compile_well_known_types,
				  PpNode **request, PpNode **response)
{
	const ProstMethod *pm = m->ctx;
	*request = convert_type(pm->input_proto_type, pm->input_type, proto_path, compile_well_known_types);
	*response = convert_type(pm->output_proto_type, pm->output_type, proto_path, compile_well_known_types);
}

/* prepare_service */
static TonicService *prepare_service(const TonicGenerator *g, const ProstModule *module, const PbMsg *file,
				     const Locations *locs, const PbMsg *descriptor, int service_index)
{
	TonicService *s = xalloc(sizeof *s);
	s->comment = get_service_comments(locs, service_index, &s->n_comment);
	s->n_methods = pb_count(descriptor, SDP_METHOD);
	s->methods = xalloc(sizeof *s->methods * (size_t)(s->n_methods ? s->n_methods : 1));
	for (int i = 0; i < s->n_methods; i++) {
		const PbMsg *m = pb_msg_at(descriptor, SDP_METHOD, i);
		TonicMethod *tm = &s->methods[i];
		ProstMethod *pm = xalloc(sizeof *pm);
		tm->comment = get_method_comments(locs, service_index, i, &tm->n_comment);
		tm->name = to_snake(pb_str(m, MDP_NAME));
		tm->identifier = pb_str(m, MDP_NAME);
		pm->input_type = resolve_ident(&g->resolver, module, pb_str(m, MDP_INPUT_TYPE));
		pm->output_type = resolve_ident(&g->resolver, module, pb_str(m, MDP_OUTPUT_TYPE));
		pm->input_proto_type = pb_str(m, MDP_INPUT_TYPE);
		pm->output_proto_type = pb_str(m, MDP_OUTPUT_TYPE);
		tm->client_streaming = pb_int(m, MDP_CLIENT_STREAMING, 0) != 0;
		tm->server_streaming = pb_int(m, MDP_SERVER_STREAMING, 0) != 0;
		tm->codec_path = "tonic_prost::ProstCodec";
		tm->request_response_name = request_response_name;
		tm->ctx = pm;
	}
	s->package = pb_str(file, FDP_PACKAGE);
	s->name = to_upper_camel(pb_str(descriptor, SDP_NAME));
	s->identifier = pb_str(descriptor, SDP_NAME);
	return s;
}

static TonicCodeGenBuilder builder(const TonicGenerator *g, const TonicAttributes *attributes)
{
	TonicCodeGenBuilder b = tonic_code_gen_builder();
	b.emit_package = g->emit_package;
	b.build_transport = g->generate_transport;
	b.compile_well_known_types = g->resolver.compile_well_known_types;
	b.attributes = *attributes;
	return b;
}

/* handle_module_request: appends the module's files (ProstFile *) to *files. */
static void handle_module_request(const TonicGenerator *g, const ProstModuleRequest *request, Vec *files)
{
	static const char PROTO_PATH[] = "super";
	char *output_filename = xcat(request->proto_package_name, ".tonic.rs");
	TonicCodeGenBuilder client = builder(g, &g->client_attributes), server = builder(g, &g->server_attributes);
	Vec services = {0}; /* items */
	for (int f = 0; f < request->files.n; f++) {
		const PbMsg *file = request->files.v[f];
		int n = pb_count(file, FDP_SERVICE);
		if (!n)
			continue;
		Locations locs = file_locations(file);
		for (int i = 0; i < n; i++) {
			TonicService *s = prepare_service(g, request->module, file, &locs, pb_msg_at(file, FDP_SERVICE, i), i);
			if (g->generate_client)
				tonic_generate_client(&client, s, PROTO_PATH, &services);
			if (g->generate_server)
				tonic_generate_server(&server, s, PROTO_PATH, &services);
		}
	}
	if (!services.n)
		return;
	PpNode *file = pp_file();
	for (int i = 0; i < services.n; i++)
		pp_push(file, services.v[i]);
	if (g->insert_include) {
		ProstFile *include = prost_append_to_file(request, str_c(xcat("include!(\"", output_filename, "\");\n")));
		if (!include)
			return;
		vec_push(files, include);
	}
	ProstFile *out = xalloc(sizeof *out);
	out->name = xcat(prost_module_request_output_dir(request), output_filename);
	out->content = str_c(xcat("// @generated\n", pp_unparse(file)));
	vec_push(files, out);
}

/* Generator::generate */
static void tonic_generator_generate(const TonicGenerator *g, const ProstModuleRequestSet *set, Vec *files)
{
	for (int i = 0; i < set->requests.n; i++)
		handle_module_request(g, set->requests.v[i], files);
}
