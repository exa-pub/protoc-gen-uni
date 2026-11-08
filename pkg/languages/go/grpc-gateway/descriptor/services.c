/* Copyright (c) 2015, Gengo, Inc. All rights reserved. BSD-3-Clause (see LICENSE of
 * github.com/grpc-ecosystem/grpc-gateway). */

/* Port of grpc-gateway v2.33.0 internal/descriptor/services.go, with the change of
 * bufbuild/plugins' separate_pkg_additional_imports.patch (IncludeAdditionalImports). The
 * google.api.http option is read with pkg/languages/go/genproto (proto.GetExtension). */
#include "annotations.h"
#include "internal.h"

#include <string.h>

static Str S(const char *s) { return str_c(s); }

static Str errcat(const char *a, Str b)
{
	Buf o = {0};
	buf_adds(&o, a);
	buf_add_str(&o, b);
	return buf_view(&o);
}

/* lookupField looks up a field named "name" within "msg". It returns NULL if no such field
 * found. */
static GwField *lookupField(GwMessage *msg, Str name)
{
	for (int i = 0; i < msg->Fields.n; i++) {
		GwField *f = msg->Fields.v[i];
		if (str_eqc(name, f->Name))
			return f;
	}
	return NULL;
}

/* resolveFieldPath resolves "path" into a list of fieldDescriptor, starting from "msg". */
static Str resolveFieldPath(GwRegistry *r, GwMessage *msg, Str path, GwFieldPath *out)
{
	*out = (GwFieldPath){NULL, 0};
	if (path.n == 0)
		return (Str){NULL, 0};

	GwMessage *root = msg;
	Str *parts;
	int nparts = str_split_s(path, '.', &parts);
	GwFieldPathComponent *result = xalloc(sizeof *result * (size_t)nparts);
	for (int i = 0; i < nparts; i++) {
		Str c = parts[i];
		if (i > 0) {
			GwField *f = result[i - 1].Target;
			if (f->Type == PT_MESSAGE || f->Type == PT_GROUP) {
				const char *err = NULL;
				msg = gw_Registry_LookupMsg(r, gw_Message_FQMN(msg), f->TypeName, &err);
				if (!msg)
					return S(err);
			} else {
				Buf b = {0};
				buf_fmt(&b, "not an aggregate type: %s in ", f->Name);
				buf_add_str(&b, path);
				return buf_view(&b);
			}
		}

		GwField *f = lookupField(msg, c);
		if (!f) {
			size_t qn;
			char *q = go_std_quote(path.p, path.n, &qn);
			return S(xfmt("no field %s found in %s", q, root->Name));
		}
		result[i] = (GwFieldPathComponent){f->Name, f};
	}
	*out = (GwFieldPath){result, nparts};
	return (Str){NULL, 0};
}

static Str newParam(GwRegistry *r, GwMethod *meth, Str path, GwParameter *out)
{
	GwMessage *msg = meth->RequestType;
	GwFieldPath fields;
	Str err = resolveFieldPath(r, msg, path, &fields);
	if (err.p)
		return err;
	int l = fields.n;
	if (l == 0)
		return errcat("invalid field access list for ", path);
	GwField *target = fields.v[l - 1].Target;
	if (target->Type == PT_MESSAGE || target->Type == PT_GROUP) {
		if (!gw_IsWellKnownType(target->TypeName)) {
			Buf b = {0};
			buf_fmt(&b, "%s.%s: ", meth->Service->Name, meth->Name);
			buf_add_str(&b, path);
			buf_adds(&b, " is a protobuf message type. Protobuf message types cannot be used as path parameters, use a "
				     "scalar value type (such as string) instead");
			return buf_view(&b);
		}
	}
	*out = (GwParameter){fields, target, meth};
	return (Str){NULL, 0};
}

static Str newBody(GwRegistry *r, GwMethod *meth, Str path, GwBody **out)
{
	*out = NULL;
	if (path.n == 0)
		return (Str){NULL, 0};
	if (str_eqc(path, "*")) {
		*out = xalloc(sizeof **out);
		return (Str){NULL, 0};
	}
	GwFieldPath fields;
	Str err = resolveFieldPath(r, meth->RequestType, path, &fields);
	if (err.p)
		return err;
	*out = xalloc(sizeof **out);
	(*out)->FieldPath = fields;
	return (Str){NULL, 0};
}

static Str newResponse(GwRegistry *r, GwMethod *meth, Str path, GwBody **out)
{
	*out = NULL;
	if (path.n == 0 || str_eqc(path, "*"))
		return (Str){NULL, 0};
	GwFieldPath fields;
	Str err = resolveFieldPath(r, meth->ResponseType, path, &fields);
	if (err.p)
		return err;
	*out = xalloc(sizeof **out);
	(*out)->FieldPath = fields;
	return (Str){NULL, 0};
}

