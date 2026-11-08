/* Copyright 2020 Google LLC. All Rights Reserved.
 * Licensed under the Apache License, Version 2.0 (see LICENSE of github.com/google/gnostic). */

/* Port of cmd/protoc-gen-openapi/generator/generator.go (gnostic v0.7.1).
 *
 * Required schemas: upstream keeps a list of required schema names and, while it is not empty,
 * walks all messages of all files (nested messages first), generating the schema of every
 * message whose name is in the list and not yet generated, then drops the names that were in
 * the list when the walk started. That walk is repeated once per level of references, which is
 * quadratic for long chains. The port computes the same result in O(n log n): the messages are
 * numbered in walk order, and every name added to the list is scheduled at the first message with
 * that name that the current walk has not passed yet, or else at its first message in the next
 * walk (schedule_required, process_required). */

static const char infoURL[] = "https://github.com/google/gnostic/tree/master/cmd/protoc-gen-openapi";

/* ---- regular expressions ---- */

/* linterRulePattern.ReplaceAllString(s, ""): `\(-- .* --\)` (`.` is any byte but \n) */
static Str replace_linter_rules(Str s)
{
	Buf out = {0};
	size_t i = 0, copied = 0;
	size_t line_end = 0, last_close = 0;
	bool have_close = false;
	size_t line_start_scanned = (size_t)-1;
	while (i + 4 <= s.n) {
		/* the line containing i: [ls, line_end) */
		if (line_start_scanned == (size_t)-1 || i >= line_end) {
			size_t e = i;
			while (e < s.n && s.p[e] != '\n')
				e++;
			line_end = e;
			have_close = false;
			for (size_t j = i; j + 4 <= e; j++)
				if (memcmp(s.p + j, " --)", 4) == 0)
					last_close = j, have_close = true;
			line_start_scanned = i;
		}
		if (memcmp(s.p + i, "(-- ", 4) == 0 && have_close && last_close >= i + 4) {
			buf_add(&out, s.p + copied, i - copied);
			i = last_close + 4;
			copied = i;
			continue;
		}
		i++;
	}
	if (copied == 0)
		return s;
	buf_add(&out, s.p + copied, s.n - copied);
	return buf_view(&out);
}

/* filterCommentString removes linter rules from comments. */
static Str filter_comment_string(PgComments c)
{
	return go_trim_space(replace_linter_rules(c.s));
}

/* pathPattern.FindAllStringSubmatch(path, -1): `{([^=}]+)}`; returns the groups. */
static int find_path_params(Str s, Str **out)
{
	int n = 0;
	Str *v = NULL;
	for (size_t i = 0; i < s.n; i++) {
		if (s.p[i] != '{')
			continue;
		size_t k = i + 1;
		while (k < s.n && s.p[k] != '=' && s.p[k] != '}')
			k++;
		if (k > i + 1 && k < s.n && s.p[k] == '}') {
			v = xrealloc(v, sizeof *v * (size_t)n, sizeof *v * (size_t)(n + 1));
			v[n++] = (Str){s.p + i + 1, k - i - 1};
			i = k;
		} else if (k > i + 1) {
			/* every start in (i, k) stops at k too */
			i = k - 1;
		}
	}
	*out = v;
	return n;
}

/* namedPathPattern.FindStringSubmatch(path): `{(.+)=(.+)}` (`.` is any byte but \n); m[0] the
 * match, m[1], m[2] the groups. Leftmost-first with greedy groups: the match starts at the first
 * '{' of a line, ends at the last '}' of that line, and group 1 ends at the last '=' that leaves
 * both groups non-empty; if the first '{' of a line has no match, no later one has. */
static bool find_named_path(Str s, Str m[3])
{
	size_t ls = 0;
	while (ls <= s.n) {
		size_t le = ls;
		while (le < s.n && s.p[le] != '\n')
			le++;
		size_t b = ls;
		while (b < le && s.p[b] != '{')
			b++;
		if (b < le) {
			/* c: the last '}' of the line after b */
			size_t c = le;
			while (c > b + 1 && s.p[c - 1] != '}')
				c--;
			if (c > b + 1) {
				c--;
				/* e: the last '=' with b+2 <= e <= c-2 */
				for (size_t e = c >= 2 ? c - 2 : 0; e >= b + 2 && e < c; e--) {
					if (s.p[e] == '=') {
						m[0] = (Str){s.p + b, c + 1 - b};
						m[1] = (Str){s.p + b + 1, e - b - 1};
						m[2] = (Str){s.p + e + 1, c - e - 1};
						return true;
					}
					if (e == 0)
						break;
				}
			}
		}
		ls = le + 1;
	}
	return false;
}

/* strings.Replace(s, old, new, 1) */
static Str replace_first(Str s, Str old, Str new)
{
	if (old.n == 0) {
		Buf b = {0};
		buf_add_str(&b, new);
		buf_add_str(&b, s);
		return buf_view(&b);
	}
	for (size_t i = 0; i + old.n <= s.n; i++) {
		if (memcmp(s.p + i, old.p, old.n) == 0) {
			Buf b = {0};
			buf_add(&b, s.p, i);
			buf_add_str(&b, new);
			buf_add(&b, s.p + i + old.n, s.n - i - old.n);
			return buf_view(&b);
		}
	}
	return s;
}

/* ---- required schemas ---- */

