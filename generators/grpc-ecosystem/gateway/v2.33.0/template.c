// Copyright (c) 2015, Gengo, Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without modification,
// are permitted provided that the following conditions are met:
//
//     * Redistributions of source code must retain the above copyright notice,
//       this list of conditions and the following disclaimer.
//
//     * Redistributions in binary form must reproduce the above copyright notice,
//       this list of conditions and the following disclaimer in the documentation
//       and/or other materials provided with the distribution.
//
//     * Neither the name of Gengo, Inc. nor the names of its
//       contributors may be used to endorse or promote products derived from this
//       software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
// ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
// ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
// (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
// LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
// ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

/* Port of protoc-gen-grpc-gateway/internal/gengateway/template.go (grpc-gateway v2.33.0 with
 * bufbuild/plugins' separate_pkg_additional_imports.patch). The text/template templates are
 * generator code that writes the same text: every {{- / -}} trim is applied by hand, so the text
 * has the same lines (go/format.Source then gives the same file). A template error (a method of
 * the data that fails) aborts with the error text/template gives, with the position of the call
 * in the template source. Included by generator.c (unity build). */

typedef struct {
	GwFile *File;
	GwGoPackage *AliasedPkg;
	GwGoPackage *Imports;
	int nImports;
	bool UseRequestContext;
	Str RegisterFuncSuffix;
	bool AllowPatchFeature;
	bool OmitPackageDoc;
	bool UseOpaqueAPI;
	bool StrictDeepWildcard;
} param;

typedef struct {
	GwBinding *b;
	GwRegistry *Registry;
	bool AllowPatchFeature;
	bool UseOpaqueAPI;
} binding;

static void ws(Buf *w, const char *s) { buf_adds(w, s); }
static void wstr(Buf *w, Str s) { buf_add_str(w, s); }
#define W(...) wcat(w, __VA_ARGS__, (const char *)NULL)
static void wcat(Buf *w, ...)
{
	va_list ap;
	va_start(ap, w);
	for (const char *s; (s = va_arg(ap, const char *));)
		buf_adds(w, s);
	va_end(ap);
}

/* fmt %q of a string */
static const char *q(Str s) { return go_std_quote(s.p, s.n, NULL); }

/* A text/template ExecError of a method call that returned an error: "template: <name>:<line>:
 * <col>: executing "<name>" at <<node>>: error calling <method>: <err>". */
static NORETURN COLD void exec_error(const char *name, int line, int col, const char *node, const char *method, const char *err)
{
	fail_str(str_c(xfmt("template: %s:%d:%d: executing \"%s\" at <%s>: error calling %s: %s", name, line, col, name, node,
			     method, err)));
}

/* ---- binding methods ---- */

/* GetBodyFieldPath returns the binding body's field path. */
static const char *GetBodyFieldPath(binding b)
{
	if (b.b->Body && b.b->Body->FieldPath.n != 0)
		return gw_FieldPath_String(b.b->Body->FieldPath);
	return "*";
}

/* GetBodyFieldStructName returns the binding body's struct field name (the error case "no body
 * field found" is not reachable from the templates). */
static const char *GetBodyFieldStructName(binding b) { return gw_casing_Camel(GetBodyFieldPath(b)); }

/* GetBodyFieldType returns the Go type of the body field. */
static const char *GetBodyFieldType(binding b, const char **err)
{
	if (!b.b->Body || b.b->Body->FieldPath.n == 0) {
		*err = "no body field found";
		return NULL;
	}

	GwFieldPathComponent lastComponent = b.b->Body->FieldPath.v[b.b->Body->FieldPath.n - 1];
	int fieldType = lastComponent.Target->Type;

	/* Handle message types */
	if (fieldType == PT_MESSAGE) {
		/* Get the parent message to provide proper lookup context */
		GwMessage *parentMsg = lastComponent.Target->Message;
		const char *lerr = NULL;
		GwMessage *msg = gw_Registry_LookupMsg(b.Registry, gw_Message_FQMN(parentMsg), lastComponent.Target->TypeName, &lerr);
		if (!msg) {
			*err = xfmt("failed to lookup message type %s: %s", lastComponent.Target->TypeName, lerr);
			return NULL;
		}
		return gw_Message_GoType(msg, b.b->Method->Service->File->GoPkg.Path);
	}

	*err = "unsupported body field type";
	return NULL;
}

/* The distinct field names of a request message (the keys of HasQueryParam's map), computed once
 * per message: the map would otherwise be built for every binding. */
typedef struct {
	Map names;
	int n;
} fieldNames;

static Map fieldNamesCache; /* address of the message -> fieldNames; reset for every request */

static fieldNames *field_names(GwMessage *msg)
{
	const char *key = xfmt("%llx", (unsigned long long)(uintptr_t)msg);
	fieldNames *fn = map_get(&fieldNamesCache, key);
	if (fn)
		return fn;
	fn = xalloc(sizeof *fn);
	for (int i = 0; i < msg->Fields.n; i++) {
		const char *name = ((GwField *)msg->Fields.v[i])->Name;
		if (!map_has(&fn->names, name)) {
			map_put(&fn->names, name, fn);
			fn->n++;
		}
	}
	map_put(&fieldNamesCache, key, fn);
	return fn;
}

/* HasQueryParam determines if the binding needs parameters in query string. It sometimes returns
 * true even though actually the binding does not need. But it is not serious because it just
 * results in a small amount of extra codes generated. */
static bool HasQueryParam(binding b)
{
	if (b.b->Body && b.b->Body->FieldPath.n == 0)
		return false;
	fieldNames *fn = field_names(b.b->Method->RequestType);
	int n = fn->n;
	/* delete(fields, k) for the body path and the path parameters: each distinct key that is a
	 * field name removes one entry */
	Map deleted = {0};
	for (int i = -1; i < b.b->nPathParams; i++) {
		if (i < 0 && !b.b->Body)
			continue;
		const char *k = gw_FieldPath_String(i < 0 ? b.b->Body->FieldPath : b.b->PathParams[i].FieldPath);
		if (map_has(&fn->names, k) && !map_has(&deleted, k)) {
			map_put(&deleted, k, fn);
			n--;
		}
	}
	return n > 0;
}

