// Copyright (c) 2015, Gengo, Inc. All rights reserved. BSD-3-Clause (see the license in
// template.c and LICENSE of github.com/grpc-ecosystem/grpc-gateway).

/* Port of protoc-gen-grpc-gateway/internal/gengateway/generator.go (grpc-gateway v2.33.0 with
 * bufbuild/plugins' separate_pkg_additional_imports.patch). Included by generator.c (unity
 * build). */

#include "template.c"

/* A generated file (descriptor.ResponseFile): name, Go package path and content. */
typedef struct {
	const char *Name;
	const char *GoPkgPath;
	const char *Content;
} ResponseFile;

typedef struct {
	GwRegistry *reg;
	GwGoPackage *baseImports;
	int nBaseImports;
	bool useRequestContext;
	Str registerFuncSuffix;
	bool allowPatchFeature;
	bool standalone;
	bool useOpaqueAPI;
	bool separatePackage;
} generator;

/* New returns a new generator which generates grpc gateway files. */
static generator *gengateway_New(GwRegistry *reg, bool useRequestContext, Str registerFuncSuffix, bool allowPatchFeature,
				  bool standalone, bool useOpaqueAPI, bool separatePackage)
{
	static const char *const paths[] = {
		"context",
		"errors",
		"io",
		"net/http",
		"github.com/grpc-ecosystem/grpc-gateway/v2/runtime",
		"github.com/grpc-ecosystem/grpc-gateway/v2/utilities",
		"google.golang.org/protobuf/proto",
		"google.golang.org/grpc",
		"google.golang.org/grpc/codes",
		"google.golang.org/grpc/grpclog",
		"google.golang.org/grpc/metadata",
		"google.golang.org/grpc/status",
	};
	enum { N = sizeof paths / sizeof *paths };
	generator *g = xalloc(sizeof *g);
	g->baseImports = xalloc(sizeof *g->baseImports * N);
	for (int i = 0; i < N; i++) {
		GwGoPackage pkg = {paths[i], pg_path_base(paths[i]), ""};
		if (!gw_Registry_ReserveGoPackageAlias(reg, pkg.Name, pkg.Path)) {
			for (int k = 0;; k++) {
				const char *alias = xfmt("%s_%d", pkg.Name, k);
				if (!gw_Registry_ReserveGoPackageAlias(reg, alias, pkg.Path))
					continue;
				pkg.Alias = alias;
				break;
			}
		}
		g->baseImports[g->nBaseImports++] = pkg;
	}
	g->reg = reg;
	g->useRequestContext = useRequestContext;
	g->registerFuncSuffix = registerFuncSuffix;
	g->allowPatchFeature = allowPatchFeature;
	g->standalone = standalone;
	g->useOpaqueAPI = useOpaqueAPI;
	g->separatePackage = separatePackage;
	return g;
}

/* The imports of a file being collected, with pkgSeen. */
typedef struct {
	GwGoPackage *v;
	int n, cap;
	Map seen;
} imports;

static void add_import(imports *im, GwGoPackage p)
{
	if (im->n == im->cap) {
		int cap = im->cap ? 2 * im->cap : 32;
		im->v = xrealloc(im->v, sizeof *im->v * (size_t)im->cap, sizeof *im->v * (size_t)cap);
		im->cap = cap;
	}
	im->v[im->n++] = p;
}

/* pkg == file.GoPkg || pkgSeen[pkg.Path]; else marks it seen and adds it */
static void add_seen(imports *im, GwFile *file, GwGoPackage pkg)
{
	static int present;
	if (gw_GoPackage_eq(pkg, file->GoPkg) || map_has(&im->seen, pkg.Path))
		return;
	map_put(&im->seen, pkg.Path, &present);
	add_import(im, pkg);
}

/* addEnumPathParamImports handles adding import of enum path parameter go packages */
static void addEnumPathParamImports(generator *g, GwFile *file, GwMethod *m, imports *im)
{
	for (int k = 0; k < m->Bindings.n; k++) {
		GwBinding *b = m->Bindings.v[k];
		for (int i = 0; i < b->nPathParams; i++) {
			const char *err;
			GwEnum *e = gw_Registry_LookupEnum(g->reg, "", b->PathParams[i].Target->TypeName, &err);
			if (!e)
				continue;
			add_seen(im, file, e->File->GoPkg);
		}
	}
}

/* addResponseTypeImports adds the response type's Go package for response_body bindings. */
static void addResponseTypeImports(GwFile *file, GwMethod *m, imports *im)
{
	for (int k = 0; k < m->Bindings.n; k++) {
		GwBinding *b = m->Bindings.v[k];
		if (!b->ResponseBody)
			continue;
		add_seen(im, file, m->ResponseType->File->GoPkg);
	}
}

/* addBodyFieldImports ensures nested body message types pull in their Go packages. */
static void addBodyFieldImports(generator *g, GwFile *file, GwMethod *m, imports *im)
{
	if (!g->useOpaqueAPI)
		return;
	for (int k = 0; k < m->Bindings.n; k++) {
		GwBinding *b = m->Bindings.v[k];
		if (!b->Body || b->Body->FieldPath.n == 0)
			continue;

		GwField *target = b->Body->FieldPath.v[b->Body->FieldPath.n - 1].Target;
		if (!target || !*target->TypeName || !target->Message)
			continue;

		const char *err;
		GwMessage *msg = gw_Registry_LookupMsg(g->reg, gw_Message_FQMN(target->Message), target->TypeName, &err);
		if (!msg)
			continue;
		add_seen(im, file, msg->File->GoPkg);
	}
}

