/* Internal declarations of pkg/languages/go/protoplugin (exported for the unit tests). */
#ifndef GO_PROTOPLUGIN_INTERNAL_H
#define GO_PROTOPLUGIN_INTERNAL_H

#include "goprotoplugin.h"

/* validate.c */
Str gopp_path_clean(Str path);
const char *gopp_key(Str s);
const char *gopp_validate_and_normalize_path(const char *field, Str path, Str *normalized);
const char *gopp_unnormalized_name_error(Str name, Str normalized, bool is_warning);
const char *gopp_duplicate_name_error(Str name, bool is_warning);

#endif