/* QueryParamFilter, printed with queryParamFilter.String:
 * &utilities.DoubleArray{Encoding: map[string]int{"a": 0, ...}, Base: []int{...}, Check: ...} */
static void QueryParamFilter(Buf *w, binding b)
{
	int nseqs = (b.b->Body ? 1 : 0) + b.b->nPathParams;
	const char ***seqs = xalloc(sizeof *seqs * (size_t)(nseqs + 1));
	int *lens = xalloc(sizeof *lens * (size_t)(nseqs + 1));
	int k = 0;
	if (b.b->Body) {
		Vec parts = str_split(gw_FieldPath_String(b.b->Body->FieldPath), '.');
		seqs[k] = (const char **)parts.v;
		lens[k++] = parts.n;
	}
	for (int i = 0; i < b.b->nPathParams; i++) {
		Vec parts = str_split(gw_FieldPath_String(b.b->PathParams[i].FieldPath), '.');
		seqs[k] = (const char **)parts.v;
		lens[k++] = parts.n;
	}
	GwDoubleArray *da = gw_utilities_NewDoubleArray((const char *const *const *)seqs, lens, nseqs);
	ws(w, "&utilities.DoubleArray{Encoding: map[string]int{");
	for (int i = 0; i < da->nEncoding; i++) {
		if (i)
			ws(w, ", ");
		buf_fmt(w, "%s: %d", q(str_c(da->Encoding[i])), i);
	}
	ws(w, "}, Base: ");
	for (int a = 0; a < 2; a++) {
		const int *v = a == 0 ? da->Base : da->Check;
		if (da->nil) {
			ws(w, "[]int(nil)");
		} else {
			ws(w, "[]int{");
			for (int i = 0; i < da->n; i++)
				buf_fmt(w, i ? ", %d" : "%d", v[i]);
			ws(w, "}");
		}
		if (a == 0)
			ws(w, ", Check: ");
	}
	ws(w, "}");
}

/* hasEnumPathParam returns true if the path parameter slice contains a parameter that maps to an
 * enum proto field and that the enum proto field is or isn't repeated based on the provided
 * 'repeated' parameter. */
static bool hasEnumPathParam(binding b, bool repeated)
{
	for (int i = 0; i < b.b->nPathParams; i++) {
		GwParameter *p = &b.b->PathParams[i];
		if (gw_Parameter_IsEnum(p) && gw_Parameter_IsRepeated(p) == repeated && !gw_FieldPath_IsNestedProto3(p->FieldPath))
			return true;
	}
	return false;
}

/* LookupEnum looks up an enum type by path parameter. */
static GwEnum *LookupEnum(binding b, const GwParameter *p)
{
	const char *err;
	return gw_Registry_LookupEnum(b.Registry, "", p->Target->TypeName, &err);
}

/* FieldMaskField returns the golang-style name of the variable for a FieldMask, if there is
 * exactly one of that type in the message. Otherwise, it returns an empty string. */
static const char *FieldMaskField(binding b)
{
	GwField *fieldMaskField = NULL;
	Vec *fs = &b.b->Method->RequestType->Fields;
	for (int i = 0; i < fs->n; i++) {
		GwField *f = fs->v[i];
		if (strcmp(f->TypeName, ".google.protobuf.FieldMask") == 0) {
			/* if there is more than 1 FieldMask for this request, then return none */
			if (fieldMaskField)
				return "";
			fieldMaskField = f;
		}
	}
	if (fieldMaskField)
		return gw_casing_Camel(fieldMaskField->Name);
	return "";
}

/* ---- funcMap ---- */

/* toHTTPMethod */
static const char *toHTTPMethod(Str method)
{
	static const char *const m[][2] = {
		{"GET", "http.MethodGet"},       {"HEAD", "http.MethodHead"},     {"POST", "http.MethodPost"},
		{"PUT", "http.MethodPut"},       {"PATCH", "http.MethodPatch"},   {"DELETE", "http.MethodDelete"},
		{"CONNECT", "http.MethodConnect"}, {"OPTIONS", "http.MethodOptions"}, {"TRACE", "http.MethodTrace"},
	};
	for (size_t i = 0; i < sizeof m / sizeof *m; i++)
		if (str_eqc(method, m[i][0]))
			return m[i][1];
	/* Custom methods such as "LOCK" have no net/http constant. */
	return q(method);
}

/* embeddedFieldName: an embedded field is named after its type without the package qualifier,
 * e.g. *examplepb.Outer_Inner embeds a field Outer_Inner. */
static const char *embeddedFieldName(const char *goType)
{
	const char *dot = strrchr(goType, '.');
	return dot ? dot + 1 : goType;
}

/* ---- templates ---- */

/* The prefix S_M_I of the generated names: {{ .Method.Service.GetName }}_{{ .Method.GetName }}_{{ .Index }} */
static const char *smi(const GwBinding *b) { return xfmt("%s_%s_%d", b->Method->Service->Name, b->Method->Name, b->Index); }