static void heap_push(Heap *h, Pending p)
{
	if (h->n == h->cap) {
		int cap = h->cap ? h->cap * 2 : 16;
		h->v = xrealloc(h->v, sizeof *h->v * (size_t)h->cap, sizeof *h->v * (size_t)cap);
		h->cap = cap;
	}
	int i = h->n++;
	while (i > 0) {
		int parent = (i - 1) / 2;
		if (h->v[parent].pos <= p.pos)
			break;
		h->v[i] = h->v[parent];
		i = parent;
	}
	h->v[i] = p;
}

static Pending heap_pop(Heap *h)
{
	Pending top = h->v[0], last = h->v[--h->n];
	int i = 0;
	for (;;) {
		int c = 2 * i + 1;
		if (c >= h->n)
			break;
		if (c + 1 < h->n && h->v[c + 1].pos < h->v[c].pos)
			c++;
		if (h->v[c].pos >= last.pos)
			break;
		h->v[i] = h->v[c];
		i = c;
	}
	if (h->n)
		h->v[i] = last;
	return top;
}

static NameInfo *name_info(Gen *g, Str name)
{
	const char *key = xstrndup(name.p, name.n); /* names have no NUL (ASCII identifiers) */
	NameInfo *ni = map_get(&g->names, key);
	if (!ni) {
		ni = xalloc(sizeof *ni);
		ni->last_pass = -2;
		map_put(&g->names, key, ni);
	}
	return ni;
}

/* schemaReferenceForMessage: `if !contains(r.requiredSchemas, schemaName) { append }`. The list
 * holds the names added in the previous walk and in the current one. */
static void require_schema(Gen *g, Str name)
{
	NameInfo *ni = name_info(g, name);
	if (ni->last_pass == g->pass || ni->last_pass == g->pass - 1)
		return;
	ni->last_pass = g->pass;
	if (ni->generated || !ni->n)
		return;
	/* the first message with that name after the current position, in this walk */
	int lo = 0, hi = ni->n;
	while (lo < hi) {
		int m = (lo + hi) / 2;
		if (ni->pos[m] <= g->cur_pos)
			lo = m + 1;
		else
			hi = m;
	}
	if (g->pass > 0 && lo < ni->n)
		heap_push(&g->cur, (Pending){ni->pos[lo], ni});
	else
		heap_push(&g->next, (Pending){ni->pos[0], ni});
}

static void walk_messages(Gen *g, PgMessage **messages, int n)
{
	for (int i = 0; i < n; i++) {
		const PgMessage *message = messages[i];
		if (message->nMessages)
			walk_messages(g, message->Messages, message->nMessages);
		Walked *w = xalloc(sizeof *w);
		w->message = message;
		w->name = format_message_name(g->conf, message->Desc);
		vec_push(&g->walk, w);
	}
}

/* ---- the generator ---- */

/* addSchemaForMessageToDocumentV3 adds the schema to the document if required */
static void add_schema_to_document(Gen *g, PbMsg *d, PbMsg *schema)
{
	Str name = oa_str(schema, OA_NamedSchemaOrReference_name);
	NameInfo *ni = name_info(g, name);
	if (ni->generated)
		return;
	ni->generated = true;
	PbMsg *components = oa_msg(d, OA_Document_components);
	PbMsg *schemas = oa_msg(components, OA_Components_schemas);
	if (!schemas)
		nil_panic("addSchemaToDocumentV3", NULL);
	oa_add_msg(schemas, OA_SchemasOrReferences_additional_properties, schema);
}

static const GoField *find_field(Str name, const PgMessage *in)
{
	for (int i = 0; i < in->nFields; i++) {
		const GoField *f = in->Fields[i]->Desc;
		if (str_eqc(name, f->d.name) || str_equal(name, f->json_name))
			return f;
	}
	return NULL;
}

static const PgField *find_pg_field(Str name, const PgMessage *in)
{
	for (int i = 0; i < in->nFields; i++) {
		const GoField *f = in->Fields[i]->Desc;
		if (str_eqc(name, f->d.name) || str_equal(name, f->json_name))
			return in->Fields[i];
	}
	return NULL;
}

static Str find_and_format_field_name(Gen *g, Str name, const PgMessage *in)
{
	const GoField *f = find_field(name, in);
	if (f)
		return format_field_name(g->conf, f);
	return name;
}

static PbMsg *new_parameter(Str name, const char *in, Str description, bool required, PbMsg *schema)
{
	PbMsg *p = oa_new(OA_Parameter);
	oa_set_str(p, OA_Parameter_name, name);
	oa_set_cstr(p, OA_Parameter_in, in);
	oa_set_str(p, OA_Parameter_description, description);
	oa_set_bool(p, OA_Parameter_required, required);
	oa_set_msg(p, OA_Parameter_schema, schema);
	PbMsg *r = oa_new(OA_ParameterOrReference);
	oa_set_msg(r, OA_ParameterOrReference_parameter, p);
	return r;
}

/* Nesting of query parameters deeper than this is not supported (with a large depth option,
 * the parameters of a recursive message grow quadratically). */
#define MAX_QUERY_DEPTH 1000

/* Note that fields which are mapped to URL query parameters must have a primitive type or a
 * repeated primitive type or a non-repeated message type. (...) maps, Struct and Empty can NOT be
 * used; messages can have any number of sub messages - including circular (e.g.
 * sub.subsub.sub.subsub.id). depths are used to keep track of how many times a message's fields
 * has been seen. */
