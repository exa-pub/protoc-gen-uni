/* proc-macro2 1.0.106 (https://github.com/dtolnay/proc-macro2, MIT OR Apache-2.0, Copyright David
 * Tolnay): the token text of the literals that `quote!` interpolates outside a procedural macro (the
 * fallback implementation, src/fallback.rs), which the generated code shows verbatim. Results are
 * arena strings. */
#ifndef PROC_MACRO2_H
#define PROC_MACRO2_H

#include "util.h"

typedef struct PpNode PpNode; /* prettyplease.h */

/* Literal::string: "..." with char::escape_debug, except ' unescaped and NUL as \0 (\x00 before
 * an octal digit). */
char *pm2_string(Str s);
/* Literal::byte_string: b"..." with \0 \t \n \r \" \\, printable ASCII, else \xHH. */
char *pm2_byte_string(Str s);
/* Literal::character */
char *pm2_character(uint32_t c);
/* Literal::<int>_suffixed: "<n><suffix>" ("1u32", "-3i32"); <int>_unsuffixed: pm2_int(n, ""). */
char *pm2_int(int64_t n, const char *suffix);
char *pm2_uint(uint64_t n, const char *suffix);
/* Literal::f32_suffixed / f64_suffixed: Display of the value plus the suffix ("1.5f32", "1f64"). */
char *pm2_f32_suffixed(float v);
char *pm2_f64_suffixed(double v);

/* TokenStream::from_str (parse.c, proc-macro2 1.0.103 src/parse.rs): appends the token trees of
 * src to out->list as prettyplease token nodes (PP_TT_IDENT, PP_TT_PUNCT with PP_F_JOINT,
 * PP_TT_LIT, PP_TT_GROUP); a doc comment becomes `#` [`!`] `[doc = "..."]`. Returns false on a
 * LexError. A non-ASCII identifier char fails as unsupported. */
bool pm2_token_stream(Str src, PpNode *out);

#endif
