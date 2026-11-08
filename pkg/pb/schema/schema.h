/* Helpers for the hand-written schema tables. Every table lists the fields of one message,
 * sorted by number: S(number, type, label) for scalars, M(number, label, def) for messages and
 * E(number, label, values) for closed enums (values: the known numbers). */
#ifndef PB_SCHEMA_H
#define PB_SCHEMA_H

#include "pb.h"

#define N(a) (int)(sizeof(a) / sizeof((a)[0]))
#define S(num, t, l) {num, PT_##t, l, NULL, NULL, 0}
#define M(num, l, d) {num, PT_MESSAGE, l, &d, NULL, 0}
#define E(num, l, vals) {num, PT_ENUM, l, NULL, vals, N(vals)}
#define DEF(name, fields) const PbMsgDef name = {N(fields), fields}

#endif
