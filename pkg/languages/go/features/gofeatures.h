/* Copyright 2024 The Go Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file. */

/* Edition features as protobuf-go resolves them (internal/filedesc.EditionFeatures), ported from
 * google.golang.org/protobuf v1.36.12:
 *   - internal/editiondefaults (editions_defaults.binpb, as a table: features_defaults),
 *   - reflect/protodesc/editions.go (getFeatureSetFor, mergeEditionFeatures),
 *   - internal/filedesc/presence.go (UsePresenceForField). */
#ifndef GO_FEATURES_H
#define GO_FEATURES_H

#include "pb.h"

/* filedesc.EditionFeatures (the fields protobuf-go uses). */
typedef struct {
	bool IsFieldPresence, IsLegacyRequired, IsOpenEnum, IsPacked, IsUTF8Validated, IsDelimitedEncoded,
		IsJSONCompliant;
	/* pb.go features (go_features.proto) */
	bool GenerateLegacyUnmarshalJSON;
	int APILevel, StripEnumPrefix;
} GoEditionFeatures;

/* The supported range of the embedded defaults (FeatureSetDefaults.minimum/maximum_edition). */
enum { FEATURES_MINIMUM_EDITION = 998, FEATURES_MAXIMUM_EDITION = 1001 };

/* getFeatureSetFor merged into empty features: the defaults of an edition. The edition must be in
 * the supported range or EDITION_UNSTABLE (protodesc rejects other editions first). */
GoEditionFeatures features_for_edition(int edition);

/* mergeEditionFeatures: features set in child (a google.protobuf.FeatureSet parsed with the
 * schema of protodesc.h, where pb.go is the known field 1002; may be NULL) override parent. */
GoEditionFeatures features_merge(GoEditionFeatures parent, const PbMsg *child);

/* filedesc.UsePresenceForField for a field with the given properties: whether the presence
 * bitmap is used, and whether the field can be lazy. */
bool features_use_presence_for_field(bool in_real_oneof, bool is_map, bool is_message, bool is_lazy, bool has_presence,
				     bool *can_be_lazy);

#endif
