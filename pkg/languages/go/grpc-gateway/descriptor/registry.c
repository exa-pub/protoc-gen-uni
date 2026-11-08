/* Copyright (c) 2015, Gengo, Inc. All rights reserved. BSD-3-Clause (see LICENSE of
 * github.com/grpc-ecosystem/grpc-gateway). */

/* Port of grpc-gateway v2.33.0 internal/descriptor/registry.go (the parts the plugins use), with
 * the changes of bufbuild/plugins' separate_pkg_additional_imports.patch (separatePackage). */
#include "internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char gw_unsupported_title[] = "\x01unsupported";

const char *gw_alias(const char *alias)
{
	if (alias == gw_unsupported_title)
		fail("unsupported by protoc-gen-uni: standalone=true with a Go package name that is not ASCII "
		     "(the title case of golang.org/x/text for its alias is not ported)");
	return alias;
}

/* NewRegistry returns a new Registry. */
GwRegistry *gw_NewRegistry(void)
{
	GwRegistry *r = xalloc(sizeof *r);
	r->repeatedPathParamSeparatorName = STR_LIT("csv");
	r->repeatedPathParamSeparator = ',';
	r->recursiveDepth = 1000;
	r->openAPINamingStrategy = "legacy";
	return r;
}

static int cmp_paths(const void *a, const void *b) { return strcmp(*(const char *const *)a, *(const char *const *)b); }

static void registerEnum(GwRegistry *r, GwFile *file, const char **outerPath, int nOuter, const PbMsg *parent, uint32_t field)
{
	for (int i = 0; i < pb_count(parent, field); i++) {
		GwEnum *e = xalloc(sizeof *e);
		e->proto = pb_msg_at(parent, field, i);
		e->Name = pb_str(e->proto, EDP_NAME);
		e->File = file;
		e->Outers = outerPath;
		e->nOuters = nOuter;
		e->Index = i;
		e->ForcePrefixedName = r->standalone;
		vec_push(&file->Enums, e);
		map_put(&r->enums, gw_Enum_FQEN(e), e);
	}
}

static void registerMsg(GwRegistry *r, GwFile *file, const char **outerPath, int nOuter, const PbMsg *parent, uint32_t field)
{
	for (int i = 0; i < pb_count(parent, field); i++) {
		GwMessage *m = xalloc(sizeof *m);
		m->proto = pb_msg_at(parent, field, i);
		m->Name = pb_str(m->proto, DP_NAME);
		m->File = file;
		m->Outers = outerPath;
		m->nOuters = nOuter;
		m->Index = i;
		m->ForcePrefixedName = r->standalone;
		for (int k = 0; k < pb_count(m->proto, DP_FIELD); k++) {
			PbMsg *fd = pb_msg_at(m->proto, DP_FIELD, k);
			GwField *f = xalloc(sizeof *f);
			f->proto = fd;
			f->Name = pb_str(fd, FD_NAME);
			f->Type = (int)pb_int(fd, FD_TYPE, 0);
			f->Label = (int)pb_int(fd, FD_LABEL, 0);
			f->TypeName = pb_str(fd, FD_TYPE_NAME);
			f->HasOneofIndex = pb_has(fd, FD_ONEOF_INDEX);
			f->OneofIndex = (int)pb_int(fd, FD_ONEOF_INDEX, 0);
			f->Proto3Optional = pb_int(fd, FD_PROTO3_OPTIONAL, 0) != 0;
			f->JsonName = pb_str(fd, FD_JSON_NAME);
			f->Message = m;
			f->ForcePrefixedName = r->standalone;
			vec_push(&m->Fields, f);
		}
		vec_push(&file->Messages, m);
		map_put(&r->msgs, gw_Message_FQMN(m), m);

		const char **outers = xalloc(sizeof *outers * (size_t)(nOuter + 1));
		if (nOuter)
			memcpy(outers, outerPath, sizeof *outers * (size_t)nOuter);
		outers[nOuter] = m->Name;
		registerMsg(r, file, outers, nOuter + 1, m->proto, DP_NESTED_TYPE);
		registerEnum(r, file, outers, nOuter + 1, m->proto, DP_ENUM_TYPE);
	}
}

/* Title of x/text cases for an ASCII name: the first letter of the word upper case, the other
 * letters lower case (digits and '_' do not break words); NULL if the name is not ASCII. */
