/* Copyright 2021-2026 Buf Technologies, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License. */

/* Ported from packages/protobuf/src/registry.ts (featureDefaults, resolveFeature). */
#include "internal.h"

/* The defaults of features 1..8 per edition, from the compiled FeatureSetDefaults of
 * @bufbuild/protobuf v2.17.0. */
const int32_t *feature_defaults(int edition)
{
	static const int32_t p2[] = {1, 2, 2, 3, 1, 2, 2, 1}, p3[] = {2, 1, 1, 2, 1, 1, 2, 1},
			     e23[] = {1, 1, 1, 2, 1, 1, 2, 1}, e24[] = {1, 1, 1, 2, 1, 1, 1, 2},
			     e26[] = {1, 1, 1, 2, 1, 1, 3, 4};
	switch (edition) {
	case ED_PROTO2: return p2;
	case ED_PROTO3: return p3;
	case ED_2023: return e23;
	case ED_2024: return e24;
	case ED_2026: return e26;
	}
	return NULL;
}

int32_t resolve_feature(int feat, const PbMsg *own, const Desc *parent)
{
	int64_t v = pb_int(own, (uint32_t)feat + 1, 0);
	if (v)
		return (int32_t)v;
	for (;;) {
		v = pb_int(desc_features(parent), (uint32_t)feat + 1, 0);
		if (v)
			return (int32_t)v;
		if (parent->kind == DK_MESSAGE) {
			const DescMessage *m = (const DescMessage *)parent;
			parent = m->parent ? &m->parent->h : &m->file->h;
		} else {
			const int32_t *d = feature_defaults(((const DescFile *)parent)->edition);
			if (!d)
				fail("feature default for edition %d not found", ((const DescFile *)parent)->edition);
			return d[feat];
		}
	}
}