/* headerTemplate */
static void headerTemplate(Buf *w, const param *p)
{
	W("\n// Code generated by protoc-gen-grpc-gateway. DO NOT EDIT.\n// source: ", p->File->name, "\n\n");
	if (!p->OmitPackageDoc) {
		W("/*\nPackage ", p->File->GoPkg.Name, " is a reverse proxy.\n\nIt translates gRPC into RESTful JSON APIs.\n");
		if (p->AliasedPkg)
			W("\nDeprecated: This package has moved to \"", p->AliasedPkg->Path, "\". Use that import path instead.");
		ws(w, "\n*/");
	} else if (p->AliasedPkg) {
		W("\n/*\nDeprecated: This package has moved to \"", p->AliasedPkg->Path, "\". Use that import path instead.\n*/");
	}
	W("\npackage ", p->File->GoPkg.Name);
	if (p->AliasedPkg) {
		W("\nimport ", gw_GoPackage_String(*p->AliasedPkg));
	} else {
		ws(w, "\nimport (\n\t");
		for (int i = 0; i < p->nImports; i++)
			if (gw_GoPackage_Standard(p->Imports[i]))
				W(gw_GoPackage_String(p->Imports[i]), "\n");
		ws(w, "\n\n\t");
		for (int i = 0; i < p->nImports; i++)
			if (!gw_GoPackage_Standard(p->Imports[i]))
				W(gw_GoPackage_String(p->Imports[i]), "\n");
		ws(w, "\n)\n\n// Suppress \"imported and not used\" errors\nvar (\n\t_ codes.Code\n\t_ io.Reader\n\t_ status.Status\n"
		      "\t_ = errors.New\n\t_ = runtime.String\n\t_ = utilities.NewDoubleArray\n\t_ = metadata.Join\n)");
	}
	ws(w, "\n");
}

/* request-func-signature (local: local-request-func-signature), one line */
static void request_func_signature(Buf *w, binding b, bool local)
{
	GwMethod *m = b.b->Method;
	const char *inst = gw_Service_InstanceName(m->Service);
	if (local) {
		W("func local_request_", smi(b.b), "(ctx context.Context, marshaler runtime.Marshaler, server ", inst,
		  "Server, req *http.Request, pathParams map[string]string) (proto.Message, runtime.ServerMetadata, error)");
		return;
	}
	W("func request_", smi(b.b), "(ctx context.Context, marshaler runtime.Marshaler, client ", inst,
	  "Client, req *http.Request, pathParams map[string]string) (");
	if (m->ServerStreaming)
		W(inst, "_", m->Name, "Client");
	else
		ws(w, "proto.Message");
	ws(w, ", runtime.ServerMetadata, error)");
}

/* client-streaming-request-func */
static void client_streaming_request_func(Buf *w, binding b)
{
	GwMethod *m = b.b->Method;
	ws(w, "\n");
	request_func_signature(w, b, false);
	W(" {\n\tvar metadata runtime.ServerMetadata\n\tstream, err := client.", m->Name,
	  "(ctx)\n\tif err != nil {\n\t\tgrpclog.Errorf(\"Failed to start streaming: %v\", err)\n\t\treturn nil, metadata, err\n\t}\n"
	  "\tdec := marshaler.NewDecoder(req.Body)\n\tfor {\n\t\tvar protoReq ",
	  gw_Message_GoType(m->RequestType, m->Service->File->GoPkg.Path),
	  "\n\t\terr = dec.Decode(&protoReq)\n\t\tif errors.Is(err, io.EOF) {\n\t\t\tbreak\n\t\t}\n\t\tif err != nil {\n"
	  "\t\t\tgrpclog.Errorf(\"Failed to decode request: %v\", err)\n"
	  "\t\t\treturn nil, metadata, status.Errorf(codes.InvalidArgument, \"%v\", err)\n\t\t}\n"
	  "\t\tif err = stream.Send(&protoReq); err != nil {\n\t\t\tif errors.Is(err, io.EOF) {\n\t\t\t\tbreak\n\t\t\t}\n"
	  "\t\t\tgrpclog.Errorf(\"Failed to send request: %v\", err)\n\t\t\treturn nil, metadata, err\n\t\t}\n\t}\n"
	  "\tif err := stream.CloseSend(); err != nil {\n\t\tgrpclog.Errorf(\"Failed to terminate client stream: %v\", err)\n"
	  "\t\treturn nil, metadata, err\n\t}\n\theader, err := stream.Header()\n\tif err != nil {\n"
	  "\t\tgrpclog.Errorf(\"Failed to get header from client: %v\", err)\n\t\treturn nil, metadata, err\n\t}\n"
	  "\tmetadata.HeaderMD = header");
	if (m->ServerStreaming)
		ws(w, "\n\treturn stream, metadata, nil");
	else
		ws(w, "\n\tmsg, err := stream.CloseAndRecv()\n\tmetadata.TrailerMD = stream.Trailer()\n\treturn msg, metadata, err");
	ws(w, "\n}\n");
}

/* bidi-streaming-request-func */
static void bidi_streaming_request_func(Buf *w, binding b)
{
	GwMethod *m = b.b->Method;
	ws(w, "\n");
	request_func_signature(w, b, false);
	W(" {\n\tvar metadata runtime.ServerMetadata\n\tstream, err := client.", m->Name,
	  "(ctx)\n\tif err != nil {\n\t\tgrpclog.Errorf(\"Failed to start streaming: %v\", err)\n\t\treturn nil, metadata, err\n\t}\n"
	  "\tdec := marshaler.NewDecoder(req.Body)\n\thandleSend := func() error {\n\t\tvar protoReq ",
	  gw_Message_GoType(m->RequestType, m->Service->File->GoPkg.Path),
	  "\n\t\terr := dec.Decode(&protoReq)\n\t\tif errors.Is(err, io.EOF) {\n\t\t\treturn err\n\t\t}\n\t\tif err != nil {\n"
	  "\t\t\tgrpclog.Errorf(\"Failed to decode request: %v\", err)\n"
	  "\t\t\treturn status.Errorf(codes.InvalidArgument, \"Failed to decode request: %v\", err)\n\t\t}\n"
	  "\t\tif err := stream.Send(&protoReq); err != nil {\n\t\t\tgrpclog.Errorf(\"Failed to send request: %v\", err)\n"
	  "\t\t\treturn err\n\t\t}\n\t\treturn nil\n\t}\n\tgo func() {\n\t\tfor {\n\t\t\tif err := handleSend(); err != nil {\n"
	  "\t\t\t\tbreak\n\t\t\t}\n\t\t}\n\t\tif err := stream.CloseSend(); err != nil {\n"
	  "\t\t\tgrpclog.Errorf(\"Failed to terminate client stream: %v\", err)\n\t\t}\n\t}()\n"
	  "\theader, err := stream.Header()\n\tif err != nil {\n\t\tgrpclog.Errorf(\"Failed to get header from client: %v\", err)\n"
	  "\t\treturn nil, metadata, err\n\t}\n\tmetadata.HeaderMD = header\n\treturn stream, metadata, nil\n}\n");
}