/* The HttpRule fields of a binding (options.HttpRule getters). */
static Str newBinding(GwRegistry *r, GwMethod *meth, const PbMsg *opts, int idx, GwBinding **out)
{
	*out = NULL;
	Str httpMethod, pathTemplate;
	Str body = pb_bytes(opts, GENPROTO_HttpRule_body);
	const char *mdName = pb_str(meth->proto, MDP_NAME);
	if (pb_bytes(opts, GENPROTO_HttpRule_get).n) {
		httpMethod = STR_LIT("GET");
		pathTemplate = pb_bytes(opts, GENPROTO_HttpRule_get);
		if (body.n)
			return S(xcat("must not set request body when http method is GET: ", mdName));
	} else if (pb_bytes(opts, GENPROTO_HttpRule_put).n) {
		httpMethod = STR_LIT("PUT");
		pathTemplate = pb_bytes(opts, GENPROTO_HttpRule_put);
	} else if (pb_bytes(opts, GENPROTO_HttpRule_post).n) {
		httpMethod = STR_LIT("POST");
		pathTemplate = pb_bytes(opts, GENPROTO_HttpRule_post);
	} else if (pb_bytes(opts, GENPROTO_HttpRule_delete).n) {
		httpMethod = STR_LIT("DELETE");
		pathTemplate = pb_bytes(opts, GENPROTO_HttpRule_delete);
		if (body.n && !r->allowDeleteBody)
			return S(xcat("must not set request body when http method is DELETE except allow_delete_body option is true: ",
				      mdName));
	} else if (pb_bytes(opts, GENPROTO_HttpRule_patch).n) {
		httpMethod = STR_LIT("PATCH");
		pathTemplate = pb_bytes(opts, GENPROTO_HttpRule_patch);
	} else if (pb_has(opts, GENPROTO_HttpRule_custom)) {
		const PbMsg *custom = pb_msg(opts, GENPROTO_HttpRule_custom);
		httpMethod = pb_bytes(custom, GENPROTO_CustomHttpPattern_kind);
		pathTemplate = pb_bytes(custom, GENPROTO_CustomHttpPattern_path);
	} else {
		return (Str){NULL, 0};
	}

	Str err;
	GwHttpruleTemplate *parsed = gw_httprule_Parse(pathTemplate, &err);
	if (!parsed)
		return err;
	GwTemplate tmpl = gw_httprule_Compile(parsed);

	if (meth->ClientStreaming && tmpl.nFields > 0)
		return STR_LIT("cannot use path parameter in client streaming");

	GwBinding *b = xalloc(sizeof *b);
	b->Method = meth;
	b->Index = idx;
	b->PathTmpl = tmpl;
	b->HTTPMethod = httpMethod;

	b->PathParams = xalloc(sizeof *b->PathParams * (size_t)(tmpl.nFields + 1));
	for (int i = 0; i < tmpl.nFields; i++) {
		err = newParam(r, meth, tmpl.Fields[i], &b->PathParams[i]);
		if (err.p)
			return err;
		b->nPathParams++;
	}

	/* TODO(yugui) Handle query params */

	err = newBody(r, meth, body, &b->Body);
	if (err.p)
		return err;

	err = newResponse(r, meth, pb_bytes(opts, GENPROTO_HttpRule_response_body), &b->ResponseBody);
	if (err.p)
		return err;

	*out = b;
	return (Str){NULL, 0};
}

static Str applyOpts(GwRegistry *r, GwService *svc, GwMethod *meth, const PbMsg *opts)
{
	GwBinding *b;
	Str err = newBinding(r, meth, opts, meth->Bindings.n, &b);
	if (err.p)
		return err;

	if (b)
		vec_push(&meth->Bindings, b);
	const PbSlot *ab = pb_slot(opts, GENPROTO_HttpRule_additional_bindings);
	for (int i = 0; ab && i < ab->n; i++) {
		const PbMsg *additional = ab->v[i].m;
		if (pb_count(additional, GENPROTO_HttpRule_additional_bindings) > 0)
			return S(xcat("additional_binding in additional_binding not allowed: ", svc->Name, ".",
				      pb_str(meth->proto, MDP_NAME)));
		err = newBinding(r, meth, additional, meth->Bindings.n, &b);
		if (err.p)
			return err;
		if (b)
			vec_push(&meth->Bindings, b);
	}
	return (Str){NULL, 0};
}