static const char *title_ascii(const char *s)
{
	size_t n = strlen(s);
	char *t = xalloc(n + 1);
	bool mid = false;
	for (size_t i = 0; i < n; i++) {
		unsigned char c = (unsigned char)s[i];
		if (c >= 0x80)
			return NULL;
		bool upper = c >= 'A' && c <= 'Z', lower = c >= 'a' && c <= 'z';
		if (upper || lower) {
			if (!mid) {
				c = (unsigned char)(lower ? c - 32 : c);
				mid = true;
			} else if (upper) {
				c = (unsigned char)(c + 32);
			}
		} else if (!((c >= '0' && c <= '9') || c == '_')) {
			return NULL; /* not in a Go package name: word breaking not ported */
		}
		t[i] = (char)c;
	}
	return t;
}

/* ReserveGoPackageAlias(name_i) for i = 0, 1, ...; the alias that succeeded. The candidates are
 * built in one buffer (only the reserved one is kept). */
static const char *reserve_numbered(GwRegistry *r, const char *name, const char *pkgpath)
{
	size_t n = strlen(name);
	char *buf = xalloc(n + 24);
	for (long i = 0;; i++) {
		int k = snprintf(buf, n + 24, "%s_%ld", name, i);
		const char *taken = map_get(&r->pkgAliases, buf);
		if (!taken) {
			char *alias = xstrndup(buf, (size_t)k);
			map_put(&r->pkgAliases, alias, (void *)pkgpath);
			return alias;
		}
		if (strcmp(taken, pkgpath) == 0)
			return xstrndup(buf, (size_t)k);
	}
}

/* loadFile loads messages, enumerations and fields from "file". It does not load services and
 * methods in "file". You need to call loadServices after loadFiles is called for all files to
 * load services and methods. */
static void loadFile(GwRegistry *r, const char *filePath, PgFile *file)
{
	GwGoPackage pkg = {file->GoImportPath, file->GoPackageName, ""};
	if (r->standalone) {
		const char *t = title_ascii(pkg.Name);
		pkg.Alias = t ? xcat("ext", t) : gw_unsupported_title;
	}

	if (r->separatePackage)
		pkg.Name = xcat(pkg.Name, "gateway");
	if (!gw_Registry_ReserveGoPackageAlias(r, pkg.Name, pkg.Path))
		pkg.Alias = reserve_numbered(r, pkg.Name, pkg.Path);
	GwFile *f = xalloc(sizeof *f);
	f->proto = file->Proto;
	f->name = pb_str(file->Proto, FDP_NAME);
	f->package = pb_str(file->Proto, FDP_PACKAGE);
	f->has_package = pb_has(file->Proto, FDP_PACKAGE);
	f->proto2 = !pb_has(file->Proto, FDP_SYNTAX) || strcmp(pb_str(file->Proto, FDP_SYNTAX), "proto2") == 0;
	f->GoPkg = pkg;
	f->GeneratedFilenamePrefix = file->GeneratedFilenamePrefix;

	map_put(&r->files, filePath, f);
	registerMsg(r, f, NULL, 0, file->Proto, FDP_MESSAGE_TYPE);
	registerEnum(r, f, NULL, 0, file->Proto, FDP_ENUM_TYPE);
}

Str gw_Registry_LoadFromPlugin(GwRegistry *r, PgPlugin *gen)
{
	const char **filePaths = xalloc(sizeof *filePaths * (size_t)(gen->nFiles + 1));
	PgFile **byPath = xalloc(sizeof *byPath * (size_t)(gen->nFiles + 1));
	int n = 0;
	for (int i = 0; i < gen->nFiles; i++)
		filePaths[n++] = pb_str(gen->Files[i]->Proto, FDP_NAME);
	qsort(filePaths, (size_t)n, sizeof *filePaths, cmp_paths);
	for (int i = 0; i < n; i++)
		byPath[i] = map_get(&gen->FilesByPath, filePaths[i]);

	for (int i = 0; i < n; i++)
		loadFile(r, filePaths[i], byPath[i]);

	for (int i = 0; i < n; i++) {
		if (!byPath[i]->Generate)
			continue;
		GwFile *file = map_get(&r->files, filePaths[i]);
		Str err = gw_Registry_loadServices(r, file);
		if (err.p)
			return err;
	}
	return (Str){NULL, 0};
}

static const char *lookup(Map *m, const char *location, const char *name, const char *what, const char **err, void **out)
{
	*out = NULL;
	if (name[0] == '.') {
		*out = map_get(m, name);
		if (!*out)
			*err = xfmt("no %s found: %s", what, name);
		return NULL;
	}

	if (location[0] != '.')
		location = xcat(".", location);
	/* components := strings.Split(location, "."); while non-empty: join(components + name) */
	size_t len = strlen(location);
	for (;;) {
		const char *fq = xcat(xstrndup(location, len), ".", name);
		if ((*out = map_get(m, fq)))
			return NULL;
		/* drop the last component; the first one ("" before the leading dot) ends the loop */
		const char *dot = NULL;
		for (size_t i = len; i > 0; i--)
			if (location[i - 1] == '.') {
				dot = location + i - 1;
				break;
			}
		if (!dot)
			break;
		len = (size_t)(dot - location);
	}
	*err = xfmt("no %s found: %s", what, name);
	return NULL;
}