/* The body decoding of client-rpc-request-func (reader: "req.Body" or "newReader()"); the
 * template lines of its {{ .GetBodyFieldType }} for errors. */
static void decode_body(Buf *w, binding b, const char *reader, const char *tmplName, int typeLine)
{
	GwMethod *m = b.b->Method;
	const char *pkgPath = m->Service->File->GoPkg.Path;
	if (b.UseOpaqueAPI) {
		if (strcmp(GetBodyFieldPath(b), "*") == 0) {
			W("\n\tvar bodyData ", gw_Message_GoType(m->RequestType, pkgPath), "\n\tif err := marshaler.NewDecoder(", reader,
			  ").Decode(&bodyData); err != nil && !errors.Is(err, io.EOF) {\n"
			  "\t\treturn nil, metadata, status.Errorf(codes.InvalidArgument, \"%v\", err)\n\t}\n"
			  "\tproto.Merge(&protoReq, &bodyData)");
		} else {
			const char *err = NULL;
			const char *t = GetBodyFieldType(b, &err);
			if (!t)
				exec_error(tmplName, typeLine, 17, ".GetBodyFieldType", "GetBodyFieldType", err);
			W("\n\tbodyData := &", t, "{}\n\tif err := marshaler.NewDecoder(", reader,
			  ").Decode(bodyData); err != nil && !errors.Is(err, io.EOF) {\n"
			  "\t\treturn nil, metadata, status.Errorf(codes.InvalidArgument, \"%v\", err)\n\t}\n\tprotoReq.Set",
			  GetBodyFieldStructName(b), "(bodyData)");
		}
	} else {
		W("\n\tif err := marshaler.NewDecoder(", reader, ").Decode(&",
		  gw_FieldPath_AssignableExpr(b.b->Body->FieldPath, "protoReq", pkgPath),
		  "); err != nil && !errors.Is(err, io.EOF) {\n\t\treturn nil, metadata, status.Errorf(codes.InvalidArgument, \"%v\", err)\n\t}");
	}
}

/* client-rpc-request-func and local-client-rpc-request-func: the same text but for the filter
 * variable, the signature and the end. */
