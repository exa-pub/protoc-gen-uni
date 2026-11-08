/* Internal declarations of pkg/languages/go/protodesc. */
#ifndef GO_PROTODESC_INTERNAL_H
#define GO_PROTODESC_INTERNAL_H

#include "genid.h"
#include "protodesc.h"

/* schema.c: the static message tables. msg: index of the message type in go_spec_msgs or -1. */
typedef struct {
	uint32_t num;
	uint8_t type, label, flags;
	int8_t msg;
} GoSpecField;

typedef struct {
	const char *name;
	const GoSpecField *f;
	int n;
	bool has_ext;
	const char *const *names; /* field names (for the errors of required fields), or NULL */
} GoSpecMsg;

typedef struct {
	const char *name;
	int extendee;
	uint32_t num;
	int msg;
} GoSpecExt;

extern const GoSpecMsg go_spec_msgs[];
extern const int go_spec_n_msgs;
extern const GoSpecExt go_spec_base_ext;

/* desc.c */
const char *go_err(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
bool go_name_is_valid(const char *s);
bool go_full_name_is_valid(const char *s);
const char *go_quote(const char *s); /* %q of a NUL-terminated string */
const char *go_quote_str(Str s);
char *go_itoa(int64_t v);
char *go_utoa(uint64_t v);

/* validate.c */
const char *go_validate_enums(GoEnum **es, int n);
const char *go_validate_messages(GoFile *f, GoMessage **ms, int n);
const char *go_validate_extensions(GoFile *f, GoField **xs, int n);

#endif
