/* Copyright (c) 2015, Gengo, Inc. All rights reserved. BSD-3-Clause (see LICENSE of
 * github.com/grpc-ecosystem/grpc-gateway). */

/* Port of grpc-gateway v2.33.0 internal/descriptor/types.go, with the changes of bufbuild/plugins'
 * separate_pkg_additional_imports.patch (Service.GRPCFile). */
#include "internal.h"

#include <string.h>

static const char *const wellKnownTypeConv[][2] = {
	{".google.protobuf.Timestamp", "runtime.Timestamp"},
	{".google.protobuf.Duration", "runtime.Duration"},
	{".google.protobuf.StringValue", "runtime.StringValue"},
	{".google.protobuf.FloatValue", "runtime.FloatValue"},
	{".google.protobuf.DoubleValue", "runtime.DoubleValue"},
	{".google.protobuf.BoolValue", "runtime.BoolValue"},
	{".google.protobuf.BytesValue", "runtime.BytesValue"},
	{".google.protobuf.Int32Value", "runtime.Int32Value"},
	{".google.protobuf.UInt32Value", "runtime.UInt32Value"},
	{".google.protobuf.Int64Value", "runtime.Int64Value"},
	{".google.protobuf.UInt64Value", "runtime.UInt64Value"},
};

static const char *wkt_conv(const char *typeName)
{
	for (size_t i = 0; i < sizeof wellKnownTypeConv / sizeof *wellKnownTypeConv; i++)
		if (strcmp(typeName, wellKnownTypeConv[i][0]) == 0)
			return wellKnownTypeConv[i][1];
	return NULL;
}

/* IsWellKnownType returns true if the provided fully qualified type name is considered
 * 'well-known'. */
bool gw_IsWellKnownType(const char *typeName) { return wkt_conv(typeName) != NULL; }

bool gw_GoPackage_eq(GwGoPackage a, GwGoPackage b)
{
	return strcmp(a.Path, b.Path) == 0 && strcmp(a.Name, b.Name) == 0 && strcmp(a.Alias, b.Alias) == 0;
}

/* Standard returns whether the import is a golang standard package. */
bool gw_GoPackage_Standard(GwGoPackage p) { return !strchr(p.Path, '.'); }

/* String returns a string representation of this package in the form of import line in golang. */
const char *gw_GoPackage_String(GwGoPackage p)
{
	const char *q = go_std_quote(p.Path, strlen(p.Path), NULL);
	if (!*p.Alias)
		return q;
	return xcat(gw_alias(p.Alias), " ", q);
}

/* Pkg returns package name or alias if it's present */
const char *gw_File_Pkg(const GwFile *f)
{
	const char *pkg = f->GoPkg.Name;
	if (*f->GoPkg.Alias)
		pkg = gw_alias(f->GoPkg.Alias);
	return pkg;
}

static const char *fq(const GwFile *f, const char *const *outers, int nOuters, const char *name)
{
	Buf b = {0};
	if (f->has_package) {
		buf_addc(&b, '.');
		buf_adds(&b, f->package);
	}
	for (int i = 0; i < nOuters; i++) {
		buf_addc(&b, '.');
		buf_adds(&b, outers[i]);
	}
	buf_addc(&b, '.');
	buf_adds(&b, name);
	return buf_str(&b);
}

/* FQMN returns a fully qualified message name of this message. */
const char *gw_Message_FQMN(const GwMessage *m) { return fq(m->File, m->Outers, m->nOuters, m->Name); }

static const char *goTypeName(const char *const *outers, int nOuters, const char *name)
{
	Buf b = {0};
	for (int i = 0; i < nOuters; i++) {
		buf_adds(&b, gw_casing_Camel(outers[i]));
		buf_addc(&b, '_');
	}
	buf_adds(&b, gw_casing_Camel(name));
	return buf_str(&b);
}

/* GoType returns a go type name for the message type. It prefixes the type name with the package
 * alias if its belonging package is not "currentPackage". */
const char *gw_Message_GoType(const GwMessage *m, const char *currentPackage)
{
	const char *name = goTypeName(m->Outers, m->nOuters, m->Name);
	if (!m->ForcePrefixedName && strcmp(m->File->GoPkg.Path, currentPackage) == 0)
		return name;
	return xcat(gw_File_Pkg(m->File), ".", name);
}

const char *gw_Message_OneofName(const GwMessage *m, int i)
{
	return pb_str(pb_msg_at(m->proto, DP_ONEOF_DECL, i), ODP_NAME);
}

