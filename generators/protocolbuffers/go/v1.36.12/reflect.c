/* Copyright 2018 The Go Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file. */

/* Ported from cmd/protoc-gen-go/internal_gengo/reflect.go (protobuf-go v1.36.12). */

static COLD const char *file_var_name(const PgFile *f, const char *suffix)
{
	/* the first rune lowercased: GoDescriptorIdent is "File_..." */
	const char *prefix = f->GoDescriptorIdent.GoName;
	char first = prefix[0] >= 'A' && prefix[0] <= 'Z' ? (char)(prefix[0] + 32) : prefix[0];
	char head[2] = {first, 0};
	return xcat(head, prefix[0] ? prefix + 1 : "", "_", suffix);
}
static const char *raw_desc_var_name(const FileInfo *f) { return file_var_name(f->f, "rawDesc"); }
static const char *go_types_var_name(const FileInfo *f) { return file_var_name(f->f, "goTypes"); }
static const char *dep_idxs_var_name(const FileInfo *f) { return file_var_name(f->f, "depIdxs"); }
static const char *enum_types_var_name(const FileInfo *f) { return file_var_name(f->f, "enumTypes"); }
static const char *message_types_var_name(const FileInfo *f) { return file_var_name(f->f, "msgTypes"); }
static const char *extension_types_var_name(const FileInfo *f) { return file_var_name(f->f, "extTypes"); }
static const char *init_func_name(const PgFile *f) { return file_var_name(f, "init"); }

/* unexportIdent turns id into its unexported version (by lower-casing), but only if is_opaque
 * is set. Generated names start with an ASCII letter. */
static COLD PgGoIdent unexport_ident(PgGoIdent id, bool is_opaque)
{
	if (!is_opaque)
		return id;
	char *s = xstrdup(id.GoName);
	if (s[0] >= 'A' && s[0] <= 'Z')
		s[0] = (char)(s[0] + 32);
	id.GoName = s;
	return id;
}

typedef struct {
	PgGeneratedFile *g;
	Vec goTypes, depIdxs; /* lines */
	Map seen;             /* full name -> index + 1 */
	int nseen;
} DepState;

static COLD void gen_dep(DepState *st, const char *name, const char *dep_source)
{
	if (*dep_source) {
		int idx = (int)(intptr_t)map_get(&st->seen, name) - 1;
		vec_push(&st->depIdxs, xcat(go_itoa(idx), ", // ", go_itoa(st->depIdxs.n), ": ", dep_source, " -> ", name));
	}
}

static COLD void dep_enum(DepState *st, const PgEnum *e, const char *dep_source)
{
	if (!e)
		return;
	const char *name = e->Desc->d.full_name;
	if (!map_get(&st->seen, name)) {
		vec_push(&st->goTypes, xcat("(", pg_qualified_go_ident(st->g, e->GoIdent), ")(0), // ", go_itoa(st->goTypes.n), ": ", name));
		map_put(&st->seen, name, (void *)(intptr_t)(++st->nseen));
	}
	gen_dep(st, name, dep_source);
}

static COLD void dep_message(DepState *st, const PgMessage *m, const char *dep_source)
{
	if (!m)
		return;
	const char *name = m->Desc->d.full_name;
	if (!map_get(&st->seen, name)) {
		const char *line;
		if (m->Desc->is_map_entry) /* Map entry messages have no associated Go type. */
			line = xcat("nil, // ", go_itoa(st->goTypes.n), ": ", name);
		else
			line = xcat("(*", pg_qualified_go_ident(st->g, m->GoIdent), ")(nil), // ", go_itoa(st->goTypes.n), ": ", name);
		vec_push(&st->goTypes, (void *)line);
		map_put(&st->seen, name, (void *)(intptr_t)(++st->nseen));
	}
	gen_dep(st, name, dep_source);
}

static void gen_file_descriptor(PgPlugin *gen, PgGeneratedFile *g, FileInfo *f);
static const char *unsafe_bytes_raw_desc(PgGeneratedFile *g, FileInfo *f);