/* generate: the code of the file, or NULL if it has no target service. */
static const char *gengateway_generate(generator *g, GwFile *file, GwGoPackage *aliasedPkg)
{
	imports im = {0};
	static int present;
	for (int i = 0; i < g->nBaseImports; i++) {
		map_put(&im.seen, g->baseImports[i].Path, &present);
		add_import(&im, g->baseImports[i]);
	}

	const char **additional;
	int nadditional = gw_Registry_GetAdditionalImports(g->reg, file->GoPkg, &additional);
	for (int i = 0; i < nadditional; i++) {
		const char *slash = strrchr(additional[i], '/');
		add_import(&im, (GwGoPackage){additional[i], slash ? slash + 1 : additional[i], ""});
	}

	if (g->standalone)
		add_import(&im, file->GoPkg);

	for (int s = 0; s < file->Services.n; s++) {
		GwService *svc = file->Services.v[s];
		for (int i = 0; i < svc->Methods.n; i++) {
			GwMethod *m = svc->Methods.v[i];
			addEnumPathParamImports(g, file, m, &im);
			addBodyFieldImports(g, file, m, &im);
			addResponseTypeImports(file, m, &im);
			GwGoPackage pkg = m->RequestType->File->GoPkg;
			if (m->Bindings.n == 0)
				continue;
			add_seen(&im, file, pkg);
		}
	}
	param params = {
		.File = file,
		.AliasedPkg = aliasedPkg,
		.Imports = im.v,
		.nImports = im.n,
		.UseRequestContext = g->useRequestContext,
		.RegisterFuncSuffix = g->registerFuncSuffix,
		.AllowPatchFeature = g->allowPatchFeature,
		.UseOpaqueAPI = g->useOpaqueAPI,
		.OmitPackageDoc = g->reg->omitPackageDoc,
		.StrictDeepWildcard = g->reg->strictDeepWildcard,
	};
	bool noTarget;
	return applyTemplate(params, g->reg, &noTarget);
}

/* format.Source; a parse error fails with its text. */
static const char *format_source(const char *code)
{
	Buf out = {0};
	const char *err = go_std_format_source(code, strlen(code), &out);
	if (err)
		fail("%s", err);
	return buf_str(&out);
}

/* Generate: the files of the targets (an error fails). */
static ResponseFile *gengateway_Generate(generator *g, GwFile **targets, int ntargets, int *nfiles)
{
	ResponseFile *files = xalloc(sizeof *files * (size_t)(2 * ntargets + 1));
	int n = 0;
	for (int i = 0; i < ntargets; i++) {
		GwFile *file = targets[i];
		const char *code = gengateway_generate(g, file, NULL);
		if (!code)
			continue; /* errNoTargetService */
		const char *formatted = format_source(code);
		if (!g->separatePackage) {
			files[n++] = (ResponseFile){xcat(file->GeneratedFilenamePrefix, ".pb.gw.go"), file->GoPkg.Path, formatted};
			continue;
		}
		GwGoPackage goPkg = {pg_path_join(file->GoPkg.Path, file->GoPkg.Name), file->GoPkg.Name, ""};
		const char *prefix = file->GeneratedFilenamePrefix;
		const char *fileNamePrefix = pg_path_join(pg_path_join(pg_path_dir(prefix), file->GoPkg.Name), pg_path_base(prefix));
		files[n++] = (ResponseFile){xcat(fileNamePrefix, ".pb.gw.go"), goPkg.Path, formatted};
		/* There was a bug where we include an extra path element (the filename), resulting in a
		 * stuttering import path. Fixing this bug cannot involve removing the Go file generated
		 * at the wrong path, because that would be a breaking change.
		 *
		 * Instead, we generate the same file both at the right path and at the wrong path,
		 * marking the file (its package) at the wrong path as deprecated.
		 *
		 * If gateway has a new major version, we should then stop generating at the wrong path. */
		GwGoPackage *aliasedPackage = xalloc(sizeof *aliasedPackage);
		/* When generating for generated SDK, the original goPkg points to code generated by
		 * "protocolbuffers/go", but we are aliasing to a package generated by
		 * "grpc-ecosystem/gateway". */
		const char *path = goPkg.Path, *hit = strstr(path, "/" GW_BaseTypePackageSubPath);
		if (hit)
			path = xcat(xstrndup(path, (size_t)(hit - path)), "/" GW_GatewayPackageSubPath,
				    hit + strlen("/" GW_BaseTypePackageSubPath));
		*aliasedPackage = (GwGoPackage){path, goPkg.Name, "gateway"};
		code = gengateway_generate(g, file, aliasedPackage);
		if (!code)
			continue;
		formatted = format_source(code);
		/* The prefix is incorrect, but we are still generating it for backwards compatibility. */
		fileNamePrefix = pg_path_join(pg_path_join(prefix, file->GoPkg.Name), pg_path_base(prefix));
		files[n++] = (ResponseFile){xcat(fileNamePrefix, ".pb.gw.go"), goPkg.Path, formatted};
	}
	*nfiles = n;
	return files;
}