/* FQEN returns a fully qualified enum name of this enum. */
const char *gw_Enum_FQEN(const GwEnum *e) { return fq(e->File, e->Outers, e->nOuters, e->Name); }

/* GoType returns a go type name for the enum type. */
const char *gw_Enum_GoType(const GwEnum *e, const char *currentPackage)
{
	const char *name = goTypeName(e->Outers, e->nOuters, e->Name);
	if (!e->ForcePrefixedName && strcmp(e->File->GoPkg.Path, currentPackage) == 0)
		return name;
	return xcat(gw_File_Pkg(e->File), ".", name);
}

/* FQSN returns the fully qualified service name of this service. GRPCFile has no
 * FileDescriptorProto (its GetPackage() is ""), so the package is always the file's. */
const char *gw_Service_FQSN(const GwService *s) { return fq(s->File, NULL, 0, s->Name); }

/* InstanceName returns object name of the service with package prefix if needed */
const char *gw_Service_InstanceName(const GwService *s)
{
	if (!s->ForcePrefixedName)
		return s->Name;
	const char *pkg = s->GRPCFile ? gw_File_Pkg(s->GRPCFile) : gw_File_Pkg(s->File);
	return xcat(pkg, ".", s->Name);
}

/* ClientConstructorName returns name of the Client constructor with package prefix if needed */
const char *gw_Service_ClientConstructorName(const GwService *s)
{
	const char *constructor = xcat("New", s->Name, "Client");
	if (!s->ForcePrefixedName)
		return constructor;
	const char *pkg = s->GRPCFile ? gw_File_Pkg(s->GRPCFile) : gw_File_Pkg(s->File);
	return xcat(pkg, ".", constructor);
}

/* FQMN returns a fully qualified rpc method name of this method. */
const char *gw_Method_FQMN(const GwMethod *m) { return xcat(gw_Service_FQSN(m->Service), ".", m->Name); }

/* FQFN returns a fully qualified field name of this field. */
const char *gw_Field_FQFN(const GwField *f) { return xcat(gw_Message_FQMN(f->Message), ".", f->Name); }

/* ---- Parameter ---- */

static const char *const typeNames[] = {
	NULL, "TYPE_DOUBLE", "TYPE_FLOAT", "TYPE_INT64", "TYPE_UINT64", "TYPE_INT32", "TYPE_FIXED64",
	"TYPE_FIXED32", "TYPE_BOOL", "TYPE_STRING", "TYPE_GROUP", "TYPE_MESSAGE", "TYPE_BYTES",
	"TYPE_UINT32", "TYPE_ENUM", "TYPE_SFIXED32", "TYPE_SFIXED64", "TYPE_SINT32", "TYPE_SINT64",
};

/* The converter tables, indexed by field type: "runtime." + name + suffix, NULL where a table has
 * no entry (group, message; bytes in the proto2 tables). */
static const char *const convNames[] = {
	NULL, "Float64", "Float32", "Int64", "Uint64", "Int32", "Uint64", "Uint32", "Bool", "String",
	NULL, NULL, "Bytes", "Uint32", "Enum", "Int32", "Int64", "Int32", "Int64",
};

enum { proto3ConvertFuncs, proto3OptionalConvertFuncs, proto3RepeatedConvertFuncs, proto2ConvertFuncs, proto2RepeatedConvertFuncs };

static const char *conv(int tbl, int typ)
{
	if (typ < 1 || typ > 18 || !convNames[typ])
		return NULL;
	if (typ == 12 && (tbl == proto2ConvertFuncs || tbl == proto2RepeatedConvertFuncs))
		return NULL; /* TODO(yugui) Handle bytes */
	static const char *const suffix[] = {"", "P", "Slice", "P", "Slice"};
	return xcat("runtime.", convNames[typ], suffix[tbl]);
}

/* ConvertFuncExpr returns a go expression of a converter function. The converter function
 * converts a string into a value for the parameter. */