static void build_query_params(Gen *g, const PgField *field, Map *depths, Vec *parameters, int level)
{
	if (level > MAX_QUERY_DEPTH)
		fail("unsupported by protoc-gen-uni: query parameters nested deeper than %d levels (option depth)", MAX_QUERY_DEPTH);
	Str query_field_name = format_field_name(g->conf, field->Desc);
	Str field_description = filter_comment_string(field->Comments.Leading);
	const GoField *fd = field->Desc;

	if (go_field_is_map(fd)) {
		/* Map types are not allowed in query parameteres */
		return;
	} else if (fd->kind == PT_MESSAGE) {
		Str t = full_message_type_name(fd->message_type);
		if (str_eqc(t, ".google.protobuf.Value")) {
			PbMsg *s = schema_or_reference_for_field(g, fd);
			vec_push(parameters, new_parameter(query_field_name, "query", field_description, false, s));
			return;
		}
		if (str_eqc(t, ".google.protobuf.BoolValue") || str_eqc(t, ".google.protobuf.BytesValue") ||
		    str_eqc(t, ".google.protobuf.Int32Value") || str_eqc(t, ".google.protobuf.UInt32Value") ||
		    str_eqc(t, ".google.protobuf.StringValue") || str_eqc(t, ".google.protobuf.Int64Value") ||
		    str_eqc(t, ".google.protobuf.UInt64Value") || str_eqc(t, ".google.protobuf.FloatValue") ||
		    str_eqc(t, ".google.protobuf.DoubleValue")) {
			const GoField *value_field = get_value_field(field->Message->Desc);
			PbMsg *s = schema_or_reference_for_field(g, value_field);
			vec_push(parameters, new_parameter(query_field_name, "query", field_description, false, s));
			return;
		}
		if (str_eqc(t, ".google.protobuf.Timestamp") || str_eqc(t, ".google.protobuf.Duration")) {
			PbMsg *s = schema_or_reference_for_message(g, field->Message->Desc);
			vec_push(parameters, new_parameter(query_field_name, "query", field_description, false, s));
			return;
		}

		if (go_field_is_list(fd)) {
			/* Only non-repeated message types are valid */
			return;
		}

		/* Represent field masks directly as strings (don't expand them). */
		if (str_eqc(t, ".google.protobuf.FieldMask")) {
			PbMsg *s = schema_or_reference_for_field(g, fd);
			vec_push(parameters, new_parameter(query_field_name, "query", field_description, false, s));
			return;
		}

		/* Sub messages are allowed, even circular, as long as the final type is a
		 * primitive. Go through each of the sub message fields */
		const PgMessage *m = field->Message;
		for (int i = 0; i < m->nFields; i++) {
			const PgField *sub = m->Fields[i];
			const char *full = sub->Desc->d.full_name;
			intptr_t seen = (intptr_t)map_get(depths, full);
			if (seen < g->conf->CircularDepth) {
				map_put(depths, full, (void *)(seen + 1));
				Vec sub_params = {0};
				build_query_params(g, sub, depths, &sub_params, level + 1);
				for (int k = 0; k < sub_params.n; k++) {
					PbMsg *por = sub_params.v[k];
					PbMsg *param = oa_msg(por, OA_ParameterOrReference_parameter);
					if (param) {
						Buf b = {0};
						buf_add_str(&b, query_field_name);
						buf_addc(&b, '.');
						buf_add_str(&b, oa_str(param, OA_Parameter_name));
						oa_set_str(param, OA_Parameter_name, buf_view(&b));
						vec_push(parameters, por);
					}
				}
			}
		}
	} else if (fd->kind != PT_GROUP) {
		/* schemaOrReferenceForField also handles array types */
		PbMsg *s = schema_or_reference_for_field(g, fd);
		vec_push(parameters, new_parameter(query_field_name, "query", field_description, false, s));
	}
}

static PbMsg *string_schema(void)
{
	PbMsg *s = oa_new(OA_Schema);
	oa_set_cstr(s, OA_Schema_type, "string");
	PbMsg *r = oa_new(OA_SchemaOrReference);
	oa_set_msg(r, OA_SchemaOrReference_schema, s);
	return r;
}

