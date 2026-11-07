/* Internal declarations of pkg/languages/go/protogen. */
#ifndef GO_PROTOGEN_INTERNAL_H
#define GO_PROTOGEN_INTERNAL_H

#include "genid.h"
#include "protogen.h"
#include "strs.h"

enum { PATH_TYPE_IMPORT = 0, PATH_TYPE_SOURCE_RELATIVE = 1 };

/* protogen.c */
const char *pg_path_clean(const char *path);
const char *pg_path_base(const char *path);
PgLocation pg_location_append(PgLocation loc, int32_t num, int32_t idx);
/* pkg/languages/go/protodesc helpers */
const char *go_err(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
char *go_itoa(int64_t v);

#endif
