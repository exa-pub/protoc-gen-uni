/* MIT License
 *
 * Copyright (c) 2022 SpiralScout
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE. */

/* Ported from protoc_plugins/protoc-gen-php-grpc/php/ns.go (roadrunner-server/grpc v6.0.0). */

/* A Go map[string]string (keys may contain any byte). */
typedef struct {
	Str *keys, *vals;
	size_t cap, n;
} StrMap;

static uint64_t strmap_hash(Str k)
{
	uint64_t h = UINT64_C(14695981039346656037); /* FNV-1a */
	for (size_t i = 0; i < k.n; i++)
		h = (h ^ (unsigned char)k.p[i]) * UINT64_C(1099511628211);
	return h;
}

static Str *strmap_get(const StrMap *m, Str k)
{
	if (!m->cap)
		return NULL;
	for (size_t i = strmap_hash(k) & (m->cap - 1);; i = (i + 1) & (m->cap - 1)) {
		if (!m->keys[i].p)
			return NULL;
		if (str_equal(m->keys[i], k))
			return &m->vals[i];
	}
}

static void strmap_put(StrMap *m, Str k, Str v)
{
	Str *old = strmap_get(m, k);
	if (old) {
		*old = v;
		return;
	}
	if (2 * (m->n + 1) > m->cap) {
		StrMap g = {xalloc(sizeof(Str) * (m->cap ? 2 * m->cap : 16)), NULL, m->cap ? 2 * m->cap : 16, 0};
		g.vals = xalloc(sizeof(Str) * g.cap);
		for (size_t i = 0; i < m->cap; i++)
			if (m->keys[i].p)
				strmap_put(&g, m->keys[i], m->vals[i]);
		*m = g;
	}
	size_t i = strmap_hash(k) & (m->cap - 1);
	while (m->keys[i].p)
		i = (i + 1) & (m->cap - 1);
	m->keys[i] = k.p ? k : STR_LIT(""); /* p != NULL marks a used slot */
	m->vals[i] = v;
	m->n++;
}

/* The request as the plugin sees it: per "." + package, the php_namespace option of the first
 * file of that package that sets it. registerMessageNamespace looks it up by scanning
 * req.ProtoFile for every input and output type; the table gives the same answer in constant
 * time. */
typedef struct {
	const PbMsg *req;
	StrMap phpNamespaces;
} Request;

enum { FILE_OPTIONS_PHP_NAMESPACE = 41 };

/* Go *string of an optional string field: p == NULL if unset. */
static Str opt_str(const PbMsg *m, uint32_t num)
{
	return pb_has(m, num) ? pb_bytes(m, num) : (Str){NULL, 0};
}

/* deref */
static Str deref(Str s) { return s.p ? s : STR_LIT(""); }

/* file.Options.PhpNamespace (NULL p if file.Options or the option is unset) */
static Str php_namespace(const PbMsg *file)
{
	return opt_str(pb_msg(file, FDP_OPTIONS), FILE_OPTIONS_PHP_NAMESPACE);
}

static Str dot_package(Str pkg)
{
	Buf b = {0};
	buf_addc(&b, '.');
	buf_add_str(&b, deref(pkg));
	return buf_view(&b);
}

static COLD void request_init(Request *r, const PbMsg *req)
{
	r->req = req;
	for (int i = 0; i < pb_count(req, CGR_PROTO_FILE); i++) {
		const PbMsg *f = pb_msg_at(req, CGR_PROTO_FILE, i);
		Str ns = php_namespace(f);
		Str key = dot_package(opt_str(f, FDP_PACKAGE));
		if (ns.p && !strmap_get(&r->phpNamespaces, key))
			strmap_put(&r->phpNamespaces, key, ns);
	}
}

/* manages internal name representation of the package */
typedef struct {
	/* Package defines file package. */
	Str Package;
	/* Root namespace of the package */
	Str Namespace;
	/* Declares the namespaces of messages used as input and output types for service methods */
	StrMap MessageNamespaces;
} ns;

/* The package of a message type name: strings.Join(chunks[:len(chunks)-1], "."), and its last
 * chunk. */
static Str type_package(Str msg, Str *last)
{
	const char *dot = NULL;
	for (size_t i = msg.n; i-- > 0;)
		if (msg.p[i] == '.') {
			dot = msg.p + i;
			break;
		}
	if (!dot) {
		*last = msg;
		return STR_LIT("");
	}
	*last = (Str){dot + 1, msg.n - (size_t)(dot + 1 - msg.p)};
	return (Str){msg.p, (size_t)(dot - msg.p)};
}

/* pkg == "." + ns.Package */
static bool is_root(const ns *n, Str pkg)
{
	return pkg.n == n->Package.n + 1 && pkg.p[0] == '.' && !memcmp(pkg.p + 1, n->Package.p, n->Package.n);
}

/* Registers the namespace of a message used as an input or output type for a service method. */
static void registerMessageNamespace(ns *n, const Request *req, Str msg)
{
	if (!msg.p)
		return;
	Str last;
	Str pkg = type_package(msg, &last);
	if (is_root(n, pkg))
		return; /* root package */

	Str *custom = strmap_get(&req->phpNamespaces, pkg);
	if (custom) {
		/* custom message namespace */
		strmap_put(&n->MessageNamespaces, pkg, *custom);
		return;
	}

	Buf result = {0};
	if (last.p != msg.p) { /* for _, p := range chunks[:len(chunks)-1] */
		for (size_t i = 0;; i++) {
			const char *dot = memchr(msg.p + i, '.', pkg.n - i);
			size_t end = dot ? (size_t)(dot - msg.p) : pkg.n;
			identifier(&result, (Str){msg.p + i, end - i}, "");
			buf_addc(&result, '\\');
			if (!dot)
				break;
			i = end;
		}
	}
	strmap_put(&n->MessageNamespaces, pkg, trim_byte(buf_view(&result), '\\'));
}

/* newNamespace creates new work namespace. */
static ns *newNamespace(const Request *req, const PbMsg *file, const PbMsg *service)
{
	ns *n = xalloc(sizeof *n);
	Str pkg = opt_str(file, FDP_PACKAGE);
	n->Package = deref(pkg);
	n->Namespace = namespace(pkg, '\\');
	Str phpNs = php_namespace(file);
	if (phpNs.p)
		n->Namespace = phpNs;
	for (int k = 0; k < pb_count(service, SDP_METHOD); k++) {
		const PbMsg *m = pb_msg_at(service, SDP_METHOD, k);
		registerMessageNamespace(n, req, opt_str(m, MDP_INPUT_TYPE));
		registerMessageNamespace(n, req, opt_str(m, MDP_OUTPUT_TYPE));
	}
	return n;
}

/* resolve message alias (msg is not nil) */
static Str resolve(const ns *n, Str msg)
{
	Str last;
	Str pkg = type_package(msg, &last);
	if (is_root(n, pkg))
		return identifier_s(last, ""); /* root message */

	Str *nsv = strmap_get(&n->MessageNamespaces, pkg);
	if (nsv) {
		/* use last namespace chunk */
		Str ident = identifier_s(last, "");
		Buf b = {0};
		buf_addc(&b, '\\');
		buf_add_str(&b, *nsv);
		buf_addc(&b, '\\');
		buf_add_str(&b, resolveReserved(ident, pkg));
		return buf_view(&b);
	}

	/* fully clarified name (fallback) */
	Buf b = {0};
	buf_addc(&b, '\\');
	buf_add_str(&b, namespace(msg, '\\'));
	return buf_view(&b);
}