const char *gw_Parameter_ConvertFuncExpr(const GwParameter *p, const char **err)
{
	int tbl = proto3ConvertFuncs;
	if (!gw_Parameter_IsProto2(p) && gw_Parameter_IsRepeated(p))
		tbl = proto3RepeatedConvertFuncs;
	else if (!gw_Parameter_IsProto2(p) && gw_FieldPath_IsOptionalProto3(p->FieldPath))
		tbl = proto3OptionalConvertFuncs;
	else if (gw_Parameter_IsProto2(p) && !gw_Parameter_IsRepeated(p))
		tbl = proto2ConvertFuncs;
	else if (gw_Parameter_IsProto2(p) && gw_Parameter_IsRepeated(p))
		tbl = proto2RepeatedConvertFuncs;
	int typ = p->Target->Type;
	const char *c = conv(tbl, typ);
	if (!c)
		c = wkt_conv(p->Target->TypeName);
	if (!c) {
		const char *tn = typ >= 1 && typ <= 18 ? typeNames[typ] : xfmt("%d", typ);
		*err = xfmt("unsupported field type %s of parameter %s in %s.%s", tn, gw_FieldPath_String(p->FieldPath),
			    p->Method->Service->Name, p->Method->Name);
		return NULL;
	}
	return c;
}

/* IsEnum returns true if the field is an enum type, otherwise false is returned. */
bool gw_Parameter_IsEnum(const GwParameter *p) { return p->Target->Type == PT_ENUM; }

/* IsRepeated returns true if the field is repeated, otherwise false is returned. */
bool gw_Parameter_IsRepeated(const GwParameter *p) { return p->Target->Label == 3; }

/* IsProto2 returns true if the field is proto2, otherwise false is returned. */
bool gw_Parameter_IsProto2(const GwParameter *p) { return p->Target->Message->File->proto2; }

/* ---- FieldPath ---- */

/* String returns a string representation of the field path. */
const char *gw_FieldPath_String(GwFieldPath p)
{
	Buf b = {0};
	for (int i = 0; i < p.n; i++) {
		if (i)
			buf_addc(&b, '.');
		buf_adds(&b, p.v[i].Name);
	}
	return buf_str(&b);
}

/* IsNestedProto3 indicates whether the FieldPath is a nested Proto3 path. */
bool gw_FieldPath_IsNestedProto3(GwFieldPath p) { return p.n > 1 && !p.v[0].Target->Message->File->proto2; }

/* IsOptionalProto3 indicates whether the FieldPath is a proto3 optional field. */
bool gw_FieldPath_IsOptionalProto3(GwFieldPath p) { return p.n > 0 && p.v[0].Target->Proto3Optional; }

/* The oneof wrapper of a component in a oneof (not proto3 optional), or NULL. */
static void oneof_names(GwFieldPathComponent c, const char *currentPackage, const char **oneOfName, const char **oneofFieldName)
{
	GwMessage *msg = c.Target->Message;
	*oneOfName = gw_casing_Camel(gw_Message_OneofName(msg, c.Target->OneofIndex));
	*oneofFieldName = xcat(gw_Message_GoType(msg, currentPackage), "_", gw_FieldPathComponent_AssignableExpr(c));
	if (c.Target->ForcePrefixedName)
		*oneofFieldName = xcat(gw_File_Pkg(msg->File), ".", msg->Name, "_", gw_FieldPathComponent_AssignableExpr(c));
}

/* AssignableExpr is an assignable expression in Go to be used to assign a value to the target
 * field. It starts with "msgExpr", which is the go expression of the method request object.
 * Before using such an expression the prep statements must be emitted first, in case the field
 * path includes a oneof. See FieldPath.AssignableExprPrep. */
const char *gw_FieldPath_AssignableExpr(GwFieldPath p, const char *msgExpr, const char *currentPackage)
{
	int l = p.n;
	if (l == 0)
		return msgExpr;

	Buf b = {0};
	buf_adds(&b, msgExpr);
	for (int i = 0; i < l; i++) {
		GwFieldPathComponent c = p.v[i];
		/* We need to check if the target is not proto3_optional first. Under the hood,
		 * proto3_optional uses oneof to signal to old proto3 clients that presence is tracked
		 * for this field. This oneof is known as a "synthetic" oneof. */
		if (!c.Target->Proto3Optional && c.Target->HasOneofIndex) {
			const char *oneOfName, *oneofFieldName;
			oneof_names(c, currentPackage, &oneOfName, &oneofFieldName);
			buf_adds(&b, xcat(".", oneOfName, ".(*", oneofFieldName, ")"));
		}

		if (i == l - 1) {
			buf_addc(&b, '.');
			buf_adds(&b, gw_FieldPathComponent_AssignableExpr(c));
			continue;
		}
		buf_addc(&b, '.');
		buf_adds(&b, gw_FieldPathComponent_ValueExpr(c));
	}
	return buf_str(&b);
}