static void client_rpc_request_func(Buf *w, binding b, bool local)
{
	const char *tmplName = local ? "local-client-rpc-request-func" : "client-rpc-request-func";
	/* the template lines of the calls that can fail (the local template is 3 lines shorter) */
	int off = local ? -3 : 0;
	GwBinding *bb = b.b;
	GwMethod *m = bb->Method;
	const char *pkgPath = m->Service->File->GoPkg.Path;
	bool hasQueryParam = HasQueryParam(b);

	ws(w, "\n\n\n");
	if (!local) {
		if (hasQueryParam) {
			W("\nvar filter_", smi(bb), " = ");
			QueryParamFilter(w, b);
			ws(w, "\n");
		}
		ws(w, "\n");
	}
	request_func_signature(w, b, local);
	W(" {\n\tvar (\n\t\tprotoReq ", gw_Message_GoType(m->RequestType, pkgPath), "\n\t\tmetadata runtime.ServerMetadata");
	if (bb->nPathParams) {
		if (hasEnumPathParam(b, false))
			ws(w, "\n\t\te int32");
		if (hasEnumPathParam(b, true))
			ws(w, "\n\t\tes []int32");
		ws(w, "\n\t\terr error");
	}
	ws(w, "\n\t)");
	if (bb->Body) {
		bool isFieldMask = b.AllowPatchFeature && str_eqc(bb->HTTPMethod, "PATCH") && *FieldMaskField(b) &&
				   strcmp(GetBodyFieldPath(b), "*") != 0;
		if (isFieldMask)
			ws(w, "\n\tnewReader, berr := utilities.IOReaderFactory(req.Body)\n\tif berr != nil {\n"
			      "\t\treturn nil, metadata, status.Errorf(codes.InvalidArgument, \"%v\", berr)\n\t}");
		const char *protoReq = gw_FieldPath_AssignableExprPrep(bb->Body->FieldPath, "protoReq", pkgPath);
		if (*protoReq)
			W("\n\t", protoReq);
		if (!isFieldMask)
			decode_body(w, b, "req.Body", tmplName, 42 + off);
		if (isFieldMask) {
			decode_body(w, b, "newReader()", tmplName, 63 + off);
			const char *fmf = FieldMaskField(b);
			if (b.UseOpaqueAPI)
				W("\n\tif !protoReq.Has", fmf, "() || len(protoReq.Get", fmf, "().GetPaths()) == 0 {\n"
				  "\t\t\tif fieldMask, err := runtime.FieldMaskFromRequestBody(newReader(), protoReq.Get",
				  GetBodyFieldStructName(b), "()); err != nil {\n"
				  "\t\t\t\treturn nil, metadata, status.Errorf(codes.InvalidArgument, \"%v\", err)\n\t\t\t} else {\n"
				  "\t\t\t\tprotoReq.Set", fmf, "(fieldMask)\n\t\t\t}\n\t}");
			else
				W("\n\tif protoReq.", fmf, " == nil || len(protoReq.", fmf, ".GetPaths()) == 0 {\n"
				  "\t\t\tif fieldMask, err := runtime.FieldMaskFromRequestBody(newReader(), protoReq.",
				  GetBodyFieldStructName(b), "); err != nil {\n"
				  "\t\t\t\treturn nil, metadata, status.Errorf(codes.InvalidArgument, \"%v\", err)\n\t\t\t} else {\n"
				  "\t\t\t\tprotoReq.", fmf, " = fieldMask\n\t\t\t}\n\t}");
		}
	}
	for (int index = 0; index < bb->nPathParams; index++) {
		GwParameter *param = &bb->PathParams[index];
		GwEnum *e = LookupEnum(b, param);
		const char *qp = q(str_c(gw_FieldPath_String(param->FieldPath)));
		const char *sep = "";
		if (gw_Parameter_IsRepeated(param)) {
			int32_t r = gw_Registry_GetRepeatedPathParamSeparator(b.Registry);
			char c[2] = {(char)r, 0};
			sep = xcat(", ", q(str_c(c)));
		}
		W("\n\tval, ok ", index == 0 ? ":" : "", "= pathParams[", qp, "]\n\tif !ok {\n"
		  "\t\treturn nil, metadata, status.Errorf(codes.InvalidArgument, \"missing parameter %s\", ", qp, ")\n\t}");
		const char *mismatch = xcat("\n\tif err != nil {\n\t\treturn nil, metadata, status.Errorf(codes.InvalidArgument, "
					    "\"type mismatch, parameter: %s, error: %v\", ", qp, ", err)\n\t}");
		const char *err = NULL;
		if (gw_FieldPath_IsNestedProto3(param->FieldPath)) {
			W("\n\terr = runtime.PopulateFieldFromPath(&protoReq, ", qp, ", val)", mismatch);
		} else if (e) {
			const char *cf = gw_Parameter_ConvertFuncExpr(param, &err);
			if (!cf)
				exec_error(tmplName, 107 + off, 55, "$param.ConvertFuncExpr", "ConvertFuncExpr", err);
			W("\n\te", gw_Parameter_IsRepeated(param) ? "s" : "", ", err = ", cf, "(val", sep, ", ",
			  gw_casing_CamelIdentifier(gw_Enum_GoType(e, pkgPath)), "_value)", mismatch);
		} else {
			if (!b.UseOpaqueAPI) {
				const char *prep = gw_FieldPath_AssignableExprPrep(param->FieldPath, "protoReq", pkgPath);
				if (*prep)
					W("\n\t", prep);
			}
			if (b.UseOpaqueAPI) {
				const char *cf = gw_Parameter_ConvertFuncExpr(param, &err);
				if (!cf)
					exec_error(tmplName, 119 + off, 75, "$param.ConvertFuncExpr", "ConvertFuncExpr", err);
				const char *conv = gw_casing_CamelIdentifier(gw_FieldPath_String(param->FieldPath));
				W("\n\tconverted", conv, ", err := ", cf, "(val", sep, ")", mismatch, "\n\t",
				  gw_FieldPath_OpaqueSetterExpr(param->FieldPath, "protoReq"), "(converted", conv, ")");
			} else {
				const char *ae = gw_FieldPath_AssignableExpr(param->FieldPath, "protoReq", pkgPath);
				const char *cf = gw_Parameter_ConvertFuncExpr(param, &err);
				if (!cf)
					exec_error(tmplName, 125 + off, 96, "$param.ConvertFuncExpr", "ConvertFuncExpr", err);
				W("\n\t", ae, ", err = ", cf, "(val", sep, ")", mismatch);
			}
		}
		if (!gw_FieldPath_IsNestedProto3(param->FieldPath)) {
			if (e && gw_Parameter_IsRepeated(param)) {
				const char *et = gw_Enum_GoType(e, pkgPath);
				W("\n\ts := make([]", et, ", len(es))\n\tfor i, v := range es {\n\t\ts[i] = ", et, "(v)\n\t}");
				if (b.UseOpaqueAPI)
					W("\n\t", gw_FieldPath_OpaqueSetterExpr(param->FieldPath, "protoReq"), "(s)");
				else
					W("\n\t", gw_FieldPath_AssignableExpr(param->FieldPath, "protoReq", pkgPath), " = s");
			} else if (e) {
				const char *et = gw_casing_CamelIdentifier(gw_Enum_GoType(e, pkgPath));
				if (b.UseOpaqueAPI)
					W("\n\t", gw_FieldPath_OpaqueSetterExpr(param->FieldPath, "protoReq"), "(", et, "(e))");
				else
					W("\n\t", gw_FieldPath_AssignableExpr(param->FieldPath, "protoReq", pkgPath), " = ", et, "(e)");
			}
		}
	}
	if (hasQueryParam)
		W("\n\tif err := req.ParseForm(); err != nil {\n\t\treturn nil, metadata, status.Errorf(codes.InvalidArgument, \"%v\", err)\n"
		  "\t}\n\tif err := runtime.PopulateQueryParameters(&protoReq, req.Form, filter_", smi(bb),
		  "); err != nil {\n\t\treturn nil, metadata, status.Errorf(codes.InvalidArgument, \"%v\", err)\n\t}");
	if (local) {
		if (m->ServerStreaming)
			ws(w, "\n\t// TODO");
		else
			W("\n\tmsg, err := server.", m->Name, "(ctx, &protoReq)\n\treturn msg, metadata, err");
		ws(w, "\n}");
		return;
	}
	ws(w, "\n\tif req.Body != nil {\n\t\t_, _ = io.Copy(io.Discard, req.Body)\n\t}");
	if (m->ServerStreaming)
		W("\n\tstream, err := client.", m->Name, "(ctx, &protoReq)\n\tif err != nil {\n\t\treturn nil, metadata, err\n\t}\n"
		  "\theader, err := stream.Header()\n\tif err != nil {\n\t\treturn nil, metadata, err\n\t}\n"
		  "\tmetadata.HeaderMD = header\n\treturn stream, metadata, nil");
	else
		W("\n\tmsg, err := client.", m->Name,
		  "(ctx, &protoReq, grpc.Header(&metadata.HeaderMD), grpc.Trailer(&metadata.TrailerMD))\n\treturn msg, metadata, err");
	ws(w, "\n}");
}

