// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* Port of FileDescriptor::GetSourceLocation and FileDescriptorTables::GetSourceLocation
 * (src/google/protobuf/descriptor.cc): the locations by path are built once per file, the last
 * location of a path wins, and only spans of 3 or 4 elements give a location. The table is kept
 * by the caller (LpSourceLocations), not in the file. */

struct LpSourceLocations {
	const LpFile *file;
	Map by_path; /* "1,2,3" -> Location */
};

static char *source_path_key(const int32_t *path, int n)
{
	Buf b = {0};
	for (int i = 0; i < n; i++) {
		if (i)
			buf_addc(&b, ',');
		buf_adds(&b, lp_itoa(path[i]));
	}
	return (char *)buf_str(&b);
}

LpSourceLocations *lp_source_locations(const LpFile *file)
{
	LpSourceLocations *t = xalloc(sizeof *t);
	t->file = file;
	const PbMsg *info = file->source_code_info;
	for (int i = 0; i < pb_count(info, SCI_LOCATION); i++) {
		const PbMsg *loc = pb_msg_at(info, SCI_LOCATION, i);
		const PbSlot *s = pb_slot(loc, LOC_PATH);
		int n = s ? s->n : 0;
		int32_t *path = xalloc(sizeof(int32_t) * (size_t)(n + 1));
		for (int j = 0; j < n; j++)
			path[j] = (int32_t)s->v[j].u;
		map_put(&t->by_path, source_path_key(path, n), (void *)loc);
	}
	return t;
}

bool lp_get_source_location_path(const LpSourceLocations *t, const int32_t *path, int n, LpSourceLocation *out)
{
	const PbMsg *loc = map_get(&t->by_path, source_path_key(path, n));
	if (!loc)
		return false;
	const PbSlot *span = pb_slot(loc, PROTO_LOC_SPAN);
	int ns = span ? span->n : 0;
	if (ns != 3 && ns != 4)
		return false;
	out->start_line = (int32_t)span->v[0].u;
	out->start_column = (int32_t)span->v[1].u;
	out->end_line = (int32_t)span->v[ns == 3 ? 0 : 2].u;
	out->end_column = (int32_t)span->v[ns - 1].u;
	out->leading_comments = pb_bytes(loc, LOC_LEADING);
	out->trailing_comments = pb_bytes(loc, LOC_TRAILING);
	int nd = pb_count(loc, LOC_LEADING_DETACHED);
	Str *detached = xalloc(sizeof(Str) * (size_t)(nd + 1));
	for (int i = 0; i < nd; i++)
		detached[i] = pb_bytes_at(loc, LOC_LEADING_DETACHED, i);
	out->leading_detached_comments = detached;
	out->n_leading_detached_comments = nd;
	return true;
}

bool lp_get_source_location(const LpSourceLocations *t, const ProtoDesc *d, LpSourceLocation *out)
{
	int n;
	int32_t *path = proto_desc_source_path(d, &n);
	return lp_get_source_location_path(t, path, n, out);
}
