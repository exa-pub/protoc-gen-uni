/* Copyright (c) 2015, Gengo, Inc. All rights reserved. BSD-3-Clause (see LICENSE of
 * github.com/grpc-ecosystem/grpc-gateway). */

/* Port of internal/descriptor/buf_build.go, which bufbuild/plugins'
 * separate_pkg_additional_imports.patch adds to grpc-gateway v2.33.0 for the buf.build gateway
 * plugin (plugins/grpc-ecosystem/gateway/v2.33.0). */
#include "internal.h"

#include <string.h>

/* strings.Replace(s, old, new, 1) */
static const char *replace1(const char *s, const char *old, const char *new)
{
	const char *p = strstr(s, old);
	if (!p)
		return s;
	return xcat(xstrndup(s, (size_t)(p - s)), new, p + strlen(old));
}

/* IncludeAdditionalImports adds additionalImports to the registry on a per-package basis */
void gw_Registry_IncludeAdditionalImports(GwRegistry *r, GwService *svc, GwGoPackage goPkg)
{
	if (!r->separatePackage)
		return;
	/* when generating a separate package for the gateway, we need to generate an import
	 * statement for the gRPC stubs that are no longer in the same package. This is done by
	 * adding the grpc package to the additionalImports list. In order to prepare a valid import
	 * statement, we'll replace the source package name, something like: ../pet/v1/v1petgateway
	 * with ../pet/v1/v1petgrpc */
	const char *packageName = xcat(str_trim_suffix(goPkg.Name, "gateway"), "grpc");
	svc->GRPCFile = xalloc(sizeof *svc->GRPCFile);
	/* additionally, as the `go_package` option is passed through from the generator, and can
	 * only be set the one time, without making major changes, we'll use the package name sent
	 * through the options as a basis, and replace the source package name with the grpc package
	 * name. */
	svc->GRPCFile->GoPkg = (GwGoPackage){
		replace1(pg_path_join(goPkg.Path, packageName), GW_BaseTypePackageSubPath, "grpc/go"),
		replace1(packageName, GW_BaseTypePackageSubPath, "grpc/go"),
		"",
	};
	Vec *v = map_get(&r->additionalImports, goPkg.Path);
	if (!v) {
		v = xalloc(sizeof *v);
		map_put(&r->additionalImports, goPkg.Path, v);
	}
	vec_push(v, (void *)svc->GRPCFile->GoPkg.Path);
}

/* GetAdditionalImports returns additionalImports */
int gw_Registry_GetAdditionalImports(GwRegistry *r, GwGoPackage goPkg, const char ***out)
{
	*out = NULL;
	if (!r->separatePackage)
		return 0;
	Vec *v = map_get(&r->additionalImports, goPkg.Path);
	if (!v)
		return 0;
	*out = (const char **)v->v;
	return v->n;
}