/* buildOperationV3 constructs an operation for a set of values. */
static PbMsg *build_operation(Gen *g, PbMsg *d, Str operation_id, Str tag_name, Str description, Str default_host, Str path,
			      Str body_field, const PgMessage *input, const PgMessage *output, Str *path_out)
{
	/* coveredParameters tracks the parameters that have been used in the body or path. */
	Vec covered = {0};
	if (body_field.n)
		vec_push_str(&covered, body_field);
	/* Initialize the list of operation parameters. */
	Vec parameters = {0};

	/* Find simple path parameters like {id} */
	Str *matches;
	int nm = find_path_params(path, &matches);
	for (int i = 0; i < nm; i++) {
		/* Add the value to the list of covered parameters. */
		vec_push_str(&covered, matches[i]);
		Str path_parameter = find_and_format_field_name(g, matches[i], input);
		path = replace_first(path, matches[i], path_parameter);

		/* Add the path parameters to the operation parameters. */
		PbMsg *field_schema;
		Str field_description = {"", 0};
		const PgField *field = find_pg_field(path_parameter, input);
		if (field) {
			field_schema = schema_or_reference_for_field(g, field->Desc);
			field_description = filter_comment_string(field->Comments.Leading);
		} else {
			/* If field does not exist, it is safe to set it to string, as it is ignored
			 * downstream */
			field_schema = string_schema();
		}
		vec_push(&parameters, new_parameter(path_parameter, "path", field_description, true, field_schema));
	}

	/* Find named path parameters like {name=shelves/(star)} */
	Str m[3];
	if (find_named_path(path, m)) {
		/* Build a list of named path parameters. */
		Vec named = {0};
		/* Add the "name=" "name" value to the list of covered parameters. */
		vec_push_str(&covered, m[1]);
		/* Convert the path from the starred form to use named path parameters. */
		Str starred = m[2];
		Str *parts;
		int np = str_split_s(starred, '/', &parts);
		/* The starred path is assumed to be in the form "things/(star)/otherthings/(star)". We want
		 * to convert it to "things/{thingsId}/otherthings/{otherthingsId}". */
		for (int i = 0; i < np - 1; i += 2) {
			Str section = parts[i];
			Str p = find_and_format_field_name(g, section, input);
			p = singular(p);
			Buf b = {0};
			buf_addc(&b, '{');
			buf_add_str(&b, p);
			buf_addc(&b, '}');
			parts[i + 1] = buf_view(&b);
			vec_push_str(&named, p);
		}
		/* Rewrite the path to use the path parameters. */
		Buf np_ = {0};
		for (int i = 0; i < np; i++) {
			if (i)
				buf_addc(&np_, '/');
			buf_add_str(&np_, parts[i]);
		}
		path = replace_first(path, m[0], buf_view(&np_));

		/* Add the named path parameters to the operation parameters. */
		for (int i = 0; i < named.n; i++) {
			Str p = *(Str *)named.v[i];
			Buf desc = {0};
			buf_adds(&desc, "The ");
			buf_add_str(&desc, p);
			buf_adds(&desc, " id.");
			vec_push(&parameters, new_parameter(p, "path", buf_view(&desc), true, string_schema()));
		}
	}

	/* Add any unhandled fields in the request message as query parameters. */
	if (!str_eqc(body_field, "*") && !str_eq(input->Desc->d.full_name, "google.api.HttpBody")) {
		for (int i = 0; i < input->nFields; i++) {
			const PgField *field = input->Fields[i];
			Str field_name = str_c(field->Desc->d.name);
			if (!contains(&covered, field_name) && !str_equal(field_name, body_field)) {
				Map depths = {0};
				build_query_params(g, field, &depths, &parameters, 0);
			}
		}
	}

	/* Create the response. */
	PbMsg *content;
	Str name = response_content_for_message(g, output->Desc, &content);
	PbMsg *response = oa_new(OA_Response);
	oa_set_cstr(response, OA_Response_description, "OK");
	oa_set_msg(response, OA_Response_content, content);
	PbMsg *ror = oa_new(OA_ResponseOrReference);
	oa_set_msg(ror, OA_ResponseOrReference_response, response);
	PbMsg *named_response = oa_new(OA_NamedResponseOrReference);
	oa_set_str(named_response, OA_NamedResponseOrReference_name, name);
	oa_set_msg(named_response, OA_NamedResponseOrReference_value, ror);
	PbMsg *responses = oa_new(OA_Responses);
	oa_add_msg(responses, OA_Responses_response_or_reference, named_response);

	/* Add the default reponse if needed */
	if (g->conf->DefaultResponse) {
		Str any_schema_name = format_message_name_of(g->conf, "google.protobuf", STR_LIT("Any"), STR_LIT(".google.protobuf.Any"));
		add_schema_to_document(g, d, wk_NewGoogleProtobufAnySchema(any_schema_name));

		Str status_schema_name = format_message_name_of(g->conf, "google.rpc", STR_LIT("Status"), STR_LIT(".google.rpc.Status"));
		add_schema_to_document(g, d, wk_NewGoogleRpcStatusSchema(status_schema_name, any_schema_name));

		PbMsg *ref = oa_new(OA_Reference);
		oa_set_cstr(ref, OA_Reference__ref, xcat("#/components/schemas/", status_schema_name.p));
		PbMsg *sor = oa_new(OA_SchemaOrReference);
		oa_set_msg(sor, OA_SchemaOrReference_reference, ref);
		PbMsg *r = oa_new(OA_Response);
		oa_set_cstr(r, OA_Response_description, "Default error response");
		oa_set_msg(r, OA_Response_content, wk_NewApplicationJsonMediaType(sor));
		PbMsg *rr = oa_new(OA_ResponseOrReference);
		oa_set_msg(rr, OA_ResponseOrReference_response, r);
		PbMsg *def = oa_new(OA_NamedResponseOrReference);
		oa_set_cstr(def, OA_NamedResponseOrReference_name, "default");
		oa_set_msg(def, OA_NamedResponseOrReference_value, rr);
		oa_add_msg(responses, OA_Responses_response_or_reference, def);
	}

	/* Create the operation. */
	PbMsg *op = oa_new(OA_Operation);
	oa_add_str(op, OA_Operation_tags, tag_name);
	oa_set_str(op, OA_Operation_description, description);
	oa_set_str(op, OA_Operation_operation_id, operation_id);
	for (int i = 0; i < parameters.n; i++)
		oa_add_msg(op, OA_Operation_parameters, parameters.v[i]);
	oa_set_msg(op, OA_Operation_responses, responses);

	if (default_host.n) {
		go_std_url host_url;
		if (!go_std_url_parse(default_host, &host_url)) {
			host_url.Scheme = STR_LIT("https");
			PbMsg *server = oa_new(OA_Server);
			oa_set_str(server, OA_Server_url, go_std_url_string(&host_url));
			oa_add_msg(op, OA_Operation_servers, server);
		}
	}

	/* If a body field is specified, we need to pass a message as the request body. */
	if (body_field.n) {
		PbMsg *request_schema = NULL;
		if (str_eqc(body_field, "*")) {
			/* Pass the entire request message as the request body. */
			request_schema = schema_or_reference_for_message(g, input->Desc);
		} else {
			/* If body refers to a message field, use that type. */
			for (int i = 0; i < input->nFields; i++) {
				const PgField *field = input->Fields[i];
				if (str_eqc(body_field, field->Desc->d.name)) {
					switch (field->Desc->kind) {
					case PT_STRING:
						request_schema = string_schema();
						break;
					case PT_MESSAGE:
						request_schema = schema_or_reference_for_message(g, field->Message->Desc);
						break;
					default:
						/* log.Printf("unsupported field type %+v", field.Desc) */
						break;
					}
					break;
				}
			}
		}
		PbMsg *mt = oa_new(OA_MediaType);
		oa_set_msg(mt, OA_MediaType_schema, request_schema);
		PbMsg *nmt = oa_new(OA_NamedMediaType);
		oa_set_cstr(nmt, OA_NamedMediaType_name, "application/json");
		oa_set_msg(nmt, OA_NamedMediaType_value, mt);
		PbMsg *mts = oa_new(OA_MediaTypes);
		oa_add_msg(mts, OA_MediaTypes_additional_properties, nmt);
		PbMsg *rb = oa_new(OA_RequestBody);
		oa_set_bool(rb, OA_RequestBody_required, true);
		oa_set_msg(rb, OA_RequestBody_content, mts);
		PbMsg *rbr = oa_new(OA_RequestBodyOrReference);
		oa_set_msg(rbr, OA_RequestBodyOrReference_request_body, rb);
		oa_set_msg(op, OA_Operation_request_body, rbr);
	}
	*path_out = path;
	return op;
}