/* LookupMsg looks up a message type by "name". It tries to resolve "name" from "location" if
 * "name" is a relative message name. */
GwMessage *gw_Registry_LookupMsg(GwRegistry *r, const char *location, const char *name, const char **err)
{
	void *m;
	lookup(&r->msgs, location, name, "message", err, &m);
	return m;
}

/* LookupEnum looks up an enum type by "name". It tries to resolve "name" from "location" if
 * "name" is a relative enum name. */
GwEnum *gw_Registry_LookupEnum(GwRegistry *r, const char *location, const char *name, const char **err)
{
	void *e;
	lookup(&r->enums, location, name, "enum", err, &e);
	return e;
}

/* LookupFile looks up a file by name. */
GwFile *gw_Registry_LookupFile(GwRegistry *r, const char *name, const char **err)
{
	GwFile *f = map_get(&r->files, name);
	if (!f)
		*err = xcat("no such file given: ", name);
	return f;
}

/* UnboundExternalHTTPRules returns the list of External HTTPRules which does not have a matching
 * method in the registry. */
int gw_Registry_UnboundExternalHTTPRules(GwRegistry *r, const char ***out)
{
	(void)r;
	*out = NULL;
	return 0;
}

/* ReserveGoPackageAlias reserves the unique alias of go package. If succeeded, the alias will be
 * never used for other packages in generated go files. If failed, the alias is already taken by
 * another package, so you need to use another alias for the package in your go files. */
bool gw_Registry_ReserveGoPackageAlias(GwRegistry *r, const char *alias, const char *pkgpath)
{
	const char *taken = map_get(&r->pkgAliases, alias);
	if (taken)
		return strcmp(taken, pkgpath) == 0;
	map_put(&r->pkgAliases, alias, (void *)pkgpath);
	return true;
}

/* SetRepeatedPathParamSeparator sets how path parameter repeated fields are separated. Allowed
 * names are 'csv', 'pipe', 'ssv' and 'tsv'. */
Str gw_Registry_SetRepeatedPathParamSeparator(GwRegistry *r, Str name)
{
	int32_t sep;
	if (str_eqc(name, "csv"))
		sep = ',';
	else if (str_eqc(name, "pipes"))
		sep = '|';
	else if (str_eqc(name, "ssv"))
		sep = ' ';
	else if (str_eqc(name, "tsv"))
		sep = '\t';
	else {
		Buf b = {0};
		buf_adds(&b, "unknown repeated path parameter separator: ");
		buf_add_str(&b, name);
		return buf_view(&b);
	}
	r->repeatedPathParamSeparatorName = name;
	r->repeatedPathParamSeparator = sep;
	return (Str){NULL, 0};
}

/* GetRepeatedPathParamSeparator returns a rune specifying how path parameter repeated fields are
 * separated. */
int32_t gw_Registry_GetRepeatedPathParamSeparator(const GwRegistry *r) { return r->repeatedPathParamSeparator; }

static void hex(Buf *b, Str s)
{
	static const char digits[] = "0123456789abcdef";
	for (size_t i = 0; i < s.n; i++) {
		buf_addc(b, digits[(unsigned char)s.p[i] >> 4]);
		buf_addc(b, digits[(unsigned char)s.p[i] & 15]);
	}
}

Str gw_Registry_CheckDuplicateAnnotation(GwRegistry *r, Str httpMethod, Str httpTemplate, const GwService *svc)
{
	Buf k = {0};
	hex(&k, httpMethod);
	buf_addc(&k, ':');
	hex(&k, httpTemplate);
	buf_fmt(&k, ":%llx", (unsigned long long)(uintptr_t)svc);
	const char *key = buf_str(&k);
	if (map_has(&r->annotationMap, key)) {
		Buf b = {0};
		buf_adds(&b, "duplicate annotation: method=");
		buf_add_str(&b, httpMethod);
		buf_adds(&b, ", template=");
		buf_add_str(&b, httpTemplate);
		return buf_view(&b);
	}
	map_put(&r->annotationMap, key, (void *)key);
	return (Str){NULL, 0};
}

/* ---- the openapiv2 parts of registry.go ---- */

/* The slot of field num of m (which must be in its definition). */
static PbSlot *slot_of(PbMsg *m, uint32_t num, const PbField **f)
{
	for (int i = 0; i < m->def->n; i++)
		if (m->def->f[i].num == num) {
			*f = &m->def->f[i];
			return &m->slots[i];
		}
	return NULL;
}

