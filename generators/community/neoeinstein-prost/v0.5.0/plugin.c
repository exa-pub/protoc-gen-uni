/* Ports of protoc-gen-prost 0.5.0 (https://github.com/neoeinstein/protoc-gen-prost, Apache-2.0):
 * execute, Parameters and ProstParameters (src/lib.rs), CoreProstGenerator
 * (src/generator/core.rs); and of Config::generate of prost-build 0.14.1 (src/config.rs). */

typedef struct {
	Vec btree_map, bytes, boxed, disable_comments, skip_debug; /* const char * */
	const char *default_package_filename;                      /* NULL: "_" */
	Vec extern_path, type_attribute, field_attribute, enum_attribute, message_attribute; /* pairs */
	bool compile_well_known_types, retain_enum_prefix, enable_type_names, flat_output_dir;
	bool file_descriptor_set;
} Parameters;

static bool is_value(const ProstParam *p, const char *name) { return p->kind == PROST_VALUE && str_eq(p->param, name); }
static bool is_key_value(const ProstParam *p, const char *name) { return p->kind == PROST_KEY_VALUE && str_eq(p->param, name); }

static void push_pair(Vec *v, const char *a, const char *b)
{
	vec_push(v, (void *)a);
	vec_push(v, (void *)b);
}

/* The attribute value: upstream unescapes \, and \\ a second time. */
static const char *attribute(const ProstParam *p)
{
	return str_replace_all(str_replace_all(p->value, "\\,", ","), "\\\\", "\\");
}

/* ProstParameters::try_handle_parameter */
static bool try_handle_parameter(Parameters *o, const ProstParam *p)
{
	if (is_value(p, "btree_map"))
		vec_push(&o->btree_map, (void *)p->value);
	else if (is_value(p, "bytes"))
		vec_push(&o->bytes, (void *)p->value);
	else if (is_value(p, "boxed"))
		vec_push(&o->boxed, (void *)p->value);
	else if (str_eq(p->param, "default_package_filename") && p->kind != PROST_KEY_VALUE)
		o->default_package_filename = prost_param_value(p);
	else if (prost_param_flag(p, "compile_well_known_types", &o->compile_well_known_types))
		;
	else if (is_value(p, "disable_comments"))
		vec_push(&o->disable_comments, (void *)p->value);
	else if (is_value(p, "skip_debug"))
		vec_push(&o->skip_debug, (void *)p->value);
	else if (prost_param_flag(p, "retain_enum_prefix", &o->retain_enum_prefix))
		;
	else if (is_key_value(p, "extern_path"))
		push_pair(&o->extern_path, p->key, p->value);
	else if (is_key_value(p, "type_attribute"))
		push_pair(&o->type_attribute, p->key, attribute(p));
	else if (is_key_value(p, "field_attribute"))
		push_pair(&o->field_attribute, p->key, attribute(p));
	else if (is_key_value(p, "enum_attribute"))
		push_pair(&o->enum_attribute, p->key, attribute(p));
	else if (is_key_value(p, "message_attribute"))
		push_pair(&o->message_attribute, p->key, attribute(p));
	else if (prost_param_flag(p, "enable_type_names", &o->enable_type_names))
		;
	else if (prost_param_flag(p, "flat_output_dir", &o->flat_output_dir))
		;
	else
		return false;
	return true;
}

/* Parameters::from_str: NULL or the error. */
static const char *parse_parameters(Parameters *o, Str s)
{
	ProstParam *params;
	int n = prost_params_parse(s, &params);
	for (int i = 0; i < n; i++) {
		const ProstParam *p = &params[i];
		if (try_handle_parameter(o, p))
			continue;
		if (!prost_param_flag(p, "file_descriptor_set", &o->file_descriptor_set))
			return prost_invalid_parameter(p);
	}
	return NULL;
}

static void path_map_insert_all(PathMap *pm, const Vec *matchers)
{
	for (int i = 0; i < matchers->n; i++)
		path_map_insert(pm, matchers->v[i], "");
}

