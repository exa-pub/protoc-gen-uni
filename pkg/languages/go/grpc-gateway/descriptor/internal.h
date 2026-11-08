/* Copyright (c) 2015, Gengo, Inc. All rights reserved. BSD-3-Clause (see LICENSE of
 * github.com/grpc-ecosystem/grpc-gateway). */

/* internal/descriptor of grpc-gateway v2.33.0: declarations shared by its files. */
#ifndef GW_DESCRIPTOR_INTERNAL_H
#define GW_DESCRIPTOR_INTERNAL_H

#include "go_std.h"
#include "grpcgateway.h"

/* The alias "ext" + cases.Title(language.AmericanEnglish).String(name) of a standalone package
 * when the name is not ASCII: the port implements Title for ASCII names only (Unicode word
 * breaking and title case mappings of golang.org/x/text are not ported). Printing it fails with
 * an "unsupported" error; comparisons of GoPackage values work on it as on any alias. */
extern const char gw_unsupported_title[];
/* alias, or fail() if it is gw_unsupported_title */
const char *gw_alias(const char *alias);

/* services.go: loadServices */
Str gw_Registry_loadServices(GwRegistry *r, GwFile *file);
/* buf_build.go */
void gw_Registry_IncludeAdditionalImports(GwRegistry *r, GwService *svc, GwGoPackage goPkg);

#endif
