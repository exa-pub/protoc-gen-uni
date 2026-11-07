/* Shared between the files of this library only. */
#ifndef ES_PROTOBUF_INTERNAL_H
#define ES_PROTOBUF_INTERNAL_H

#include "protobuf.h"

/* features.c: feature numbers of google.protobuf.FeatureSet, minus one. */
enum { F_FIELD_PRESENCE, F_ENUM_TYPE, F_REPEATED_FIELD_ENCODING, F_UTF8, F_MESSAGE_ENCODING, F_JSON_FORMAT };
/* The defaults of features 1..8 of an edition, or NULL if the edition is not supported. */
const int32_t *feature_defaults(int edition);
/* Resolves a feature from own (the FeatureSet of a field or enum, may be NULL) and the parents
 * (messages up to the file). */
int32_t resolve_feature(int feat, const PbMsg *own, const Desc *parent);

#endif
