// Copyright (c) 2015, Gengo, Inc. All rights reserved. BSD-3-Clause (see LICENSE of
// github.com/grpc-ecosystem/grpc-gateway).

/* Port of protoc-gen-openapiv2/internal/genopenapi/naming.go. The naming strategy functions take
 * the fully-qualified names and return the mapping (a Map from fully-qualified name to OpenAPI
 * name). Included by generator.c. */

typedef Map *(*namingStrategyFn)(const char **messages, int n);

static Map *resolveNamesFQN(const char **messages, int n);
static Map *resolveNamesLegacy(const char **messages, int n);
static Map *resolveNamesSimple(const char **messages, int n);
static Map *resolveNamesPackage(const char **messages, int n);

static void put_rune(Buf *b, uint32_t r)
{
	char u[4];
	int k = 0;
	if (r < 0x80)
		u[k++] = (char)r;
	else if (r < 0x800)
		u[k++] = (char)(0xC0 | r >> 6), u[k++] = (char)(0x80 | (r & 0x3F));
	else if (r < 0x10000)
		u[k++] = (char)(0xE0 | r >> 12), u[k++] = (char)(0x80 | ((r >> 6) & 0x3F)), u[k++] = (char)(0x80 | (r & 0x3F));
	else
		u[k++] = (char)(0xF0 | r >> 18), u[k++] = (char)(0x80 | ((r >> 12) & 0x3F)), u[k++] = (char)(0x80 | ((r >> 6) & 0x3F)),
		u[k++] = (char)(0x80 | (r & 0x3F));
	buf_add(b, u, (size_t)k);
}

/* strings.ToLower (invalid UTF-8 becomes U+FFFD) */
static Str to_lower(Str s)
{
	Buf b = {0};
	for (size_t i = 0; i < s.n;) {
		uint32_t r;
		int w = utf8_decode(s.p + i, s.n - i, &r);
		if (r < 0x80)
			buf_addc(&b, (char)(r >= 'A' && r <= 'Z' ? r + 32 : r));
		else
			put_rune(&b, r == 0xFFFD ? r : (uint32_t)go_std_to_lower((int32_t)r));
		i += (size_t)w;
	}
	return (Str){b.p ? b.p : "", b.n};
}

/* LookupNamingStrategy looks up the given naming strategy and returns the naming strategy
 * function for it. */
static namingStrategyFn LookupNamingStrategy(Str strategyName)
{
	Str s = to_lower(strategyName);
	if (str_eqc(s, "fqn"))
		return resolveNamesFQN;
	if (str_eqc(s, "legacy"))
		return resolveNamesLegacy;
	if (str_eqc(s, "simple"))
		return resolveNamesSimple;
	if (str_eqc(s, "package"))
		return resolveNamesPackage;
	return NULL;
}

/* resolveNamesFQN uses the fully-qualified proto message name as the OpenAPI name, stripping
 * the leading dot. */
static Map *resolveNamesFQN(const char **messages, int n)
{
	Map *uniqueNames = xalloc(sizeof *uniqueNames);
	for (int i = 0; i < n; i++)
		map_put(uniqueNames, messages[i], (void *)(messages[i] + (messages[i][0] ? 1 : 0)));
	return uniqueNames;
}

static Map *resolveNamesUniqueWithContext(const char **messages, int n, int extraContext, const char *componentSeparator,
					  bool qualifyNestedMessages);

/* resolveNamesLegacy: the minimum number of components necessary to yield a unique name, plus
 * one, concatenated with no separator in between ("bCD" for ".a.b.C.D"). */
static Map *resolveNamesLegacy(const char **messages, int n) { return resolveNamesUniqueWithContext(messages, n, 1, "", false); }

/* resolveNamesSimple: the minimum number of components necessary to yield a unique name,
 * joined with "." ("C.D"). */
static Map *resolveNamesSimple(const char **messages, int n) { return resolveNamesUniqueWithContext(messages, n, 0, ".", false); }

/* resolveNamesPackage: the package-scoped name (nested messages qualified by their parents),
 * then package components as needed for uniqueness. */
static Map *resolveNamesPackage(const char **messages, int n) { return resolveNamesUniqueWithContext(messages, n, 0, ".", true); }

typedef struct {
	const char **v;
	int n;
} Parts;