static COLD void gen_reflect_file_descriptor(PgPlugin *gen, PgGeneratedFile *g, FileInfo *f)
{
	P(g, "var ", f->f->GoDescriptorIdent, " ", ID(protoreflectPackage, "FileDescriptor"));
	P(g, "");

	gen_file_descriptor(gen, g, f);
	if (f->nAllEnums > 0)
		P(g, "var ", enum_types_var_name(f), " = make([]", ID(protoimplPackage, "EnumInfo"), ",", f->nAllEnums, ")");
	if (f->nAllMessages > 0)
		P(g, "var ", message_types_var_name(f), " = make([]", ID(protoimplPackage, "MessageInfo"), ",", f->nAllMessages, ")");

	/* Generate a unique list of Go types for all declarations and dependencies, and the
	 * associated index into the type list for all dependencies. This ordering is significant
	 * (filetype.TypeBuilder.DependencyIndexes). */
	DepState st = {0};
	st.g = g;
	struct {
		int start;
		const char *name;
	} offsets[8];
	int no = 0;
	for (int i = 0; i < f->nAllEnums; i++)
		dep_enum(&st, f->allEnums[i]->e, "");
	for (int i = 0; i < f->nAllMessages; i++)
		dep_message(&st, f->allMessages[i]->m, "");
	offsets[no].start = st.depIdxs.n, offsets[no++].name = "field type_name";
	for (int i = 0; i < f->nAllMessages; i++) {
		PgMessage *message = f->allMessages[i]->m;
		for (int k = 0; k < message->nFields; k++) {
			PgField *field = message->Fields[k];
			const char *source = xcat(field->Desc->d.full_name, ":type_name");
			dep_enum(&st, field->Enum, source);
			dep_message(&st, field->Message, source);
		}
	}
	offsets[no].start = st.depIdxs.n, offsets[no++].name = "extension extendee";
	for (int i = 0; i < f->nAllExtensions; i++) {
		PgExtension *x = f->allExtensions[i];
		dep_message(&st, x->Extendee, xcat(x->Desc->d.full_name, ":extendee"));
	}
	offsets[no].start = st.depIdxs.n, offsets[no++].name = "extension type_name";
	for (int i = 0; i < f->nAllExtensions; i++) {
		PgExtension *x = f->allExtensions[i];
		const char *source = xcat(x->Desc->d.full_name, ":type_name");
		dep_enum(&st, x->Enum, source);
		dep_message(&st, x->Message, source);
	}
	offsets[no].start = st.depIdxs.n, offsets[no++].name = "method input_type";
	for (int i = 0; i < f->f->nServices; i++)
		for (int k = 0; k < f->f->Services[i]->nMethods; k++) {
			PgMethod *method = f->f->Services[i]->Methods[k];
			dep_message(&st, method->Input, xcat(method->Desc->d.full_name, ":input_type"));
		}
	offsets[no].start = st.depIdxs.n, offsets[no++].name = "method output_type";
	for (int i = 0; i < f->f->nServices; i++)
		for (int k = 0; k < f->f->Services[i]->nMethods; k++) {
			PgMethod *method = f->f->Services[i]->Methods[k];
			dep_message(&st, method->Output, xcat(method->Desc->d.full_name, ":output_type"));
		}
	offsets[no].start = st.depIdxs.n, offsets[no++].name = "";
	for (int i = no - 2; i >= 0; i--) {
		int curr = offsets[i].start, next = offsets[i + 1].start;
		vec_push(&st.depIdxs, xcat(go_itoa(curr), ", // [", go_itoa(curr), ":", go_itoa(next), "] is the sub-list for ", offsets[i].name));
	}

	P(g, "var ", go_types_var_name(f), " = []any{");
	for (int i = 0; i < st.goTypes.n; i++)
		P(g, (const char *)st.goTypes.v[i]);
	P(g, "}");

	P(g, "var ", dep_idxs_var_name(f), " = []int32{");
	for (int i = 0; i < st.depIdxs.n; i++)
		P(g, (const char *)st.depIdxs.v[i]);
	P(g, "}");

	P(g, "func init() { ", init_func_name(f->f), "() }");

	P(g, "func ", init_func_name(f->f), "() {");
	P(g, "if ", f->f->GoDescriptorIdent, " != nil {");
	P(g, "return");
	P(g, "}");

	/* Ensure that initialization functions for different files in the same Go package run in
	 * the correct order: call the init funcs for every .proto file imported by this one that
	 * is in the same Go package. */
	for (int i = 0; i < f->f->Desc->n_imports; i++) {
		PgFile *imp_file = map_get(&gen->FilesByPath, f->f->Desc->imports[i].file->d.name);
		if (!str_eq(imp_file->GoImportPath, f->f->GoImportPath))
			continue;
		P(g, init_func_name(imp_file), "()");
	}

	if (f->nAllMessages > 0) {
		/* Populate MessageInfo.OneofWrappers. */
		for (int i = 0; i < f->nAllMessages; i++) {
			MessageInfo *message = f->allMessages[i];
			if (message->m->nOneofs == 0)
				continue;
			P(g, message_types_var_name(f), "[", message->index, "].OneofWrappers = []any {");
			for (int k = 0; k < message->m->nOneofs; k++) {
				PgOneof *oneof = message->m->Oneofs[k];
				if (go_oneof_is_synthetic(oneof->Desc))
					continue;
				for (int j = 0; j < oneof->nFields; j++)
					P(g, "(*", unexport_ident(oneof->Fields[j]->GoIdent, is_opaque(message)), ")(nil),");
			}
			P(g, "}");
		}
	}

	P(g, "type x struct{}");
	P(g, "out := ", ID(protoimplPackage, "TypeBuilder"), "{");
	P(g, "File: ", ID(protoimplPackage, "DescBuilder"), "{");
	P(g, "GoPackagePath: ", ID(reflectPackage, "TypeOf"), "(x{}).PkgPath(),");
	/* Avoid a copy of the descriptor. */
	P(g, "RawDescriptor: ", unsafe_bytes_raw_desc(g, f), ",");
	P(g, "NumEnums: ", f->nAllEnums, ",");
	P(g, "NumMessages: ", f->nAllMessages, ",");
	P(g, "NumExtensions: ", f->nAllExtensions, ",");
	P(g, "NumServices: ", f->f->nServices, ",");
	P(g, "},");
	P(g, "GoTypes: ", go_types_var_name(f), ",");
	P(g, "DependencyIndexes: ", dep_idxs_var_name(f), ",");
	if (f->nAllEnums > 0)
		P(g, "EnumInfos: ", enum_types_var_name(f), ",");
	if (f->nAllMessages > 0)
		P(g, "MessageInfos: ", message_types_var_name(f), ",");
	if (f->nAllExtensions > 0)
		P(g, "ExtensionInfos: ", extension_types_var_name(f), ",");
	P(g, "}.Build()");
	P(g, f->f->GoDescriptorIdent, " = out.File");

	/* Set inputs to nil to allow GC to reclaim resources. */
	P(g, go_types_var_name(f), " = nil");
	P(g, dep_idxs_var_name(f), " = nil");
	P(g, "}");
}