/* fillGoPackageOptionIfMissing synthesizes a placeholder go_package option for any proto file in
 * req that doesn't declare one, so protogen.Options.New does not error with "unable to determine
 * Go import path". */
static COLD void fillGoPackageOptionIfMissing(PbMsg *req)
{
	enum { FILE_OPTIONS_GO_PACKAGE = 11 };
	for (int i = 0; i < pb_count(req, CGR_PROTO_FILE); i++) {
		PbMsg *fdesc = pb_msg_at(req, CGR_PROTO_FILE, i);
		if (pb_bytes(pb_msg(fdesc, FDP_OPTIONS), FILE_OPTIONS_GO_PACKAGE).n)
			continue;
		Str pkg = pb_bytes(fdesc, FDP_PACKAGE);
		Buf path = {0};
		buf_adds(&path, "generated.invalid/");
		if (pkg.n) {
			for (size_t k = 0; k < pkg.n; k++)
				buf_addc(&path, pkg.p[k] == '.' ? '/' : pkg.p[k]);
		} else {
			Str name = pb_bytes(fdesc, FDP_NAME);
			Buf b = {0};
			for (size_t k = 0; k < name.n; k++)
				buf_addc(&b, name.p[k] == '/' ? '_' : name.p[k]);
			buf_add_str(&path, str_trim_suffix_s(buf_view(&b), STR_LIT(".proto")));
		}
		const PbField *f;
		PbSlot *s = slot_of(fdesc, FDP_OPTIONS, &f);
		if (!s->n) {
			s->v = xalloc(sizeof *s->v);
			s->n = s->cap = 1;
			s->v[0].m = pb_new(f->msg);
		}
		pb_set_bytes(s->v[0].m, FILE_OPTIONS_GO_PACKAGE, buf_view(&path));
	}
}

COLD Str gw_Registry_Load(GwRegistry *r, PbMsg *req, const PgOptions *opts)
{
	if (r->ignoreGoPackageOption)
		fillGoPackageOptionIfMissing(req);
	Str err;
	PgPlugin *gen = pg_options_new(opts, req, &err);
	if (!gen)
		return err;
	/* Note: keep in mind that this might be not enough because protogen.Plugin is used only to
	 * load files here. The support for features must be set on the
	 * pluginpb.CodeGeneratorResponse. */
	gw_codegenerator_SetSupportedFeaturesOnPluginGen(gen);
	return gw_Registry_LoadFromPlugin(r, gen);
}

static int map_keys(const Map *m, const char ***out)
{
	const char **keys = xalloc(sizeof *keys * (m->n + 1));
	int n = 0;
	for (size_t i = 0; i < m->cap; i++)
		if (m->keys[i])
			keys[n++] = m->keys[i];
	*out = keys;
	return n;
}

int gw_Registry_GetAllFQMNs(const GwRegistry *r, const char ***out) { return map_keys(&r->msgs, out); }
int gw_Registry_GetAllFQENs(const GwRegistry *r, const char ***out) { return map_keys(&r->enums, out); }
int gw_Registry_GetAllFQMethNs(const GwRegistry *r, const char ***out) { return map_keys(&r->meths, out); }

static int str_cmp(Str a, Str b)
{
	size_t n = a.n < b.n ? a.n : b.n;
	int c = n ? memcmp(a.p, b.p, n) : 0;
	return c ? c : (a.n > b.n) - (a.n < b.n);
}

static int cmp_str(const void *a, const void *b) { return str_cmp(*(const Str *)a, *(const Str *)b); }

/* The selectors are kept sorted, without duplicates (a Go map[string]bool). */
COLD void gw_Registry_SetVisibilityRestrictionSelectors(GwRegistry *r, const Str *selectors, int n)
{
	Str *v = xalloc(sizeof(Str) * (size_t)(n + 1));
	for (int i = 0; i < n; i++)
		v[i] = go_trim_space(selectors[i]);
	qsort(v, (size_t)n, sizeof *v, cmp_str);
	int k = 0;
	for (int i = 0; i < n; i++)
		if (k == 0 || !str_equal(v[k - 1], v[i]))
			v[k++] = v[i];
	r->visibilityRestrictionSelectors = v;
	r->nVisibilityRestrictionSelectors = k;
}

bool gw_Registry_IsVisibilityRestrictionSelector(const GwRegistry *r, Str selector)
{
	return r->nVisibilityRestrictionSelectors &&
	       bsearch(&selector, r->visibilityRestrictionSelectors, (size_t)r->nVisibilityRestrictionSelectors, sizeof(Str),
		       cmp_str);
}

const char *gw_Registry_FieldName(const GwRegistry *r, const GwField *f)
{
	return r->useJSONNamesForFields ? f->JsonName : f->Name;
}
