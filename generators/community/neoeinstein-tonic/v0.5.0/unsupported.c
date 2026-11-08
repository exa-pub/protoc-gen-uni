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

/* Names that go through heck case conversion: upstream converts with Unicode case mapping,
 * pkg/languages/rust/heck only ASCII. */
COLD static void assert_ascii_name(Str s, const char *what)
{
	assert_no_nul(s, what);
	for (size_t k = 0; k < s.n; k++)
		if ((uint8_t)s.p[k] >= 0x80)
			fail(UNSUPPORTED "non-ASCII %s %s (upstream converts its case with Unicode case mapping)", what,
			     quote_json(s));
}

COLD static void assert_supported(const PbMsg *req)
{
	for (int i = 0; i < pb_count(req, CGR_FILE_TO_GENERATE); i++)
		assert_no_nul(pb_bytes_at(req, CGR_FILE_TO_GENERATE, i), "CodeGeneratorRequest.file_to_generate");
	for (int i = 0; i < pb_count(req, CGR_PROTO_FILE); i++) {
		const PbMsg *file = pb_msg_at(req, CGR_PROTO_FILE, i);
		assert_no_nul(pb_bytes(file, FDP_NAME), "FileDescriptorProto.name");
		assert_ascii_name(pb_bytes(file, FDP_PACKAGE), "FileDescriptorProto.package");
		for (int s = 0; s < pb_count(file, FDP_SERVICE); s++) {
			const PbMsg *service = pb_msg_at(file, FDP_SERVICE, s);
			assert_ascii_name(pb_bytes(service, SDP_NAME), "ServiceDescriptorProto.name");
			for (int m = 0; m < pb_count(service, SDP_METHOD); m++) {
				const PbMsg *method = pb_msg_at(service, SDP_METHOD, m);
				assert_ascii_name(pb_bytes(method, MDP_NAME), "MethodDescriptorProto.name");
				assert_ascii_name(pb_bytes(method, MDP_INPUT_TYPE), "MethodDescriptorProto.input_type");
				assert_ascii_name(pb_bytes(method, MDP_OUTPUT_TYPE), "MethodDescriptorProto.output_type");
			}
		}
	}
}
