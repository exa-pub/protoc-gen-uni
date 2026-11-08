/* The core of protoc-gen-uni: selects a generator by the uni-name and uni-version parameters and
 * passes the request with the remaining parameters to it. Same contract and error texts as
 * pkg/uni of the Go implementation. */
#ifndef UNI_H
#define UNI_H

#include "pb.h"

#define UNI_PARAM_NAME "uni-name"
#define UNI_PARAM_VERSION "uni-version"

/* A port of one upstream plugin at one exact version. */
typedef struct {
	/* The plugin name as on buf.build: "<owner>/<plugin>", e.g. "bufbuild/es". */
	const char *name;
	/* The exact upstream version with the "v" prefix, e.g. "v2.17.0". */
	const char *version;
	/* Handles a request whose parameter no longer contains uni-* options and writes the
	 * serialized CodeGeneratorResponse to out. Generation errors are reported in
	 * CodeGeneratorResponse.error, as upstream does. raw is the serialized request as received
	 * (with the original parameter), for generators that parse it with other semantics than
	 * pkg/pb (protobuf-go). Returns NULL, or the error of a request that the generator cannot
	 * parse (out is ignored; the process exits with status 1 like for unparsable requests). */
	const char *(*generate)(PbMsg *req, Str raw, Buf *out);
	/* generate reads nothing from req but the parameter and parses raw itself: uni_main then
	 * also runs it for a request that pkg/pb rejects as nested deeper than PB_MAX_DEPTH, with a
	 * req that holds only the parameter. Otherwise such a request is a parse error. */
	bool parses_raw;
} UniGenerator;

/* Extracts uni-name and uni-version from a protoc parameter string. The remaining
 * comma-separated elements are returned unchanged and in their original order (some plugins echo
 * the parameter verbatim into generated files). Returns NULL or an error message. */
COLD const char *uni_split_parameter(Str parameter, Str *name, Str *version, Str *rest);

/* Finds the generator with the exact name and version in gens (n entries). Returns NULL and sets
 * *out, or returns an error message. */
COLD const char *uni_lookup(const UniGenerator *const *gens, int n, Str name, Str version, const UniGenerator **out);

/* Dispatches req to the generator selected by its uni-* parameters and writes the serialized
 * response to out. The request parameter is replaced with the parameter without uni-* options.
 * Returns NULL or the parse error of the generator (see UniGenerator.generate). */
const char *uni_run(PbMsg *req, Str raw, const UniGenerator *const *gens, int n, Buf *out);

/* Parses the serialized CodeGeneratorRequest in[0:len] and runs it. Returns NULL, or a message
 * for stderr (exit status 1) if the request cannot be parsed (including a request nested deeper
 * than PB_MAX_DEPTH for a generator without parses_raw). */
const char *uni_main(const uint8_t *in, size_t len, const UniGenerator *const *gens, int n, Buf *out);

#endif