static COLD void gen_file_descriptor(PgPlugin *gen, PgGeneratedFile *g, FileInfo *f)
{
	(void)gen;
	PbMsg *desc_proto = gopb_clone(f->f->Proto);
	gopb_clear(desc_proto, GENID_FileDescriptorProto_SourceCodeInfo_field_number); /* drop source code information */
	gopb_strip_source_retention(desc_proto);
	Buf b = {0};
	gopb_encode(desc_proto, &b);

	/* Generate the raw descriptor as a kind-of readable const string. To not generate a single
	 * potentially very long line, the 0x0a byte splits the string into multiple "lines"
	 * concatenated with "+" (most descriptor messages start with field 1 as LEN: 0x0a). */
	pg_write(g, str_c(xcat("const ", raw_desc_var_name(f), "=\"\"")));
	size_t start = 0;
	for (;;) {
		const char *nl = b.n > start ? memchr(b.p + start, '\n', b.n - start) : NULL;
		size_t end = nl ? (size_t)(nl - b.p) + 1 : b.n;
		P(g, "+");
		pg_write(g, str_c(quote_go((Str){b.p ? b.p + start : "", end - start})));
		if (!nl)
			break;
		start = end;
	}
	P(g, "");

	if (f->needRawDesc) {
		const char *once_var = xcat(raw_desc_var_name(f), "Once");
		const char *data_var = xcat(raw_desc_var_name(f), "Data");
		P(g, "var (");
		P(g, once_var, " ", ID(syncPackage, "Once"));
		P(g, data_var, " []byte");
		P(g, ")");
		P(g, "");

		P(g, "func ", raw_desc_var_name(f), "GZIP() []byte {");
		P(g, once_var, ".Do(func() {");
		P(g, data_var, " = ", ID(protoimplPackage, "X"), ".CompressGZIP(", unsafe_bytes_raw_desc(g, f), ")");
		P(g, "})");
		P(g, "return ", data_var);
		P(g, "}");
		P(g, "");
	}
}