/* handlerTemplate */
static void handlerTemplate(Buf *w, binding b)
{
	GwMethod *m = b.b->Method;
	ws(w, "\n\n");
	if (m->ClientStreaming && m->ServerStreaming)
		bidi_streaming_request_func(w, b);
	else if (m->ClientStreaming)
		client_streaming_request_func(w, b);
	else
		client_rpc_request_func(w, b, false);
	ws(w, "\n\n");
}

/* localHandlerTemplate */
static void localHandlerTemplate(Buf *w, binding b)
{
	GwMethod *m = b.b->Method;
	ws(w, "\n\n");
	if (!m->ClientStreaming && !m->ServerStreaming) {
		client_rpc_request_func(w, b, true);
		ws(w, "\n");
	}
	ws(w, "\n");
}

typedef struct {
	GwGoPackage *AliasedPkg;
	GwService **Services;
	int nServices;
	bool UseRequestContext;
	Str RegisterFuncSuffix;
	bool UseOpaqueAPI;
	bool StrictDeepWildcard;
} trailerParams;

/* "Register" + svc + suffix + what */
static void reg(Buf *w, const trailerParams *p, const GwService *svc, const char *what)
{
	W("Register", svc->Name);
	wstr(w, p->RegisterFuncSuffix);
	ws(w, what);
}

/* "\n\tRegister<S><Suffix><what> = <alias>.Register<S><Suffix><what>" */
static void alias_line(Buf *w, const trailerParams *p, const GwService *svc, const char *what)
{
	ws(w, "\n\t");
	reg(w, p, svc, what);
	W(" = ", p->AliasedPkg->Alias, ".");
	reg(w, p, svc, what);
}

/* The context lines of a handler (ctx := ...) */
static void handler_ctx(Buf *w, const trailerParams *p)
{
	if (p->UseRequestContext)
		ws(w, "\n\t\tctx, cancel := context.WithCancel(req.Context())");
	else
		ws(w, "ctx, cancel := context.WithCancel(ctx)");
}

/* localTrailerTemplate */
static void localTrailerTemplate(Buf *w, const trailerParams *p)
{
	if (p->AliasedPkg)
		ws(w, "\nvar (");
	ws(w, "\n\n");
	for (int s = 0; s < p->nServices; s++) {
		GwService *svc = p->Services[s];
		ws(w, "\n// ");
		reg(w, p, svc, "Server");
		W(" registers the http handlers for service ", svc->Name, " to \"mux\".\n// UnaryRPC     :call ", svc->Name,
		  "Server directly.\n// StreamingRPC :currently unsupported pending https://github.com/grpc/grpc-go/issues/906.\n"
		  "// Note that using this registration option will cause many gRPC library features to stop working. Consider using ");
		reg(w, p, svc, "FromEndpoint");
		ws(w, " instead.\n// GRPC interceptors will not work for this type of registration. To use interceptors, you must use "
		      "the \"runtime.WithMiddlewares\" option in the \"runtime.NewServeMux\" call.");
		if (p->AliasedPkg) {
			alias_line(w, p, svc, "Server");
		} else {
			ws(w, "\nfunc ");
			reg(w, p, svc, "Server");
			W("(ctx context.Context, mux *runtime.ServeMux, server ", gw_Service_InstanceName(svc), "Server) error {");
			for (int i = 0; i < svc->Methods.n; i++) {
				GwMethod *m = svc->Methods.v[i];
				for (int k = 0; k < m->Bindings.n; k++) {
					GwBinding *b = m->Bindings.v[k];
					const char *n = smi(b);
					if (m->ClientStreaming || m->ServerStreaming) {
						W("\n\tmux.Handle(", toHTTPMethod(b->HTTPMethod), ", pattern_", n,
						  ", func(w http.ResponseWriter, req *http.Request, pathParams map[string]string) {\n"
						  "\t\terr := status.Error(codes.Unimplemented, \"streaming calls are not yet supported in the "
						  "in-process transport\")\n\t\t_, outboundMarshaler := runtime.MarshalerForRequest(mux, req)\n"
						  "\t\truntime.HTTPError(ctx, mux, outboundMarshaler, w, req, err)\n\t\treturn\n\t})");
					} else {
						W("mux.Handle(", toHTTPMethod(b->HTTPMethod), ", pattern_", n,
						  ", func(w http.ResponseWriter, req *http.Request, pathParams map[string]string) {");
						handler_ctx(w, p);
						W("\n\t\tdefer cancel()\n\t\tvar stream runtime.ServerTransportStream\n"
						  "\t\tctx = grpc.NewContextWithServerTransportStream(ctx, &stream)\n"
						  "\t\tinboundMarshaler, outboundMarshaler := runtime.MarshalerForRequest(mux, req)\n"
						  "\t\tannotatedContext, err := runtime.AnnotateIncomingContext(ctx, mux, req, \"/",
						  svc->File->package, ".", svc->Name, "/", m->Name, "\", runtime.WithHTTPPathPattern(\"");
						wstr(w, b->PathTmpl.Template);
						W("\"))\n\t\tif err != nil {\n\t\t\truntime.HTTPError(ctx, mux, outboundMarshaler, w, req, err)\n"
						  "\t\t\treturn\n\t\t}\n\t\tresp, md, err := local_request_", n,
						  "(annotatedContext, inboundMarshaler, server, req, pathParams)\n"
						  "\t\tmd.HeaderMD, md.TrailerMD = metadata.Join(md.HeaderMD, stream.Header()), "
						  "metadata.Join(md.TrailerMD, stream.Trailer())\n"
						  "\t\tannotatedContext = runtime.NewServerMetadataContext(annotatedContext, md)\n"
						  "\t\tif err != nil {\n\t\t\truntime.HTTPError(annotatedContext, mux, outboundMarshaler, w, req, err)\n"
						  "\t\t\treturn\n\t\t}");
						if (b->ResponseBody)
							W("\n\t\tforward_", n, "(annotatedContext, mux, outboundMarshaler, w, req, response_", n,
							  "{resp.(*", gw_Message_GoType(m->ResponseType, m->Service->File->GoPkg.Path),
							  ")}, mux.GetForwardResponseOptions()...)");
						else
							W("\n\t\tforward_", n,
							  "(annotatedContext, mux, outboundMarshaler, w, req, resp, mux.GetForwardResponseOptions()...)");
						ws(w, "\n\t})");
					}
					ws(w, "\n\t");
				}
			}
			ws(w, "\n\treturn nil\n}\n");
		}
		ws(w, "\n");
	}
}

