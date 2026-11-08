/* buffa-codegen 0.9.2 port: unity build (README.md). Copyright Anthropic, PBC, Apache-2.0. Only the
 * functions of buffa_codegen.h are exported. */
#include "internal.h"

static const char *bc_generate_files(const BcConfig *c, const PbMsg *req, Vec *files);

#include "lib.c"
#include "quote.c"
#include "feature_gates.c"
#include "context.c"
#include "comments.c"
#include "defaults.c"
#include "enumeration.c"
#include "impl_message.c"
#include "feature_overrides.c"
#include "oneof.c"
#include "message.c"
#include "view.c"
#include "json.c"
#include "lazy_view.c"
#include "impl_text.c"
#include "extension.c"
#include "reflect.c"
#include "generate.c"