/* addOperationToDocumentV3 adds an operation to the specified path/method. */
static void add_operation_to_document(Gen *g, PbMsg *d, PbMsg *op, Str path, const char *method_name)
{
	PbMsg *paths = oa_msg(d, OA_Document_paths);
	if (!paths)
		nil_panic("addOperationToDocumentV3", NULL);
	/* index the path items not seen yet (the first one of a name wins, as the linear search
	 * of upstream finds it) */
	const PbSlot *items = pb_slot(paths, OA_Paths_path);
	int n = items ? items->n : 0;
	for (; g->paths_indexed < n; g->paths_indexed++) {
		PbMsg *item = items->v[g->paths_indexed].m;
		Str name = oa_str(item, OA_NamedPathItem_name);
		const char *key = xstrndup(name.p, name.n);
		if (memchr(name.p, 0, name.n))
			key = xcat("\x01", key); /* not a valid lookup key of a C string: never found */
		if (!map_has(&g->path_items, key))
			map_put(&g->path_items, key, item);
	}
	PbMsg *selected = NULL;
	if (!memchr(path.p, 0, path.n))
		selected = map_get(&g->path_items, xstrndup(path.p, path.n));
	/* If we get here, we need to create a path item. */
	if (!selected) {
		selected = oa_new(OA_NamedPathItem);
		oa_set_str(selected, OA_NamedPathItem_name, path);
		oa_set_msg(selected, OA_NamedPathItem_value, oa_new(OA_PathItem));
		oa_add_msg(paths, OA_Paths_path, selected);
	}
	/* Set the operation on the specified method. */
	PbMsg *value = oa_msg(selected, OA_NamedPathItem_value);
	if (!value)
		nil_panic("addOperationToDocumentV3", NULL);
	if (str_eq(method_name, "GET"))
		oa_set_msg(value, OA_PathItem_get, op);
	else if (str_eq(method_name, "POST"))
		oa_set_msg(value, OA_PathItem_post, op);
	else if (str_eq(method_name, "PUT"))
		oa_set_msg(value, OA_PathItem_put, op);
	else if (str_eq(method_name, "DELETE"))
		oa_set_msg(value, OA_PathItem_delete, op);
	else if (str_eq(method_name, "PATCH"))
		oa_set_msg(value, OA_PathItem_patch, op);
}

/* proto.GetExtension of a message extension: the decoded message, or NULL (typed nil) */
static PbMsg *get_extension(const PbMsg *opts, uint32_t num, const GoMsgDef *def)
{
	bool present;
	Str b = genproto_ext_message(opts, num, &present);
	if (!present)
		return NULL;
	PbMsg *m = pb_new(&def->pb);
	const char *err = gopb_decode(m, (const uint8_t *)b.p, b.n);
	if (err)
		fail("%s", err); /* checked when the request is parsed (check_extensions) */
	return m;
}

