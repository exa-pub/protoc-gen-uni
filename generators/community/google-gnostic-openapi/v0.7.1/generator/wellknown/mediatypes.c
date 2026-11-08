/* Copyright 2021 Google LLC. All Rights Reserved.
 * Licensed under the Apache License, Version 2.0 (see LICENSE of github.com/google/gnostic). */

/* Port of cmd/protoc-gen-openapi/generator/wellknown/mediatypes.go (gnostic v0.7.1). */

static PbMsg *wk_media_types(const char *name, PbMsg *schema)
{
	PbMsg *mt = oa_new(OA_MediaType);
	oa_set_msg(mt, OA_MediaType_schema, schema);
	PbMsg *named = oa_new(OA_NamedMediaType);
	oa_set_cstr(named, OA_NamedMediaType_name, name);
	oa_set_msg(named, OA_NamedMediaType_value, mt);
	PbMsg *mts = oa_new(OA_MediaTypes);
	oa_add_msg(mts, OA_MediaTypes_additional_properties, named);
	return mts;
}

static PbMsg *wk_NewGoogleApiHttpBodyMediaType(void) { return wk_media_types("*/*", NULL); }

static PbMsg *wk_NewApplicationJsonMediaType(PbMsg *schema) { return wk_media_types("application/json", schema); }