static bool isSingularMessageField(const GwField *f)
{
	if (f->Label == 3)
		return false;
	return f->Type == PT_MESSAGE || f->Type == PT_GROUP;
}

static GwMessage *messageTypeForIntermediate(GwFieldPath p, int i)
{
	if (p.v[i].Target->FieldMessage)
		return p.v[i].Target->FieldMessage;
	if (i + 1 < p.n)
		return p.v[i + 1].Target->Message;
	return NULL;
}

/* AssignableExprPrep returns preparation statements for an assignable expression to assign a
 * value to the target field. The Go expression of the method request object is "msgExpr". This
 * is needed for field paths that contain oneofs or intermediate message fields. */
const char *gw_FieldPath_AssignableExprPrep(GwFieldPath p, const char *msgExpr, const char *currentPackage)
{
	int l = p.n;
	if (l == 0)
		return "";

	Buf b = {0};
	bool first = true;
	const char *components = msgExpr;
	for (int i = 0; i < l; i++) {
		GwFieldPathComponent c = p.v[i];
		/* (see AssignableExpr) */
		if (!c.Target->Proto3Optional && c.Target->HasOneofIndex) {
			const char *oneOfName, *oneofFieldName;
			oneof_names(c, currentPackage, &oneOfName, &oneofFieldName);
			components = xcat(components, ".", oneOfName);
			if (!first)
				buf_addc(&b, '\n');
			first = false;
			const char *cs = components, *of = oneofFieldName;
			buf_adds(&b, xcat("if ", cs, " == nil {\n\t\t\t\t", cs, " =&", of, "{}\n\t\t\t} else if _, ok := ", cs, ".(*",
					   of, "); !ok {\n\t\t\t\treturn nil, metadata, status.Errorf(codes.InvalidArgument, \"expect type: *",
					   of, ", but: %t\\n\",", cs, ")\n\t\t\t}"));
			components = xcat(components, ".(*", oneofFieldName, ")");
		}

		if (i == l - 1) {
			components = xcat(components, ".", gw_FieldPathComponent_AssignableExpr(c));
			continue;
		}

		if (isSingularMessageField(c.Target)) {
			GwMessage *msg = messageTypeForIntermediate(p, i);
			if (msg) {
				const char *fieldExpr = xcat(components, ".", gw_FieldPathComponent_AssignableExpr(c));
				if (!first)
					buf_addc(&b, '\n');
				first = false;
				buf_adds(&b, xcat("if ", fieldExpr, " == nil {\n\t\t\t\t", fieldExpr, " = &",
						   gw_Message_GoType(msg, currentPackage), "{}\n\t\t\t}"));
			}
		}

		components = xcat(components, ".", gw_FieldPathComponent_ValueExpr(c));
	}
	return buf_str(&b);
}

/* opaqueOwnerExpr builds the Go expression for the message that owns the final component in the
 * path by chaining the generated getters. */
static const char *opaqueOwnerExpr(GwFieldPath p, const char *msgExpr)
{
	if (p.n <= 1)
		return msgExpr;
	Buf b = {0};
	buf_adds(&b, msgExpr);
	for (int i = 0; i < p.n - 1; i++) {
		buf_adds(&b, ".Get");
		buf_adds(&b, gw_casing_Camel(p.v[i].Name));
		buf_adds(&b, "()");
	}
	return buf_str(&b);
}

/* OpaqueSetterExpr returns the Go expression to invoke the generated setter for the final
 * component in the path while respecting nested getters required by the opaque API. */
const char *gw_FieldPath_OpaqueSetterExpr(GwFieldPath p, const char *msgExpr)
{
	if (p.n == 0)
		return msgExpr;
	return xcat(opaqueOwnerExpr(p, msgExpr), ".Set", gw_casing_Camel(p.v[p.n - 1].Name));
}

/* AssignableExpr returns an assignable expression in go for this field. */
const char *gw_FieldPathComponent_AssignableExpr(GwFieldPathComponent c) { return gw_casing_Camel(c.Name); }

/* ValueExpr returns an expression in go for this field. */
const char *gw_FieldPathComponent_ValueExpr(GwFieldPathComponent c)
{
	if (c.Target->Message->File->proto2)
		return xcat("Get", gw_casing_Camel(c.Name), "()");
	return gw_casing_Camel(c.Name);
}