/* addPathsToDocumentV3 adds paths from a specified file descriptor. */
static void add_paths_to_document(Gen *g, PbMsg *d, PgService **services, int n)
{
	for (int s = 0; s < n; s++) {
		const PgService *service = services[s];
		int annotations_count = 0;

		for (int k = 0; k < service->nMethods; k++) {
			const PgMethod *method = service->Methods[k];
			Str comment = filter_comment_string(method->Comments.Leading);
			const PgMessage *input = method->Input, *output = method->Output;
			Str operation_id = str_c(xcat(service->GoName, "_", method->GoName));

			Vec rules = {0};
			PbMsg *rule = get_extension(method->Desc->d.options, GENPROTO_E_HTTP, genproto_http_rule());
			if (rule) {
				annotations_count++;
				vec_push(&rules, rule);
				const PbSlot *ab = pb_slot(rule, GENPROTO_HttpRule_additional_bindings);
				for (int i = 0; ab && i < ab->n; i++)
					vec_push(&rules, ab->v[i].m);
			}

			for (int r = 0; r < rules.n; r++) {
				const PbMsg *rl = rules.v[r];
				Str path = {"", 0};
				const char *method_name = "";
				Str body = pb_bytes(rl, GENPROTO_HttpRule_body);
				if (pb_has(rl, GENPROTO_HttpRule_get)) {
					path = pb_bytes(rl, GENPROTO_HttpRule_get);
					method_name = "GET";
				} else if (pb_has(rl, GENPROTO_HttpRule_post)) {
					path = pb_bytes(rl, GENPROTO_HttpRule_post);
					method_name = "POST";
				} else if (pb_has(rl, GENPROTO_HttpRule_put)) {
					path = pb_bytes(rl, GENPROTO_HttpRule_put);
					method_name = "PUT";
				} else if (pb_has(rl, GENPROTO_HttpRule_delete)) {
					path = pb_bytes(rl, GENPROTO_HttpRule_delete);
					method_name = "DELETE";
				} else if (pb_has(rl, GENPROTO_HttpRule_patch)) {
					path = pb_bytes(rl, GENPROTO_HttpRule_patch);
					method_name = "PATCH";
				}
				/* custom: "custom-unsupported", none: "unknown-unsupported"; no method */

				if (*method_name) {
					Str default_host = genproto_ext_string(service->Desc->d.options, GENPROTO_E_DEFAULT_HOST);
					Str path2;
					PbMsg *op = build_operation(g, d, operation_id, str_c(service->GoName), comment, default_host, path, body,
								    input, output, &path2);

					/* Merge any `Operation` annotations with the current */
					oa_merge(op, get_extension(method->Desc->d.options, 1143, oa_def(OA_Operation)));

					add_operation_to_document(g, d, op, path2, method_name);
				}
			}
		}

		if (annotations_count > 0) {
			Str comment = filter_comment_string(service->Comments.Leading);
			PbMsg *tag = oa_new(OA_Tag);
			oa_set_cstr(tag, OA_Tag_name, service->GoName);
			oa_set_str(tag, OA_Tag_description, comment);
			oa_add_msg(d, OA_Document_tags, tag);
		}
	}
}

/* The schema of one message (the body of the loop of addSchemasForMessagesToDocumentV3, for a
 * message whose name is required and not yet generated). */
