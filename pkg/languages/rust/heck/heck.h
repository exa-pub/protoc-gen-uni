/* Port of the heck 0.5.0 case conversions (https://github.com/withoutboats/heck, MIT OR
 * Apache-2.0), used by prost-build and pbjson-build. Results are arena strings.
 *
 * heck is Unicode-aware; the port classifies ASCII exactly and treats every non-ASCII char as an
 * alphanumeric char without case (copied unchanged). Protobuf identifiers are ASCII. */
#ifndef HECK_H
#define HECK_H

#include "util.h"

char *heck_snake(const char *s);        /* ToSnakeCase: lower_snake */
char *heck_shouty_snake(const char *s); /* ToShoutySnakeCase: UPPER_SNAKE */
char *heck_kebab(const char *s);        /* ToKebabCase: lower-kebab */
char *heck_shouty_kebab(const char *s); /* ToShoutyKebabCase: UPPER-KEBAB */
char *heck_upper_camel(const char *s);  /* ToUpperCamelCase: UpperCamel */
char *heck_lower_camel(const char *s);  /* ToLowerCamelCase: lowerCamel */
char *heck_title(const char *s);        /* ToTitleCase: Title Case */
char *heck_train(const char *s);        /* ToTrainCase: Train-Case */

#endif
