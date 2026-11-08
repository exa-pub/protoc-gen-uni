/* Internal declarations of pkg/languages/go/protogen. */
#ifndef GO_PROTOGEN_INTERNAL_H
#define GO_PROTOGEN_INTERNAL_H

#include "genid.h"
#include "protogen.h"
#include "strs.h"

enum { PATH_TYPE_IMPORT = 0, PATH_TYPE_SOURCE_RELATIVE = 1 };

/* protogen.c */
PgLocation pg_location_append(PgLocation loc, int32_t num, int32_t idx);
/* pkg/languages/go/protodesc helpers */
const char *go_err(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
char *go_itoa(int64_t v);
const char *go_quote(const char *s);

/* PgOptions.Version: an older compiler/protogen (protogen_v1.31.0.c, protogen_v1.36.7.c). Behind function pointers so
 * that generators of the newest version do not link its code; the other differences are
 * conditions on PgOptions.Version in protogen.c and generated_file.c. */
struct PgVersion {
	/* before protodesc.NewFile: an error for a file that version rejects, or NULL */
	const char *(*check_file)(PbMsg *p);
	/* the error text of protodesc.NewFile in that version */
	const char *(*new_file_error)(const char *err);
	/* after protodesc.NewFile: the checks of that version that protodesc does not make */
	const char *(*validate_file)(const GoFile *fd);
	/* a v1.36 release (v1.36.7): API levels, strip_enum_prefix, the pb.go extension linked by
	 * protogen and supported_features in error responses, which v1.31.0 has none of. Every
	 * older version processes all of CodeGeneratorRequest.proto_file. */
	bool v1_36;
};
/* PgOptions.Version behaves like v1.36 (v1.36.12 or another v1.36 release) */
#define PG_V1_36(v) (!(v) || (v)->v1_36)

#endif
