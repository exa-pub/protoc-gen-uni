/* stringcase 1.2.0 (https://github.com/okunishinishi/python-stringcase, MIT): the case
 * conversions of stringcase.py used by betterproto, for ASCII strings (Python's \w, \s and
 * str.lower/upper restricted to ASCII; callers pass identifiers). Results are new strings. */
#ifndef STRINGCASE_H
#define STRINGCASE_H

#include "util.h"

char *sc_camelcase(const char *s);
char *sc_capitalcase(const char *s);
char *sc_pascalcase(const char *s); /* capitalcase(camelcase(s)) */
char *sc_snakecase(const char *s);

#endif