/* hierarchy: the components of a name ("package" convention: the package components, then the
 * message name with its parents as one element, split at the first "\.[A-Z]") */
static Parts hierarchy(const char *pkg, bool qualifyNestedMessages)
{
	Vec parts;
	size_t end = 0;
	bool found = false;
	if (qualifyNestedMessages) {
		/* pkgEndRegexp = `\.[A-Z]` */
		for (const char *p = pkg; *p && !found; p++)
			if (p[0] == '.' && p[1] >= 'A' && p[1] <= 'Z') {
				end = (size_t)(p - pkg);
				found = true;
			}
	}
	if (!found) {
		parts = str_split(pkg, '.');
	} else {
		parts = str_split(xstrndup(pkg, end), '.');
		vec_push(&parts, (void *)(pkg + end + 1));
	}
	return (Parts){(const char **)parts.v, parts.n};
}

/* The suffix h[len(h)-depth:] joined with "\x00" (a key no name contains). */
static const char *suffix_key(Parts h, int depth)
{
	Buf b = {0};
	buf_fmt(&b, "%d", depth);
	for (int i = h.n - depth; i < h.n; i++) {
		buf_addc(&b, '\x01');
		buf_adds(&b, h.v[i]);
	}
	return buf_str(&b);
}

/* Take the names of every proto message and generates a unique reference by: first, separating
 * each message name into its components by splitting at dots. Then, take the shortest suffix
 * slice from each components slice that is unique among all messages, and convert it into a
 * component name by taking extraContext additional components into consideration and joining
 * all components with componentSeparator. (Upstream counts the suffixes with a linear scan per
 * message; a map of counts gives the same result.) */
static Map *resolveNamesUniqueWithContext(const char **messages, int n, int extraContext, const char *componentSeparator,
					  bool qualifyNestedMessages)
{
	Map counts = {0}; /* suffix of a depth -> count */
	Parts *hs = xalloc(sizeof *hs * (size_t)(n + 1));
	for (int i = 0; i < n; i++) {
		hs[i] = hierarchy(messages[i], qualifyNestedMessages);
		for (int depth = 0; depth < hs[i].n; depth++) {
			const char *k = suffix_key(hs[i], depth);
			map_put(&counts, k, (void *)((intptr_t)map_get(&counts, k) + 1));
		}
	}
	Map *uniqueNames = xalloc(sizeof *uniqueNames);
	for (int i = 0; i < n; i++) {
		Parts h = hs[i];
		int depth = 0;
		for (; depth < h.n; depth++) {
			/* depth + extraContext > 0 ensures that we only break for values of depth when
			 * the resulting slice of name components is non-empty. */
			if (depth + extraContext > 0 && (intptr_t)map_get(&counts, suffix_key(h, depth)) == 1)
				break;
		}
		int start = h.n - depth - extraContext;
		if (start < 0)
			start = 0;
		int nc = h.n - start;
		const char **components = xalloc(sizeof *components * (size_t)(nc + 1));
		for (int k = 0; k < nc; k++)
			components[k] = h.v[start + k];
		/* When using empty separator (legacy mode), apply camelCase by title-casing
		 * intermediate lowercase package components ("google.rpc.Status" ->
		 * "googleRpcStatus"), skipping the first non-empty component. The components are
		 * identifiers (protodesc validated them): ASCII. */
		if (componentSeparator[0] == 0 && nc > 1) {
			int firstNonEmpty = -1;
			for (int k = 0; k < nc; k++)
				if (components[k][0]) {
					firstNonEmpty = k;
					break;
				}
			for (int k = firstNonEmpty + 1; k < nc; k++) {
				const char *c = components[k];
				bool lower = c[0] != 0;
				for (const char *p = c; *p; p++)
					if (*p >= 'A' && *p <= 'Z')
						lower = false;
				if (lower) {
					/* strings.ToUpper(c[:1]) + c[1:] */
					char *t = xstrdup(c);
					if (t[0] >= 'a' && t[0] <= 'z')
						t[0] = (char)(t[0] - 32);
					components[k] = t;
				}
			}
		}
		Buf b = {0};
		for (int k = 0; k < nc; k++) {
			if (k)
				buf_adds(&b, componentSeparator);
			buf_adds(&b, components[k]);
		}
		map_put(uniqueNames, messages[i], (void *)buf_str(&b));
	}
	return uniqueNames;
}