static Str newMethod(GwRegistry *r, GwService *svc, PbMsg *md, const PbMsg **optsList, int nopts, GwMethod **out)
{
	const char *err = NULL;
	GwMessage *requestType = gw_Registry_LookupMsg(r, svc->File->package, pb_str(md, MDP_INPUT_TYPE), &err);
	if (!requestType)
		return S(err);
	GwMessage *responseType = gw_Registry_LookupMsg(r, svc->File->package, pb_str(md, MDP_OUTPUT_TYPE), &err);
	if (!responseType)
		return S(err);
	GwMethod *meth = xalloc(sizeof *meth);
	meth->proto = md;
	meth->Name = pb_str(md, MDP_NAME);
	meth->ClientStreaming = pb_int(md, MDP_CLIENT_STREAMING, 0) != 0;
	meth->ServerStreaming = pb_int(md, MDP_SERVER_STREAMING, 0) != 0;
	meth->Service = svc;
	meth->RequestType = requestType;
	meth->ResponseType = responseType;

	for (int i = 0; i < nopts; i++) {
		Str e = applyOpts(r, svc, meth, optsList[i]);
		if (e.p)
			return e;
	}
	*out = meth;
	return (Str){NULL, 0};
}

/* extractAPIOptions: the google.api.http option of the method, or NULL. */
static const PbMsg *extractAPIOptions(const PbMsg *meth)
{
	if (!pb_has(meth, MDP_OPTIONS))
		return NULL;
	bool present;
	Str b = genproto_ext_message(pb_msg(meth, MDP_OPTIONS), GENPROTO_E_HTTP, &present);
	if (!present)
		return NULL;
	PbMsg *m = pb_new(&genproto_http_rule()->pb);
	const char *err = gopb_decode(m, (const uint8_t *)b.p, b.n);
	if (err)
		fail("%s", err); /* checked when the request is parsed */
	return m;
}

/* defaultAPIOptions: an HttpRule that matches the gRPC mapping to HTTP/2 described in
 * https://github.com/grpc/grpc/blob/master/doc/PROTOCOL-HTTP2.md#requests, i.e.: method is
 * POST, path is "/<service name>/<method name>", body should contain the serialized request
 * message. */
static const PbMsg *defaultAPIOptions(GwService *svc, const PbMsg *md)
{
	/* FQSN prefixes the service's full name with a '.', e.g.: '.example.ExampleService' */
	const char *fqsn = gw_Service_FQSN(svc);
	if (fqsn[0] == '.')
		fqsn++;
	PbMsg *rule = pb_new(&genproto_http_rule()->pb);
	pb_set_str(rule, GENPROTO_HttpRule_post, xcat("/", fqsn, "/", pb_str(md, MDP_NAME)));
	pb_set_str(rule, GENPROTO_HttpRule_body, "*");
	return rule;
}

/* loadServices registers services and their methods from "targetFile" to "r". It must be called
 * after loadFile is called for all files so that loadServices can resolve names of message types
 * and their fields. */
Str gw_Registry_loadServices(GwRegistry *r, GwFile *file)
{
	Vec svcs = {0};
	for (int i = 0; i < pb_count(file->proto, FDP_SERVICE); i++) {
		PbMsg *sd = pb_msg_at(file->proto, FDP_SERVICE, i);
		GwService *svc = xalloc(sizeof *svc);
		svc->proto = sd;
		svc->Name = pb_str(sd, SDP_NAME);
		svc->File = file;
		svc->ForcePrefixedName = r->standalone;
		gw_Registry_IncludeAdditionalImports(r, svc, file->GoPkg);
		for (int k = 0; k < pb_count(sd, SDP_METHOD); k++) {
			PbMsg *md = pb_msg_at(sd, SDP_METHOD, k);
			const PbMsg *optsList[1];
			int nopts = 0;
			/* LookupExternalHTTPRules: none (grpc_api_configuration is not ported) */
			const PbMsg *opts = extractAPIOptions(md);
			if (opts)
				optsList[nopts++] = opts;
			if (nopts == 0 && r->generateUnboundMethods)
				optsList[nopts++] = defaultAPIOptions(svc, md);
			GwMethod *meth;
			Str err = newMethod(r, svc, md, optsList, nopts, &meth);
			if (err.p)
				return err;
			vec_push(&svc->Methods, meth);
			map_put(&r->meths, gw_Method_FQMN(meth), meth);
		}
		if (svc->Methods.n == 0)
			continue;
		vec_push(&svcs, svc);
	}
	file->Services = svcs;
	return (Str){NULL, 0};
}