static void path_map_insert_pairs(PathMap *pm, const Vec *pairs)
{
	for (int i = 0; i + 1 < pairs->n; i += 2)
		path_map_insert(pm, pairs->v[i], pairs->v[i + 1]);
}

/* ProstParameters::to_prost_config */
static Config to_prost_config(const Parameters *o)
{
	Config c = {0};
	path_map_insert_all(&c.map_type, &o->btree_map);
	path_map_insert_all(&c.bytes_type, &o->bytes);
	path_map_insert_all(&c.boxed, &o->boxed);
	path_map_insert_all(&c.disable_comments, &o->disable_comments);
	path_map_insert_all(&c.skip_debug, &o->skip_debug);
	c.extern_paths = o->extern_path;
	path_map_insert_pairs(&c.type_attributes, &o->type_attribute);
	path_map_insert_pairs(&c.field_attributes, &o->field_attribute);
	path_map_insert_pairs(&c.enum_attributes, &o->enum_attribute);
	path_map_insert_pairs(&c.message_attributes, &o->message_attribute);
	c.prost_types = !o->compile_well_known_types;
	c.strip_enum_prefix = !o->retain_enum_prefix;
	c.enable_type_names = o->enable_type_names;
	return c;
}

/* Config::generate over all files of the request: the code of every module that has code, with
 * the prost-build header (contents[i] belongs to set->requests[i]; NULL without code). Returns
 * NULL or the error. */
static const char *config_generate(const Config *config, const ProstModuleRequestSet *set, Buf **contents)
{
	Vec files = {0};
	for (int i = 0; i < set->requests.n; i++) {
		const ProstModuleRequest *r = set->requests.v[i];
		for (int j = 0; j < r->files.n; j++)
			vec_push(&files, r->files.v[j]);
	}
	Context *ctx = xalloc(sizeof *ctx);
	ctx->config = config;
	message_graph_init(&ctx->graph, &files);
	const char *err = extern_paths_new(&ctx->extern_paths, &config->extern_paths, config->prost_types);
	if (err)
		return err;
	for (int i = 0; i < set->requests.n; i++) {
		const ProstModuleRequest *r = set->requests.v[i];
		Buf *buf = xalloc(sizeof *buf);
		for (int j = 0; j < r->files.n; j++)
			code_generator_generate(ctx, r->files.v[j], buf);
		contents[i] = NULL;
		if (buf->n) {
			Buf *with = xalloc(sizeof *with);
			buf_adds(with, "// This file is @generated by prost-build.\n");
			buf_add_str(with, buf_view(buf));
			contents[i] = with;
		}
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

/* execute: CoreProstGenerator, then FileDescriptorSetGenerator if requested. */
static void execute(void *arg)
{
	Run *run = arg;
	Parameters *o = &run->params;
	check_message(run->req); /* CodeGeneratorRequest::decode */
	run->error = parse_parameters(o, pb_bytes(run->req, CGR_PARAMETER));
	if (run->error)
		return;
	check_packages(run->req);
	ProstModuleRequestSet *set =
		prost_module_request_set_new(run->req, run->raw, o->default_package_filename, o->flat_output_dir);
	Config config = to_prost_config(o);
	Buf **contents = xalloc(sizeof *contents * (size_t)(set->requests.n ? set->requests.n : 1));
	run->error = config_generate(&config, set, contents);
	if (run->error)
		return;
	for (int i = 0; i < set->requests.n; i++) {
		const ProstModuleRequest *r = set->requests.v[i];
		Buf content = {0};
		buf_adds(&content, "// @generated\n");
		if (contents[i])
			buf_add_str(&content, buf_view(contents[i]));
		buf_adds(&content, "// @@protoc_insertion_point(module)\n");
		ProstFile *f = prost_write_to_file(r, buf_view(&content));
		if (f)
			vec_push(&run->files, f);
	}
	if (o->file_descriptor_set)
		prost_file_descriptor_set_generate(set, &run->files);
}