/* trailerTemplate */
static void trailerTemplate(Buf *w, const trailerParams *p)
{
	ws(w, "\n\n\n");
	for (int s = 0; s < p->nServices; s++) {
		GwService *svc = p->Services[s];
		const char *inst = gw_Service_InstanceName(svc);
		ws(w, "\n// ");
		reg(w, p, svc, "FromEndpoint");
		ws(w, " is same as ");
		reg(w, p, svc, "");
		ws(w, " but\n// automatically dials to \"endpoint\" and closes the connection when \"ctx\" gets done.");
		if (p->AliasedPkg) {
			alias_line(w, p, svc, "FromEndpoint");
		} else {
			ws(w, "\nfunc ");
			reg(w, p, svc, "FromEndpoint");
			ws(w, "(ctx context.Context, mux *runtime.ServeMux, endpoint string, opts []grpc.DialOption) (err error) {\n"
			      "\tconn, err := grpc.NewClient(endpoint, opts...)\n\tif err != nil {\n\t\treturn err\n\t}\n"
			      "\tdefer func() {\n\t\tif err != nil {\n\t\t\tif cerr := conn.Close(); cerr != nil {\n"
			      "\t\t\t\tgrpclog.Errorf(\"Failed to close conn to %s: %v\", endpoint, cerr)\n\t\t\t}\n\t\t\treturn\n\t\t}\n"
			      "\t\tgo func() {\n\t\t\t<-ctx.Done()\n\t\t\tif cerr := conn.Close(); cerr != nil {\n"
			      "\t\t\t\tgrpclog.Errorf(\"Failed to close conn to %s: %v\", endpoint, cerr)\n\t\t\t}\n\t\t}()\n\t}()\n\treturn ");
			reg(w, p, svc, "");
			ws(w, "(ctx, mux, conn)\n}");
		}
		ws(w, "\n\n// ");
		reg(w, p, svc, "");
		W(" registers the http handlers for service ", svc->Name,
		  " to \"mux\".\n// The handlers forward requests to the grpc endpoint over \"conn\".");
		if (p->AliasedPkg) {
			alias_line(w, p, svc, "");
		} else {
			ws(w, "\nfunc ");
			reg(w, p, svc, "");
			ws(w, "(ctx context.Context, mux *runtime.ServeMux, conn *grpc.ClientConn) error {\n\treturn ");
			reg(w, p, svc, "Client");
			W("(ctx, mux, ", gw_Service_ClientConstructorName(svc), "(conn))\n}");
		}
		ws(w, "\n\n// ");
		reg(w, p, svc, "Client");
		W(" registers the http handlers for service ", svc->Name,
		  "\n// to \"mux\". The handlers forward requests to the grpc endpoint over the given implementation of \"", inst,
		  "Client\".\n// Note: the gRPC framework executes interceptors within the gRPC handler. If the passed in \"", inst,
		  "Client\"\n// doesn't go through the normal gRPC flow (creating a gRPC client etc.) then it will be up to the passed "
		  "in\n// \"",
		  inst, "Client\" to call the correct interceptors. This client ignores the HTTP middlewares.");
		if (p->AliasedPkg) {
			alias_line(w, p, svc, "Client");
			ws(w, "\n");
			continue;
		}
		ws(w, "\nfunc ");
		reg(w, p, svc, "Client");
		W("(ctx context.Context, mux *runtime.ServeMux, client ", inst, "Client) error {");
		for (int i = 0; i < svc->Methods.n; i++) {
			GwMethod *m = svc->Methods.v[i];
			for (int k = 0; k < m->Bindings.n; k++) {
				GwBinding *b = m->Bindings.v[k];
				const char *n = smi(b);
				W("\n\tmux.Handle(", toHTTPMethod(b->HTTPMethod), ", pattern_", n,
				  ", func(w http.ResponseWriter, req *http.Request, pathParams map[string]string) {");
				handler_ctx(w, p);
				W("\n\t\tdefer cancel()\n\t\tinboundMarshaler, outboundMarshaler := runtime.MarshalerForRequest(mux, req)\n"
				  "\t\tannotatedContext, err := runtime.AnnotateContext(ctx, mux, req, \"/",
				  svc->File->package, ".", svc->Name, "/", m->Name, "\", runtime.WithHTTPPathPattern(\"");
				wstr(w, b->PathTmpl.Template);
				W("\"))\n\t\tif err != nil {\n\t\t\truntime.HTTPError(ctx, mux, outboundMarshaler, w, req, err)\n\t\t\treturn\n\t\t}\n"
				  "\t\tresp, md, err := request_", n, "(annotatedContext, inboundMarshaler, client, req, pathParams)\n"
				  "\t\tannotatedContext = runtime.NewServerMetadataContext(annotatedContext, md)\n"
				  "\t\tif err != nil {\n\t\t\truntime.HTTPError(annotatedContext, mux, outboundMarshaler, w, req, err)\n"
				  "\t\t\treturn\n\t\t}");
				if (m->ServerStreaming) {
					if (b->ResponseBody)
						W("\n\t\tforward_", n,
						  "(annotatedContext, mux, outboundMarshaler, w, req, func() (proto.Message, error) {\n"
						  "\t\t\tres, err := resp.Recv()\n\t\t\treturn response_", n,
						  "{res}, err\n\t\t}, mux.GetForwardResponseOptions()...)");
					else
						W("\n\t\tforward_", n,
						  "(annotatedContext, mux, outboundMarshaler, w, req, func() (proto.Message, error) { return "
						  "resp.Recv() }, mux.GetForwardResponseOptions()...)");
				} else {
					if (b->ResponseBody)
						W("\n\t\tforward_", n, "(annotatedContext, mux, outboundMarshaler, w, req, response_", n, "{resp.(*",
						  gw_Message_GoType(m->ResponseType, m->Service->File->GoPkg.Path),
						  ")}, mux.GetForwardResponseOptions()...)");
					else
						W("\n\t\tforward_", n,
						  "(annotatedContext, mux, outboundMarshaler, w, req, resp, mux.GetForwardResponseOptions()...)");
				}
				ws(w, "\n\t})");
			}
		}
		ws(w, "\n\treturn nil\n}\n\n");
		for (int i = 0; i < svc->Methods.n; i++) {
			GwMethod *m = svc->Methods.v[i];
			ws(w, "\n");
			for (int k = 0; k < m->Bindings.n; k++) {
				GwBinding *b = m->Bindings.v[k];
				ws(w, "\n");
				if (b->ResponseBody) {
					const char *n = smi(b);
					const char *rt = gw_Message_GoType(m->ResponseType, m->Service->File->GoPkg.Path);
					W("\ntype response_", n, " struct {\n\t*", rt, "\n}\n\nfunc (m response_", n,
					  ") XXX_ResponseBody() interface{} {\n\tresponse := m.", embeddedFieldName(rt));
					const char *fp = gw_FieldPath_String(b->ResponseBody->FieldPath);
					if (p->UseOpaqueAPI) {
						if (strcmp(fp, "*") == 0)
							ws(w, "\n\treturn response");
						else
							W("\n\treturn response.Get", gw_casing_CamelIdentifier(fp), "()");
					} else {
						W("\n\treturn ", gw_FieldPath_AssignableExpr(b->ResponseBody->FieldPath, "response",
											m->Service->File->GoPkg.Path));
					}
					ws(w, "\n}\n");
				}
				ws(w, "\n");
			}
			ws(w, "\n");
		}
		ws(w, "\n\nvar (");
		for (int i = 0; i < svc->Methods.n; i++) {
			GwMethod *m = svc->Methods.v[i];
			for (int k = 0; k < m->Bindings.n; k++) {
				GwBinding *b = m->Bindings.v[k];
				GwTemplate *t = &b->PathTmpl;
				W("\n\tpattern_", smi(b), " = runtime.MustPattern(runtime.NewPattern",
				  p->StrictDeepWildcard ? "WithOptions" : "");
				buf_fmt(w, "(%d, []int{", t->Version);
				for (int j = 0; j < t->nOpCodes; j++)
					buf_fmt(w, j ? ", %d" : "%d", t->OpCodes[j]);
				ws(w, "}, ");
				if (t->nPool == 0) {
					ws(w, "[]string(nil)");
				} else {
					ws(w, "[]string{");
					for (int j = 0; j < t->nPool; j++)
						W(j ? ", " : "", q(t->Pool[j]));
					ws(w, "}");
				}
				W(", ", q(t->Verb), p->StrictDeepWildcard ? ", runtime.WithStrictDeepWildcard()" : "", "))");
			}
		}
		ws(w, "\n)\n\nvar (");
		for (int i = 0; i < svc->Methods.n; i++) {
			GwMethod *m = svc->Methods.v[i];
			for (int k = 0; k < m->Bindings.n; k++)
				W("\n\tforward_", smi(m->Bindings.v[k]), " = ",
				  m->ServerStreaming ? "runtime.ForwardResponseStream" : "runtime.ForwardResponseMessage");
		}
		ws(w, "\n)\n\n");
	}
	if (p->AliasedPkg)
		ws(w, "\n)");
}

