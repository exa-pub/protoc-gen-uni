// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* The parts of DynamicMessageFactory and Reflection (src/google/protobuf/dynamic_message.cc,
 * generated_message_reflection.cc) that generators use on messages of a builder pool: a dynamic
 * message parsed from bytes, and the descriptors of its set fields (ListFields: the slots of a
 * PbMsg are in field number order, extensions included). */

PbMsg *lp_dynamic_message_parse(const LpFile *file, const char *full_name, Str data)
{
	LpPool *pool = lp_file_pool(file);
	const LpMessage *desc = lp_find_message(pool, full_name);
	if (!desc)
		return NULL;
	PbMsg *m = pb_new(&lp_msgdef(pool, desc)->pb);
	if (!lp_decode(m, (const uint8_t *)data.p, data.n))
		return NULL;
	return m;
}

const LpField *lp_msg_slot_field(const PbMsg *m, int i) { return lp_def(m)->fields[i]; }