static void add_schema_for_message(Gen *g, PbMsg *d, const PgMessage *message, Str schema_name)
{
	Str type_name = full_message_type_name(message->Desc);
	Str message_description = filter_comment_string(message->Comments.Leading);

	/* `google.protobuf.Value` and `google.protobuf.Any` have special JSON transcoding so we
	 * can't just reflect on the message descriptor. */
	if (str_eqc(type_name, ".google.protobuf.Value")) {
		add_schema_to_document(g, d, wk_NewGoogleProtobufValueSchema(schema_name));
		return;
	} else if (str_eqc(type_name, ".google.protobuf.Any")) {
		add_schema_to_document(g, d, wk_NewGoogleProtobufAnySchema(schema_name));
		return;
	} else if (str_eqc(type_name, ".google.rpc.Status")) {
		Str any_schema_name = format_message_name_of(g->conf, "google.protobuf", STR_LIT("Any"), STR_LIT(".google.protobuf.Any"));
		add_schema_to_document(g, d, wk_NewGoogleProtobufAnySchema(any_schema_name));
		add_schema_to_document(g, d, wk_NewGoogleRpcStatusSchema(schema_name, any_schema_name));
		return;
	}

	/* Build an array holding the fields of the message. */
	PbMsg *definition_properties = oa_new(OA_Properties);

	Vec required = {0};
	for (int i = 0; i < message->nFields; i++) {
		const PgField *field = message->Fields[i];
		/* Get the field description from the comments. */
		Str description = filter_comment_string(field->Comments.Leading);
		/* Check the field annotations to see if this is a readonly or writeonly field. */
		bool input_only = false, output_only = false;
		int32_t *behaviors;
		int nb = genproto_ext_enums(field->Desc->d.options, GENPROTO_E_FIELD_BEHAVIOR, &behaviors);
		for (int k = 0; k < nb; k++) {
			switch (behaviors[k]) {
			case GENPROTO_FIELD_BEHAVIOR_OUTPUT_ONLY:
				output_only = true;
				break;
			case GENPROTO_FIELD_BEHAVIOR_INPUT_ONLY:
				input_only = true;
				break;
			case GENPROTO_FIELD_BEHAVIOR_REQUIRED:
				vec_push_str(&required, format_field_name(g->conf, field->Desc));
				break;
			}
		}

		/* The field is either described by a reference or a schema. */
		PbMsg *field_schema = schema_or_reference_for_field(g, field->Desc);
		if (!field_schema)
			continue;

		/* If this field has siblings and is a $ref now, create a new schema use `allOf` to
		 * wrap it */
		bool wrapper_needed = input_only || output_only || description.n;
		if (wrapper_needed && oa_msg(field_schema, OA_SchemaOrReference_reference)) {
			PbMsg *s = oa_new(OA_Schema);
			oa_add_msg(s, OA_Schema_all_of, field_schema);
			field_schema = oa_new(OA_SchemaOrReference);
			oa_set_msg(field_schema, OA_SchemaOrReference_schema, s);
		}

		PbMsg *schema = oa_msg(field_schema, OA_SchemaOrReference_schema);
		if (schema) {
			oa_set_str(schema, OA_Schema_description, description);
			oa_set_bool(schema, OA_Schema_read_only, output_only);
			oa_set_bool(schema, OA_Schema_write_only, input_only);

			/* Merge any `Property` annotations with the current */
			oa_merge(schema, get_extension(field->Desc->d.options, 1143, oa_def(OA_Schema)));
		}

		PbMsg *named = oa_new(OA_NamedSchemaOrReference);
		oa_set_str(named, OA_NamedSchemaOrReference_name, format_field_name(g->conf, field->Desc));
		oa_set_msg(named, OA_NamedSchemaOrReference_value, field_schema);
		oa_add_msg(definition_properties, OA_Properties_additional_properties, named);
	}

	PbMsg *schema = oa_new(OA_Schema);
	oa_set_cstr(schema, OA_Schema_type, "object");
	oa_set_str(schema, OA_Schema_description, message_description);
	oa_set_msg(schema, OA_Schema_properties, definition_properties);
	for (int i = 0; i < required.n; i++)
		oa_add_str(schema, OA_Schema_required, *(Str *)required.v[i]);

	/* Merge any `Schema` annotations with the current */
	oa_merge(schema, get_extension(message->Desc->d.options, 1143, oa_def(OA_Schema)));

	/* Add the schema to the components.schema list. */
	PbMsg *sor = oa_new(OA_SchemaOrReference);
	oa_set_msg(sor, OA_SchemaOrReference_schema, schema);
	PbMsg *named = oa_new(OA_NamedSchemaOrReference);
	oa_set_str(named, OA_NamedSchemaOrReference_name, schema_name);
	oa_set_msg(named, OA_NamedSchemaOrReference_value, sor);
	add_schema_to_document(g, d, named);
}

/* The loop of buildDocumentV3 over the required schemas (see the comment at the top). */
static void process_required(Gen *g, PbMsg *d)
{
	g->pass = 1;
	for (;;) {
		Heap tmp = g->cur;
		g->cur = g->next;
		g->next = tmp;
		g->next.n = 0;
		if (!g->cur.n)
			break;
		while (g->cur.n) {
			Pending p = heap_pop(&g->cur);
			if (p.info->generated)
				continue;
			g->cur_pos = p.pos;
			const Walked *w = g->walk.v[p.pos];
			add_schema_for_message(g, d, w->message, w->name);
		}
		g->pass++;
		g->cur_pos = -1;
	}
}

static bool less_by_name(const void *a, const void *b, void *ctx)
{
	uint32_t num = (uint32_t)(uintptr_t)ctx;
	Str x = oa_str(a, num), y = oa_str(b, num);
	size_t n = x.n < y.n ? x.n : y.n;
	int c = n ? memcmp(x.p, y.p, n) : 0;
	return c < 0 || (c == 0 && x.n < y.n);
}

/* sort.Slice(pairs, func(i, j int) bool { return pairs[i].Name < pairs[j].Name }) */
static void sort_by_name(PbMsg *m, uint32_t list, uint32_t name)
{
	if (!m)
		return;
	PbSlot *s = (PbSlot *)pb_slot(m, list);
	if (!s || s->n < 2)
		return;
	void **v = xalloc(sizeof *v * (size_t)s->n);
	for (int i = 0; i < s->n; i++) {
		v[i] = s->v[i].m;
		if (!v[i])
			nil_panic("buildDocumentV3", NULL);
	}
	go_std_sort_slice(v, (size_t)s->n, less_by_name, (void *)(uintptr_t)name);
	for (int i = 0; i < s->n; i++)
		s->v[i].m = v[i];
}