/* applyTemplate: the code of the file, or NULL and *noTarget when the file has no service with an
 * HTTP binding (errNoTargetService). Other errors fail(). */
static const char *applyTemplate(param p, GwRegistry *reg, bool *noTarget)
{
	Buf wb = {0};
	Buf *w = &wb;
	*noTarget = false;
	headerTemplate(w, &p);
	GwService **targetServices = xalloc(sizeof *targetServices * (size_t)(p.File->Services.n + 1));
	int nTarget = 0;

	for (int i = 0; i < p.File->Messages.n; i++) {
		GwMessage *msg = p.File->Messages.v[i];
		msg->Name = gw_casing_Camel(msg->Name);
	}

	for (int s = 0; s < p.File->Services.n; s++) {
		GwService *svc = p.File->Services.v[s];
		bool methodWithBindingsSeen = false;
		svc->Name = gw_casing_Camel(svc->Name);

		for (int i = 0; i < svc->Methods.n; i++) {
			GwMethod *meth = svc->Methods.v[i];
			meth->Name = gw_casing_Camel(meth->Name);
			for (int k = 0; k < meth->Bindings.n; k++) {
				GwBinding *b = meth->Bindings.v[k];
				methodWithBindingsSeen = true;
				if (p.AliasedPkg)
					break;
				Str err = gw_Registry_CheckDuplicateAnnotation(reg, b->HTTPMethod, b->PathTmpl.Template, svc);
				if (err.p)
					fail_str(err);

				binding bd = {b, reg, p.AllowPatchFeature, p.UseOpaqueAPI};
				handlerTemplate(w, bd);
				/* Local */
				localHandlerTemplate(w, bd);
			}
		}
		if (methodWithBindingsSeen)
			targetServices[nTarget++] = svc;
	}
	if (nTarget == 0) {
		*noTarget = true;
		return NULL;
	}

	trailerParams tp = {p.AliasedPkg, targetServices, nTarget, p.UseRequestContext, p.RegisterFuncSuffix, p.UseOpaqueAPI,
			    p.StrictDeepWildcard};
	/* Local */
	localTrailerTemplate(w, &tp);
	trailerTemplate(w, &tp);
	return buf_str(w);
}