/* An inlined version of strs.UnsafeBytes (gencode cannot depend on internal/strs). */
static COLD const char *unsafe_bytes_raw_desc(PgGeneratedFile *g, FileInfo *f)
{
	const char *slice = pg_qualified_go_ident(g, ID(unsafePackage, "Slice"));
	const char *data = pg_qualified_go_ident(g, ID(unsafePackage, "StringData"));
	const char *v = raw_desc_var_name(f);
	return xcat(slice, "(", data, "(", v, "), len(", v, "))");
}

static COLD void gen_enum_reflect_methods(PgGeneratedFile *g, FileInfo *f, EnumInfo *ei)
{
	PgEnum *e = ei->e;
	int idx = ei->index;
	const char *types_var = enum_types_var_name(f);

	/* Descriptor method. */
	P(g, "func (", e->GoIdent, ") Descriptor() ", ID(protoreflectPackage, "EnumDescriptor"), " {");
	P(g, "return ", types_var, "[", idx, "].Descriptor()");
	P(g, "}");
	P(g, "");

	/* Type method. */
	P(g, "func (", e->GoIdent, ") Type() ", ID(protoreflectPackage, "EnumType"), " {");
	P(g, "return &", types_var, "[", idx, "]");
	P(g, "}");
	P(g, "");

	/* Number method. */
	P(g, "func (x ", e->GoIdent, ") Number() ", ID(protoreflectPackage, "EnumNumber"), " {");
	P(g, "return ", ID(protoreflectPackage, "EnumNumber"), "(x)");
	P(g, "}");
	P(g, "");
}

static COLD void gen_message_reflect_methods(PgGeneratedFile *g, FileInfo *f, MessageInfo *m)
{
	int idx = m->index;
	const char *types_var = message_types_var_name(f);

	/* ProtoReflect method. */
	P(g, "func (x *", m->m->GoIdent, ") ProtoReflect() ", ID(protoreflectPackage, "Message"), " {");
	P(g, "mi := &", types_var, "[", idx, "]");
	P(g, "if x != nil {");
	P(g, "ms := ", ID(protoimplPackage, "X"), ".MessageStateOf(", ID(protoimplPackage, "Pointer"), "(x))");
	P(g, "if ms.LoadMessageInfo() == nil {");
	P(g, "ms.StoreMessageInfo(mi)");
	P(g, "}");
	P(g, "return ms");
	P(g, "}");
	P(g, "return mi.MessageOf(x)");
	P(g, "}");
	P(g, "");
}
