/* Not a port: the inputs upstream accepts that the port does not implement. They fail with
 * "unsupported by protoc-gen-uni: ..." instead of giving a different output (README.md).
 * protoc and buf never produce them. */

#define UNSUPPORTED "unsupported by protoc-gen-uni: "

/* The port reads names as NUL-terminated strings, so a NUL byte would cut them short. */
COLD static void assert_no_nul(Str s, const char *what)
{
	if (memchr(s.p, 0, s.n))
		fail(UNSUPPORTED "NUL byte in %s %s", what, quote_json(s));
}

COLD static void assert_enum_supported(const PbMsg *e)
{
	assert_no_nul(pb_bytes(e, EDP_NAME), "EnumDescriptorProto.name");
	for (int i = 0; i < pb_count(e, EDP_VALUE); i++)
		assert_no_nul(pb_bytes(pb_msg_at(e, EDP_VALUE, i), EVDP_NAME), "EnumValueDescriptorProto.name");
}

/* Recursion is bounded by the nesting limit of pkg/pb. */
COLD static void assert_message_supported(const PbMsg *m)
{
	assert_no_nul(pb_bytes(m, DP_NAME), "DescriptorProto.name");
	for (int i = 0; i < pb_count(m, DP_FIELD); i++) {
		const PbMsg *f = pb_msg_at(m, DP_FIELD, i);
		assert_no_nul(pb_bytes(f, FD_NAME), "FieldDescriptorProto.name");
		assert_no_nul(pb_bytes(f, FD_JSON_NAME), "FieldDescriptorProto.json_name");
		assert_no_nul(pb_bytes(f, FD_TYPE_NAME), "FieldDescriptorProto.type_name");
	}
	for (int i = 0; i < pb_count(m, DP_ONEOF_DECL); i++)
		assert_no_nul(pb_bytes(pb_msg_at(m, DP_ONEOF_DECL, i), ODP_NAME), "OneofDescriptorProto.name");
	for (int i = 0; i < pb_count(m, DP_ENUM_TYPE); i++)
		assert_enum_supported(pb_msg_at(m, DP_ENUM_TYPE, i));
	for (int i = 0; i < pb_count(m, DP_NESTED_TYPE); i++)
		assert_message_supported(pb_msg_at(m, DP_NESTED_TYPE, i));
}

/* The strings of the request the port reads, and the packages: the module of every package is
 * derived with heck (ModuleRequestSet), so a non-ASCII one can merge or split modules. */
COLD static void assert_supported(const PbMsg *req)
{
	for (int i = 0; i < pb_count(req, CGR_FILE_TO_GENERATE); i++)
		assert_no_nul(pb_bytes_at(req, CGR_FILE_TO_GENERATE, i), "CodeGeneratorRequest.file_to_generate");
	for (int i = 0; i < pb_count(req, CGR_PROTO_FILE); i++) {
		const PbMsg *file = pb_msg_at(req, CGR_PROTO_FILE, i);
		assert_no_nul(pb_bytes(file, FDP_NAME), "FileDescriptorProto.name");
		assert_no_nul(pb_bytes(file, FDP_SYNTAX), "FileDescriptorProto.syntax");
		Str package = pb_bytes(file, FDP_PACKAGE);
		assert_no_nul(package, "FileDescriptorProto.package");
		for (size_t k = 0; k < package.n; k++)
			if ((uint8_t)package.p[k] >= 0x80)
				fail(UNSUPPORTED "non-ASCII package %s (upstream derives module names with Unicode "
					     "case conversion)",
				     quote_json(package));
		for (int k = 0; k < pb_count(file, FDP_MESSAGE_TYPE); k++)
			assert_message_supported(pb_msg_at(file, FDP_MESSAGE_TYPE, k));
		for (int k = 0; k < pb_count(file, FDP_ENUM_TYPE); k++)
			assert_enum_supported(pb_msg_at(file, FDP_ENUM_TYPE, k));
	}
}