/* buildDocumentV3 builds an OpenAPIv3 document for a plugin request. */
static PbMsg *build_document(Gen *g)
{
	PbMsg *d = oa_new(OA_Document);
	oa_set_cstr(d, OA_Document_openapi, "3.0.3");
	PbMsg *info = oa_new(OA_Info);
	oa_set_str(info, OA_Info_version, g->conf->Version);
	oa_set_str(info, OA_Info_title, g->conf->Title);
	oa_set_str(info, OA_Info_description, g->conf->Description);
	oa_set_msg(d, OA_Document_info, info);
	oa_set_msg(d, OA_Document_paths, oa_new(OA_Paths));
	PbMsg *components = oa_new(OA_Components);
	oa_set_msg(components, OA_Components_schemas, oa_new(OA_SchemasOrReferences));
	oa_set_msg(d, OA_Document_components, components);

	/* Go through the files and add the services to the documents, keeping track of which
	 * schemas are referenced in the response so we can add them later. */
	g->pass = 0;
	g->cur_pos = -1;
	for (int i = 0; i < g->nInputFiles; i++) {
		const PgFile *file = g->inputFiles[i];
		if (!file->Generate)
			continue;
		/* Merge any `Document` annotations with the current */
		oa_merge(d, get_extension(file->Desc->d.options, 1143, oa_def(OA_Document)));
		add_paths_to_document(g, d, file->Services, file->nServices);
	}

	/* While we have required schemas left to generate, go through the files again looking for
	 * the related message and adding them to the document if required. */
	process_required(g, d);

	/* If there is only 1 service, then use it's title for the document, if the document is
	 * missing it. */
	const PbSlot *tags = pb_slot(d, OA_Document_tags);
	if (tags && tags->n == 1) {
		PbMsg *tag = tags->v[0].m;
		PbMsg *dinfo = oa_msg(d, OA_Document_info);
		if (!dinfo || !tag)
			nil_panic("buildDocumentV3", NULL);
		Str tag_name = oa_str(tag, OA_Tag_name);
		if (!oa_str(dinfo, OA_Info_title).n && tag_name.n) {
			Buf b = {0};
			buf_add_str(&b, tag_name);
			buf_adds(&b, " API");
			oa_set_str(dinfo, OA_Info_title, buf_view(&b));
		}
		if (!oa_str(dinfo, OA_Info_description).n)
			oa_set_str(dinfo, OA_Info_description, oa_str(tag, OA_Tag_description));
		oa_set_str(tag, OA_Tag_description, (Str){"", 0});
	}

	/* If paths methods has servers, but they're all the same, then move servers to path
	 * level */
	PbMsg *paths = oa_msg(d, OA_Document_paths);
	const PbSlot *path_items = pb_slot(paths, OA_Paths_path);
	Vec all_servers = {0};
	static const uint32_t methods[] = {OA_PathItem_get, OA_PathItem_post, OA_PathItem_put, OA_PathItem_delete, OA_PathItem_patch};
	for (int i = 0; path_items && i < path_items->n; i++) {
		PbMsg *path = path_items->v[i].m;
		PbMsg *value = oa_msg(path, OA_NamedPathItem_value);
		if (!path || !value)
			nil_panic("buildDocumentV3", NULL);
		Vec servers = {0};
		/* Only 1 server will ever be set, per method, by the generator */
		for (size_t k = 0; k < sizeof methods / sizeof *methods; k++) {
			PbMsg *op = oa_msg(value, methods[k]);
			const PbSlot *ss = op ? pb_slot(op, OA_Operation_servers) : NULL;
			if (ss && ss->n == 1) {
				if (!ss->v[0].m)
					nil_panic("buildDocumentV3", NULL);
				Str url = oa_str(ss->v[0].m, OA_Server_url);
				append_unique(&servers, url);
				/* allServers = appendUnique(servers, url): the same slice as servers */
				all_servers = servers;
			}
		}
		if (servers.n == 1) {
			PbMsg *server = oa_new(OA_Server);
			oa_set_str(server, OA_Server_url, *(Str *)servers.v[0]);
			oa_clear(value, OA_PathItem_servers);
			oa_add_msg(value, OA_PathItem_servers, server);
			for (size_t k = 0; k < sizeof methods / sizeof *methods; k++) {
				PbMsg *op = oa_msg(value, methods[k]);
				if (op)
					oa_clear(op, OA_Operation_servers);
			}
		}
	}

	/* Set all servers on API level */
	if (all_servers.n > 0) {
		oa_clear(d, OA_Document_servers);
		for (int i = 0; i < all_servers.n; i++) {
			PbMsg *server = oa_new(OA_Server);
			oa_set_str(server, OA_Server_url, *(Str *)all_servers.v[i]);
			oa_add_msg(d, OA_Document_servers, server);
		}
	}

	/* If there is only 1 server, we can safely remove all path level servers */
	if (all_servers.n == 1)
		for (int i = 0; path_items && i < path_items->n; i++)
			oa_clear(oa_msg(path_items->v[i].m, OA_NamedPathItem_value), OA_PathItem_servers);

	/* Sort the tags. */
	sort_by_name(d, OA_Document_tags, OA_Tag_name);
	/* Sort the paths. */
	sort_by_name(paths, OA_Paths_path, OA_NamedPathItem_name);
	/* Sort the schemas. */
	PbMsg *comps = oa_msg(d, OA_Document_components);
	sort_by_name(oa_msg(comps, OA_Components_schemas), OA_SchemasOrReferences_additional_properties,
		     OA_NamedSchemaOrReference_name);
	return d;
}

/* NewOpenAPIv3Generator + Run: writes the document of input_files to the generated file. */
static Str generator_run(Gen *g, PgGeneratedFile *output_file)
{
	PbMsg *d = build_document(g);
	Buf bytes = {0};
	const char *err = oa_document_yaml_value(d, STR_LIT("Generated with protoc-gen-openapi\nhttps://github.com/google/gnostic/tree/master/cmd/protoc-gen-openapi"), &bytes);
	if (err) {
		if (has_prefix(err, "unsupported by protoc-gen-uni"))
			fail("%s", err);
		return str_c(xcat("failed to marshal yaml: ", err));
	}
	pg_write(output_file, buf_view(&bytes));
	(void)infoURL;
	return (Str){NULL, 0};
}
