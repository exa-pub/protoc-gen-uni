/* Copyright 2018 The Go Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file. */

/* compiler/protogen of google.golang.org/protobuf v1.36.7, for the plugins built with it
 * (community/google-gnostic-openapi): protogen.h (v1.36.12) with PgOptions.Version = &pg_v1_36_7,
 * which gives v1.36.7's behaviour where it differs: Options.New processes every ProtoFile (not
 * only the transitive dependencies of FileToGenerate), and reflect/protodesc supports editions up
 * to 2023 only (descriptorpb of v1.36.7 has no EDITION_UNSTABLE). The other differences
 * (ResolvedFeatures, Options.FeatureSetDefaults, option imports, symbol visibility, proto3
 * optional extensions) do not show for editions up to 2023. */
#ifndef GO_PROTOGEN_V1_36_7_H
#define GO_PROTOGEN_V1_36_7_H

#include "protogen.h"

extern const PgVersion pg_v1_36_7;

#endif
