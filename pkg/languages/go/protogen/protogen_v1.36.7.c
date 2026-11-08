/* Copyright 2018 The Go Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file. */

/* compiler/protogen of google.golang.org/protobuf v1.36.7 where it differs from v1.36.12
 * (protogen.c): see protogen_v1.36.7.h. */
#include "internal.h"
#include "protogen_v1.36.7.h"

/* reflect/protodesc v1.36.7 NewFile: editionssupport.Maximum is EDITION_2023, and the edition is
 * checked after the syntax and the path. */
static COLD const char *v1367_check_file(PbMsg *p)
{
	Str syn = pb_bytes(p, GENID_FileDescriptorProto_Syntax_field_number);
	Str name = pb_bytes(p, GENID_FileDescriptorProto_Name_field_number);
	if (!str_eqc(syn, "editions") || !name.n)
		return NULL; /* protodesc reports the same errors as v1.36.12 */
	int ed = (int)pb_int(p, GENID_FileDescriptorProto_Edition_field_number, 0);
	if (ed >= GENID_Edition_EDITION_PROTO2 && ed <= GENID_Edition_EDITION_2023)
		return NULL;
	if (str_has_prefix(name, STR_LIT("cmd/protoc-gen-go/testdata/")))
		return NULL;
	/* descriptorpb.Edition.String() of v1.36.7: no EDITION_UNSTABLE */
	const char *eds = ed == GENID_Edition_EDITION_UNSTABLE ? go_itoa(ed) : go_edition_string(ed);
	return xcat("invalid FileDescriptorProto ", quote_go(name), ": ",
		    go_err("use of edition %s not yet supported by the Go Protobuf runtime", eds));
}

static COLD const char *v1367_new_file_error(const char *err) { return err; }

static COLD const char *v1367_validate_file(const GoFile *fd)
{
	(void)fd;
	return NULL;
}

const PgVersion pg_v1_36_7 = {v1367_check_file, v1367_new_file_error, v1367_validate_file, true};
