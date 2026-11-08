/* Copyright (c) 2011 Petteri Aimonen <jpa at nanopb.mail.kapsi.fi>
 *
 * This software is provided 'as-is', without any express or
 * implied warranty. In no event will the authors be held liable
 * for any damages arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any
 * purpose, including commercial applications, and to alter it and
 * redistribute it freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you
 *    must not claim that you wrote the original software. If you use
 *    this software in a product, an acknowledgment in the product
 *    documentation would be appreciated but is not required.
 *
 * 2. Altered source versions must be plainly marked as such, and
 *    must not be misrepresented as being the original software.
 *
 * 3. This notice may not be removed or altered from any source
 *    distribution. */

/* nanopb_generator.py: Message.default_value, MangleNames, ProtoFile (parsing, dependencies,
 * generate_header, generate_source), parse_file and process_file. */

/* ---- an ordered dict str -> PyVal (MangleNames.reverse_name_mapping) ---- */

typedef struct {
	Map index; /* key -> 1 + position */
	Vec keys;
	PyVal *vals;
	int cap;
} ODict;

static PyVal *odict_get(ODict *d, const char *key)
{
	intptr_t i = (intptr_t)map_get(&d->index, key);
	return i ? &d->vals[i - 1] : NULL;
}

static void odict_put(ODict *d, const char *key, PyVal v)
{
	PyVal *p = odict_get(d, key);
	if (p) {
		*p = v;
		return;
	}
	if (d->keys.n == d->cap) {
		int c = d->cap ? d->cap * 2 : 16;
		d->vals = xrealloc(d->vals, sizeof *d->vals * (size_t)d->cap, sizeof *d->vals * (size_t)c);
		d->cap = c;
	}
	vec_push(&d->keys, (void *)key);
	d->vals[d->keys.n - 1] = v;
	map_put(&d->index, key, (void *)(intptr_t)d->keys.n);
}

/* ---- MangleNames ---- */

typedef struct {
	NbOpts *file_options;
	int64_t mangle_names;
	bool flatten;
	const char *strip_prefix, *replacement_prefix; /* NULL: None */
	Map name_mapping; /* str -> Names* */
	ODict reverse;
	Names canonical_base, base_name;
} MangleNames;

/* The state of a file in the import walk number gen of process_file (files.c). */
typedef struct {
	int gen, state, next, depth, rank, key_rank, key_index;
	bool fixed;
	int64_t paths;
} WalkNode;

struct ProtoFile {
	WalkNode walk;
	ProtoFile **deps_pf; /* the imports in other_files (files.c) */
	FileDesc *fdesc;
	NbOpts *file_options;
	Map dependencies; /* str -> Enum* or Message* */
	bool math_include_required;
	Vec enums, messages, extensions;
	MangleNames mn;
	const char *optfilename;
	Map msgmap; /* str(name) -> the last Message of that name, for check_recursive */
	/* what add_dependency(other) reads of this file as other, built once by dep_cache */
	bool dc_have;
	const char **dc_keys; /* str(names) and str(unmangle(names)) of each enum, then message */
	Map dc_canon, dc_longnames, dc_nonneg;
};

static COLD void mangle_init(MangleNames *mn, const FileDesc *fd, NbOpts *fo)
{
	mn->file_options = fo;
	mn->mangle_names = opt(fo, NO_MANGLE_NAMES);
	mn->flatten = mn->mangle_names == M_FLATTEN;
	mn->canonical_base = names_split(fd->package, '.');
	if (mn->mangle_names == M_STRIP_PACKAGE) {
		mn->strip_prefix = xcat(".", fd->package);
	} else if (mn->mangle_names == M_PACKAGE_INITIALS) {
		mn->strip_prefix = xcat(".", fd->package);
		Buf b = {0};
		Vec parts = str_split(fd->package, '.');
		for (int i = 0; i < parts.n; i++) {
			const char *p = VEC_AT(parts, char, i);
			if (!*p)
				nb_raise(S_INITIALS, "IndexError: string index out of range\n");
			uint32_t r;
			int w = utf8_decode(p, strlen(p), &r);
			buf_add(&b, p, (size_t)w);
		}
		mn->replacement_prefix = buf_str(&b);
	} else if (*fo->s[NO_PACKAGE]) {
		mn->strip_prefix = xcat(".", fd->package);
		mn->replacement_prefix = fo->s[NO_PACKAGE];
	}
	if (mn->strip_prefix && str_eq(mn->strip_prefix, "."))
		mn->strip_prefix = "";
	if (mn->replacement_prefix)
		mn->base_name = names_split(mn->replacement_prefix, '.');
	else if (*fd->package)
		mn->base_name = names_split(fd->package, '.');
	else
		mn->base_name = names_make(NULL, 0);
}

/* create_name(names): is_str when the argument is a str (a top-level enum name). */
static Names create_name(MangleNames *mn, Names names, const char *str_arg)
{
	const char *key = str_arg ? str_arg : names_str(names);
	Names *found = map_get(&mn->name_mapping, key);
	if (found)
		return *found;
	Names nn;
	if (mn->mangle_names == M_NONE || mn->mangle_names == M_PACKAGE_INITIALS)
		nn = str_arg ? names_add(mn->base_name, str_arg) : names_cat(mn->base_name, names);
	else if (mn->mangle_names == M_STRIP_PACKAGE)
		nn = str_arg ? names_of(str_arg) : names;
	else if (!str_arg) {
		if (!names.n)
			fail("IndexError: tuple index out of range");
		nn = names_of(names.p[names.n - 1]);
	} else
		nn = names_of(str_arg);
	const char *nk = names_str(nn);
	PyVal *prev = odict_get(&mn->reverse, nk);
	if (prev)
		nb_log(xfmt("Warning: Duplicate name with mangle_names=%d: %s and %s map to %s\n", (int)mn->mangle_names,
			pv_text(*prev), key, nk));
	Names *stored = xalloc(sizeof *stored);
	*stored = nn;
	map_put(&mn->name_mapping, key, stored);
	odict_put(&mn->reverse, nk,
		pv_names(str_arg ? names_add(mn->canonical_base, str_arg) : names_cat(mn->canonical_base, names)));
	return nn;
}

static const char *strip_dots(const char *s)
{
	while (*s == '.')
		s++;
	size_t n = strlen(s);
	while (n && s[n - 1] == '.')
		n--;
	return xstrndup(s, n);
}

static const char *mangle_field_typename(MangleNames *mn, const char *typename)
{
	if (mn->mangle_names == M_FLATTEN) {
		const char *dot = strrchr(typename, '.');
		return xcat(".", dot ? dot + 1 : typename);
	}
	const char *canon = names_str(names_split(strip_dots(typename), '.'));
	if (!has_prefix(canon, xcat(names_str(mn->canonical_base), "_")))
		return typename;
	if (mn->strip_prefix && has_prefix(typename, mn->strip_prefix)) {
		const char *rest = typename + strlen(mn->strip_prefix);
		return mn->replacement_prefix ? xcat(".", mn->replacement_prefix, rest) : rest;
	}
	if (*mn->file_options->s[NO_PACKAGE]) {
		if (!mn->replacement_prefix)
			fail("TypeError: can only concatenate str (not \"NoneType\") to str");
		return xcat(".", mn->replacement_prefix, typename);
	}
	return typename;
}

static PyVal mangle_unmangle(ProtoFile *pf, Names names)
{
	PyVal *v = odict_get(&pf->mn.reverse, names_str(names));
	return v ? *v : pv_names(names);
}

static void reverse_put(ProtoFile *pf, const char *key, PyVal v) { odict_put(&pf->mn.reverse, key, v); }

/* ---- Message.default_value ---- */

static bool hexval(char c, int *v)
{
	if (c >= '0' && c <= '9')
		*v = c - '0';
	else if (c >= 'a' && c <= 'f')
		*v = c - 'a' + 10;
	else if (c >= 'A' && c <= 'F')
		*v = c - 'A' + 10;
	else
		return false;
	return true;
}

/* codecs.escape_decode(s)[0] (Objects/bytesobject.c _PyBytes_DecodeEscape2): invalid escapes
 * are kept (with a DeprecationWarning that is not shown), octal escapes above \377 keep their low
 * 8 bits. site: the frame of the calling line. */
static COLD Str escape_decode_at(const char *s, const char *site)
{
	Buf b = {0};
	size_t n = strlen(s);
	for (size_t i = 0; i < n;) {
		if (s[i] != '\\') {
			buf_addc(&b, s[i++]);
			continue;
		}
		i++;
		if (i >= n)
			nb_raise(site, "ValueError: Trailing \\ in string\n");
		char c = s[i++];
		switch (c) {
		case '\n': break;
		case '\\': buf_addc(&b, '\\'); break;
		case '\'': buf_addc(&b, '\''); break;
		case '"': buf_addc(&b, '"'); break;
		case 'b': buf_addc(&b, '\b'); break;
		case 'f': buf_addc(&b, '\014'); break;
		case 't': buf_addc(&b, '\t'); break;
		case 'n': buf_addc(&b, '\n'); break;
		case 'r': buf_addc(&b, '\r'); break;
		case 'v': buf_addc(&b, '\013'); break;
		case 'a': buf_addc(&b, '\007'); break;
		case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7': {
			int v = c - '0';
			if (i < n && s[i] >= '0' && s[i] <= '7') {
				v = v * 8 + (s[i++] - '0');
				if (i < n && s[i] >= '0' && s[i] <= '7')
					v = v * 8 + (s[i++] - '0');
			}
			buf_addc(&b, (char)(v & 0xff));
			break;
		}
		case 'x': {
			int h, l;
			if (i + 1 < n && hexval(s[i], &h) && hexval(s[i + 1], &l)) {
				buf_addc(&b, (char)(h * 16 + l));
				i += 2;
				break;
			}
			nb_raise(site, xfmt("ValueError: invalid \\x escape at position %d\n", (int)(i - 2)));
		}
		default:
			buf_addc(&b, '\\');
			i--;
		}
	}
	return buf_view(&b);
}


static void put_varint(Buf *b, uint64_t v)
{
	while (v >= 0x80) {
		buf_addc(b, (char)(v | 0x80));
		v >>= 7;
	}
	buf_addc(b, (char)v);
}

static void put_fixed(Buf *b, uint64_t v, int n)
{
	for (int i = 0; i < n; i++)
		buf_addc(b, (char)(v >> (8 * i)));
}

/* int(text) within the range of the field type (the type checker of python-protobuf). */
static i128 default_int(const char *text, i128 lo, i128 hi)
{
	char *repr;
	PyNum r = py_parse_int(str_c(text), &repr);
	if (r == PY_NUM_ERROR)
		nb_raise(S_DEFVAL_INT, xcat("ValueError: invalid literal for int() with base 10: ", py_repr_str(str_c(text)), "\n"));
	if (r != PY_NUM_OK)
		fail("unsupported by protoc-gen-uni: the default value %s", py_repr_str(str_c(text)));
	i128 v = 0;
	const char *p = repr;
	bool neg = *p == '-';
	if (neg)
		p++;
	for (; *p; p++) {
		v = v * 10 + (*p - '0');
		if (v > ((i128)1 << 100))
			nb_raise(S_DEFVAL_RANGE, xcat("ValueError: Value out of range: ", repr, "\n"));
	}
	if (neg)
		v = -v;
	if (v < lo || v > hi)
		nb_raise(S_DEFVAL_RANGE, xcat("ValueError: Value out of range: ", repr, "\n"));
	return v;
}

static double default_float(const char *text)
{
	double d;
	PyNum r = py_parse_float(str_c(text), &d);
	if (r == PY_NUM_ERROR)
		nb_raise(S_DEFVAL_FLOAT, xcat("ValueError: could not convert string to float: ", py_repr_str(str_c(text)), "\n"));
	if (r != PY_NUM_OK)
		fail("unsupported by protoc-gen-uni: the default value %s", py_repr_str(str_c(text)));
	return d;
}

/* The float a float field stores (FloatValueChecker). */
static float to_float(double d)
{
	const double fmax = 3.4028234663852886e38, rounded = 3.4028235677973366e38;
	if (d > fmax)
		return d <= rounded ? (float)fmax : (float)INFINITY;
	if (d < -fmax)
		return d >= -rounded ? (float)-fmax : (float)-INFINITY;
	return (float)d;
}

typedef struct {
	int32_t number, type;
	const char *value;
} DefField;

static int cmp_deffield(const void *a, const void *b)
{
	const DefField *x = a, *y = b;
	return x->number < y->number ? -1 : x->number > y->number;
}

/* default_value: the fields with default values, serialized like SerializeToString() of a
 * message built from a reduced copy of the descriptor (explicit presence, field number order). */
static COLD Str message_default_value(Message *m, Deps *d)
{
	if (!m->desc || m->desc->map_entry)
		return (Str){"", 0};
	DefField **keep = xalloc(sizeof *keep * (size_t)(m->desc->nfields + 1));
	int nk = 0;
	Vec all = all_fields(m);
	Map by_tag = {0}; /* field_for_tag: the first field with the tag */
	for (int k = all.n - 1; k >= 0; k--)
		map_put(&by_tag, xfmt("%lld", (long long)VEC_AT(all, Field, k)->tag), all.v[k]);
	for (int i = m->desc->nfields - 1; i >= 0; i--) {
		FieldDesc *fd = &m->desc->fields[i];
		const Field *parsed = map_get(&by_tag, xfmt("%d", (int)fd->number));
		if (!parsed || parsed->allocation != AL_STATIC)
			continue;
		if (fd->label == LABEL_REPEATED || fd->type == PT_MESSAGE)
			continue;
		if (fd->has_oneof_index)
			continue;
		DefField df = {fd->number, fd->type, fd->default_value};
		if (fd->type == PT_ENUM) {
			Names en = names_from_type_name(fd->type_name);
			const char *ek = names_str(en);
			Enum *et = map_get(d->base, ek);
			if (!et)
				nb_raise_chained(xcat(nb_expand(C_ENUM_KEYERROR), "KeyError: ", py_repr_str(str_c(ek)),
						"\n\nDuring handling of the above exception, another exception occurred:\n\n"), S_ENUM_NOT_FOUND,
					xfmt("Exception: Could not find enum type %s while generating default values for %s.\n"
					     "Try passing all source files to generator at once, or use -I option.\n", ek,
						names_str(m->name)));
			if (et->kind != KIND_ENUM)
				nb_raise(S_ENUM_EXPECTED, xfmt("Exception: Expected enum type as %s, got Message(%s)\n", ek,
					names_str(((Message *)et)->name)));
			bool found = false;
			int32_t v0 = 0;
			for (int k = 0; k < et->n && !found; k++) {
				if (fd->has_default_value) {
					Names vn = et->vnames[k];
					if (!vn.n)
						fail("IndexError: tuple index out of range");
					if (!str_eq(vn.p[vn.n - 1], fd->default_value))
						continue;
				}
				found = true;
				v0 = et->vnums[k];
			}
			if (!found || !v0)
				continue;
			df.type = PT_INT64;
			df.value = xfmt("%d", (int)v0);
		} else if (!fd->has_default_value) {
			continue;
		}
		keep[nk] = xalloc(sizeof **keep);
		*keep[nk++] = df;
	}
	if (!nk)
		return (Str){"", 0};
	/* the kept fields were collected in reverse order */
	for (int i = 0; i < nk / 2; i++) {
		DefField *t = keep[i];
		keep[i] = keep[nk - 1 - i];
		keep[nk - 1 - i] = t;
	}
	stable_sort((void **)keep, nk, cmp_deffield);
	Buf b = {0};
	for (int i = 0; i < nk; i++) {
		const DefField *f = keep[i];
		uint64_t tag = (uint64_t)(uint32_t)f->number << 3;
		const char *t = f->value;
		switch (f->type) {
		case PT_STRING:
			put_varint(&b, tag | WT_LEN);
			put_varint(&b, strlen(t));
			buf_adds(&b, t);
			break;
		case PT_BYTES: {
			Str data = escape_decode_at(t, S_DEFVAL_BYTES);
			put_varint(&b, tag | WT_LEN);
			put_varint(&b, data.n);
			buf_add(&b, data.p, data.n);
			break;
		}
		case PT_FLOAT: {
			float fv = to_float(default_float(t));
			uint32_t bits;
			memcpy(&bits, &fv, 4);
			put_varint(&b, tag | WT_I32);
			put_fixed(&b, bits, 4);
			break;
		}
		case PT_DOUBLE: {
			double dv = default_float(t);
			uint64_t bits;
			memcpy(&bits, &dv, 8);
			put_varint(&b, tag | WT_I64);
			put_fixed(&b, bits, 8);
			break;
		}
		case PT_BOOL:
			put_varint(&b, tag | WT_VARINT);
			put_varint(&b, str_eq(t, "true"));
			break;
		case PT_INT32: case PT_INT64: {
			bool is64 = f->type == PT_INT64;
			i128 v = default_int(t, is64 ? -((i128)1 << 63) : -((i128)1 << 31),
				is64 ? ((i128)1 << 63) - 1 : ((i128)1 << 31) - 1);
			put_varint(&b, tag | WT_VARINT);
			put_varint(&b, (uint64_t)(int64_t)v);
			break;
		}
		case PT_UINT32: case PT_UINT64: {
			i128 v = default_int(t, 0, f->type == PT_UINT64 ? ((i128)1 << 64) - 1 : ((i128)1 << 32) - 1);
			put_varint(&b, tag | WT_VARINT);
			put_varint(&b, (uint64_t)v);
			break;
		}
		case PT_SINT32: case PT_SINT64: {
			bool is64 = f->type == PT_SINT64;
			i128 v = default_int(t, is64 ? -((i128)1 << 63) : -((i128)1 << 31),
				is64 ? ((i128)1 << 63) - 1 : ((i128)1 << 31) - 1);
			int64_t s = (int64_t)v;
			put_varint(&b, tag | WT_VARINT);
			put_varint(&b, is64 ? ((uint64_t)s << 1) ^ (uint64_t)(s >> 63)
					    : (uint64_t)(((uint32_t)s << 1) ^ (uint32_t)((int32_t)s >> 31)));
			break;
		}
		case PT_FIXED32: case PT_SFIXED32: {
			i128 v = f->type == PT_FIXED32 ? default_int(t, 0, ((i128)1 << 32) - 1)
						       : default_int(t, -((i128)1 << 31), ((i128)1 << 31) - 1);
			put_varint(&b, tag | WT_I32);
			put_fixed(&b, (uint64_t)v, 4);
			break;
		}
		case PT_FIXED64: case PT_SFIXED64: {
			i128 v = f->type == PT_FIXED64 ? default_int(t, 0, ((i128)1 << 64) - 1)
						       : default_int(t, -((i128)1 << 63), ((i128)1 << 63) - 1);
			put_varint(&b, tag | WT_I64);
			put_fixed(&b, (uint64_t)v, 8);
			break;
		}
		default:
			fail("unsupported by protoc-gen-uni: a default value for a field of type %d", (int)f->type);
		}
	}
	return buf_view(&b);
}

/* ---- ProtoFile ---- */

static NbOpts *toplevel_options;

/* check_recursive_dependencies(message, all_messages, root): whether root is reachable. Upstream
 * has no memo: it walks every path, so a message is walked once per path that reaches it. Here a
 * message is walked once per check (chk_gen) and remembers the upstream work of its walk
 * (chk_work: calls plus dependencies looked at) and its height, so that the upstream recursion
 * depth and the upstream work are known exactly; recursion_work counts the latter for the whole
 * request. State 1: on the stack, 2: done (does not reach root). */
static long recursion_work;
static int recursion_gen;

static void recursion_add(long w)
{
	recursion_work += w;
	if (recursion_work > 20000000)
		fail("unsupported by protoc-gen-uni: too many message dependencies to check for recursion");
}

static bool check_recursive(Message *msg, const Map *all, const char *root, int depth)
{
	if (depth > 990)
		fail("RecursionError: maximum recursion depth exceeded");
	if (!msg->chk_have) {
		message_deps(msg, &msg->chk_deps);
		msg->chk_have = true;
	}
	long start = recursion_work;
	recursion_add(1 + msg->chk_deps.n);
	msg->chk_gen = recursion_gen;
	msg->chk_state = 1;
	int height = 0;
	for (int i = 0; i < msg->chk_deps.n; i++) {
		const char *dep = msg->chk_deps.v[i];
		if (str_eq(dep, root))
			return true;
		Message *m = map_get(all, dep);
		if (!m)
			continue;
		if (m->chk_gen != recursion_gen) {
			if (check_recursive(m, all, root, depth + 1))
				return true;
		} else if (m->chk_state == 1 || depth + 1 + m->chk_height > 990) {
			fail("RecursionError: maximum recursion depth exceeded");
		} else {
			recursion_add(m->chk_work);
		}
		if (height < 1 + m->chk_height)
			height = 1 + m->chk_height;
	}
	msg->chk_state = 2;
	msg->chk_height = height;
	msg->chk_work = recursion_work - start;
	return false;
}

static void iterate_messages(ProtoFile *pf, MsgDesc *msgs, int n, Names names, const char *path, bool top);

static void parse_message(ProtoFile *pf, MsgDesc *orig, Names names, const char *path)
{
	Names name = create_name(&pf->mn, names, NULL);
	NbOpts *mo = get_nanopb_suboptions(orig->nanopb, pf->file_options, false);
	if (opt(mo, NO_SKIP_MESSAGE))
		return;
	if (opt(mo, NO_DISCARD_DEPRECATED) && orig->deprecated)
		return;
	MsgDesc *message = xalloc(sizeof *message);
	*message = *orig;
	message->fields = xalloc(sizeof *message->fields * (size_t)(orig->nfields + 1));
	for (int i = 0; i < orig->nfields; i++) {
		message->fields[i] = orig->fields[i];
		FieldDesc *f = &message->fields[i];
		if (f->type == PT_MESSAGE || f->type == PT_ENUM) {
			TB_PUSH(F_PARSE_MANGLE_FIELD);
			f->type_name = mangle_field_typename(&pf->mn, f->type_name);
			TB_POP();
		}
	}
	TB_PUSH(F_PARSE_MSG1);
	Message *mobj = message_new(name, message, mo, path, &pf->fdesc->comments);
	TB_POP();
	recursion_gen++;
	if (check_recursive(mobj, &pf->msgmap, names_str(mobj->name), 0)) {
		opts_set(mo, NO_TYPE, opt(mo, NO_FALLBACK_TYPE));
		static const char *const ft[] = {"FT_DEFAULT", "FT_CALLBACK", "FT_STATIC", "FT_IGNORE", "FT_POINTER", "FT_INLINE"};
		nb_log(xfmt("Breaking circular dependency at message %s by converting to %s\n", names_str(mobj->name),
			ft[opt(mo, NO_TYPE)]));
		TB_PUSH(F_PARSE_MSG2);
		mobj = message_new(name, message, mo, path, &pf->fdesc->comments);
		TB_POP();
	}
	vec_push(&pf->messages, mobj);
	map_put(&pf->msgmap, names_str(mobj->name), mobj);
	for (int i = 0; i < message->nenums; i++) {
		Names en = create_name(&pf->mn, names_add(names, message->enums[i].name), NULL);
		NbOpts *eo = get_nanopb_suboptions(message->enums[i].nanopb, mo, false);
		vec_push(&pf->enums, enum_new(en, &message->enums[i], eo, path_add(path, 4, i), &pf->fdesc->comments));
	}
}

/* iterate_messages: depth first, a message before its nested messages. */
static void iterate_messages(ProtoFile *pf, MsgDesc *msgs, int n, Names names, const char *path, bool top)
{
	for (int i = 0; i < n; i++) {
		Names sub = names_add(names, msgs[i].name);
		const char *sub_path = top ? xfmt("4,%d", i) : path_add(path, 3, i);
		parse_message(pf, &msgs[i], pf->mn.flatten ? names_of(msgs[i].name) : sub, sub_path);
		iterate_messages(pf, msgs[i].nested, msgs[i].nnested, sub, sub_path, false);
	}
}

static void add_extension(ProtoFile *pf, Names names, FieldDesc *ext)
{
	Names name = create_name(&pf->mn, names_add(names, ext->name), NULL);
	NbOpts *fo = get_nanopb_suboptions(ext->nanopb, pf->file_options, false);
	FieldDesc *copy = xalloc(sizeof *copy);
	*copy = *ext;
	if (copy->type == PT_MESSAGE || copy->type == PT_ENUM) {
		TB_PUSH(F_PARSE_MANGLE_EXT);
		copy->type_name = mangle_field_typename(&pf->mn, copy->type_name);
		TB_POP();
	}
	if (opt(fo, NO_TYPE) != FT_IGNORE) {
		TB_PUSH(F_PARSE_EXT);
		vec_push(&pf->extensions, extfield_new(name, copy, fo));
		TB_POP();
	}
}

static void iterate_extensions(ProtoFile *pf, MsgDesc *msgs, int n, Names names)
{
	for (int i = 0; i < n; i++) {
		Names sub = names_add(names, msgs[i].name);
		Names yielded = pf->mn.flatten ? names_of(msgs[i].name) : sub;
		for (int k = 0; k < msgs[i].nexts; k++)
			add_extension(pf, yielded, &msgs[i].exts[k]);
		iterate_extensions(pf, msgs[i].nested, msgs[i].nnested, sub);
	}
}

static void add_dependency(ProtoFile *self, ProtoFile *other);

static ProtoFile *protofile_new(FileDesc *fd, NbOpts *fo)
{
	ProtoFile *pf = xalloc(sizeof *pf);
	pf->fdesc = fd;
	pf->file_options = fo;
	/* parse() */
	TB_PUSH(F_PROTOFILE_PARSE);
	TB_PUSH(F_PARSE_MANGLE);
	mangle_init(&pf->mn, fd, fo);
	TB_POP();
	for (int i = 0; i < fd->nenums; i++) {
		Names name = create_name(&pf->mn, (Names){0}, fd->enums[i].name);
		NbOpts *eo = get_nanopb_suboptions(fd->enums[i].nanopb, fo, false);
		vec_push(&pf->enums, enum_new(name, &fd->enums[i], eo, xfmt("5,%d", i), &fd->comments));
	}
	iterate_messages(pf, fd->msgs, fd->nmsgs, names_make(NULL, 0), "", true);
	for (int i = 0; i < fd->nexts; i++)
		add_extension(pf, names_make(NULL, 0), &fd->exts[i]);
	iterate_extensions(pf, fd->msgs, fd->nmsgs, names_make(NULL, 0));
	TB_POP();
	/* discard_unused_automatic_types() */
	if (opt(fo, NO_DISCARD_UNUSED_AUTOMATIC_TYPES)) {
		Map used = {0};
		for (int i = 0; i < pf->messages.n; i++) {
			Vec all = all_fields(VEC_AT(pf->messages, Message, i));
			for (int k = 0; k < all.n; k++) {
				const Field *f = VEC_AT(all, Field, k);
				if (f->pbtype == PB_MESSAGE)
					map_put(&used, names_str(f->submsgname), (void *)1);
			}
		}
		/* map_entries: name -> the last message of that name; remove() drops that message */
		Map entries = {0};
		Vec order = {0};
		for (int i = 0; i < pf->messages.n; i++) {
			Message *m = VEC_AT(pf->messages, Message, i);
			if (!m->desc->map_entry)
				continue;
			const char *k = names_str(m->name);
			if (!map_has(&entries, k))
				vec_push(&order, (void *)k);
			map_put(&entries, k, m);
		}
		for (int i = 0; i < order.n; i++)
			if (!map_has(&used, order.v[i]))
				((Message *)map_get(&entries, order.v[i]))->discarded = true;
		int k = 0;
		for (int i = 0; i < pf->messages.n; i++)
			if (!VEC_AT(pf->messages, Message, i)->discarded)
				pf->messages.v[k++] = pf->messages.v[i];
		pf->messages.n = k;
	}
	for (int i = 0; i < pf->messages.n; i++)
		if (VEC_AT(pf->messages, Message, i)->math_include_required) {
			pf->math_include_required = true;
			break;
		}
	add_dependency(pf, pf);
	return pf;
}

/* A Names as a map key that tells parts apart. */
static const char *names_key(Names n)
{
	Buf b = {0};
	for (int i = 0; i < n.n; i++)
		buf_fmt(&b, "%d:%s/", (int)strlen(n.p[i]), n.p[i]);
	return buf_str(&b);
}

/* Long names of enums without long_names in every file of the request, to detect defaults that
 * add_dependency would map more than once. */
static Map all_longnames;

/* What add_dependency reads of other. The import walk adds the same files to every file it
 * generates, so it is built once: the file does not change after protofile_new. */
static void dep_cache(ProtoFile *other)
{
	if (other->dc_have)
		return;
	other->dc_have = true;
	int ne = other->enums.n, nm = other->messages.n;
	other->dc_keys = xalloc(sizeof *other->dc_keys * (size_t)(2 * (ne + nm) + 1));
	for (int i = 0; i < ne; i++) {
		Enum *e = VEC_AT(other->enums, Enum, i);
		other->dc_keys[2 * i] = names_str(e->names);
		other->dc_keys[2 * i + 1] = pv_text(mangle_unmangle(other, e->names));
		if (!opt(e->options, NO_LONG_NAMES))
			for (int k = 0; k < e->n; k++) {
				const char *key = names_key(e->longnames[k]);
				if (!map_has(&other->dc_longnames, key))
					map_put(&other->dc_longnames, key, &e->vnames[k]);
			}
		if (!enum_has_negative(e))
			map_put(&other->dc_nonneg, names_key(e->names), (void *)1);
	}
	for (int i = 0; i < nm; i++) {
		Message *m = VEC_AT(other->messages, Message, i);
		const char *cn = pv_text(mangle_unmangle(other, m->name));
		other->dc_keys[2 * (ne + i)] = names_str(m->name);
		other->dc_keys[2 * (ne + i) + 1] = cn;
		if (!map_has(&other->dc_canon, cn))
			map_put(&other->dc_canon, cn, m);
	}
}

/* add_dependency(other), the dictionary part: dependencies[name] = enum or message, and the
 * owner of each enum and message. */
static void dep_assign(ProtoFile *self, ProtoFile *other)
{
	dep_cache(other);
	int ne = other->enums.n;
	for (int i = 0; i < ne; i++) {
		Enum *e = VEC_AT(other->enums, Enum, i);
		map_put(&self->dependencies, other->dc_keys[2 * i], e);
		map_put(&self->dependencies, other->dc_keys[2 * i + 1], e);
		e->protofile = other;
	}
	for (int i = 0; i < other->messages.n; i++) {
		Message *m = VEC_AT(other->messages, Message, i);
		map_put(&self->dependencies, other->dc_keys[2 * (ne + i)], m);
		map_put(&self->dependencies, other->dc_keys[2 * (ne + i) + 1], m);
		m->protofile = other;
	}
}

static const char *names_key_cached(Names n, Names *of, const char **key)
{
	if (!*key || of->p != n.p || of->n != n.n) {
		*key = names_key(n);
		*of = n;
	}
	return *key;
}

/* all_fields of every message of self */
static Vec self_fields(const ProtoFile *self)
{
	Vec v = {0};
	for (int i = 0; i < self->messages.n; i++) {
		Vec all = all_fields(VEC_AT(self->messages, Message, i));
		for (int k = 0; k < all.n; k++)
			vec_push(&v, all.v[k]);
	}
	return v;
}

/* add_dependency(other), the part that changes the fields of self (all its fields, see
 * self_fields): references to submessages with different mangling rules (a str ctype equal to a
 * canonical name), short enum names in defaults and enums without negative values. Repeating it
 * for the same file changes nothing (unless a short enum name is also a long one, which fails). */
static void dep_fix(ProtoFile *self, ProtoFile *other, const Vec *all)
{
	dep_cache(other);
	const Map *canon = &other->dc_canon, *longnames = &other->dc_longnames, *nonneg = &other->dc_nonneg;
	if (!canon->n && !longnames->n && !nonneg->n)
		return; /* the loop below would change nothing */
	Vec own = {0};
	if (!all) {
		own = self_fields(self);
		all = &own;
	}
	for (int k = 0; k < all->n; k++) {
		Field *f = VEC_AT(*all, Field, k);
		if (f->ctype.kind == PV_STR) {
			Message *m = map_get(canon, f->ctype.s);
			if (m)
				f->ctype = pv_names(m->name);
		}
		if (f->deflt.kind == PV_NAMES && longnames->n) {
			Names *shortn = map_get(longnames, names_key_cached(f->deflt.nm, &f->key_deflt_of, &f->key_deflt));
			if (shortn) {
				f->deflt = pv_names(*shortn);
				if (map_has(&all_longnames, names_key(*shortn)))
					fail("unsupported by protoc-gen-uni: a short enum value name that is also the long "
					     "name of another enum value (default of %s)", f->name);
			}
		}
		if (f->pbtype == PB_ENUM && f->ctype.kind == PV_NAMES &&
			map_has(nonneg, names_key_cached(f->ctype.nm, &f->key_ctype_of, &f->key_ctype)))
			f->pbtype = PB_UENUM;
	}
}

static void add_dependency(ProtoFile *self, ProtoFile *other)
{
	dep_assign(self, other);
	dep_fix(self, other, NULL);
}

/* ---- sort_dependencies ---- */

/* A min-heap of message indexes. */
typedef struct {
	int *v, n;
} Heap;

static void heap_push(Heap *h, int x)
{
	int i = h->n++;
	while (i && h->v[(i - 1) / 2] > x) {
		h->v[i] = h->v[(i - 1) / 2];
		i = (i - 1) / 2;
	}
	h->v[i] = x;
}

static int heap_pop(Heap *h)
{
	int top = h->v[0], x = h->v[--h->n], i = 0;
	for (;;) {
		int c = 2 * i + 1;
		if (c >= h->n)
			break;
		if (c + 1 < h->n && h->v[c + 1] < h->v[c])
			c++;
		if (h->v[c] >= x)
			break;
		h->v[i] = h->v[c];
		i = c;
	}
	if (h->n)
		h->v[i] = x;
	return top;
}

/* sort_dependencies: emits the first remaining message (in list order) whose dependencies
 * (dependencies[str(name)]: of the last message of that name) are not in the set of remaining
 * names, or the first remaining one if there is none ("Circular dependency"). Each message counts
 * its dependency names still in the set; a min-heap holds those at zero. */
static Vec sort_dependencies(const Vec *messages, const char *site)
{
	int n = messages->n;
	Vec out = {0};
	const char **name = xalloc(sizeof *name * (size_t)(n + 1));
	Map last = {0};   /* name -> 1 + index of the last message of that name */
	Map remain = {0}; /* the set of remaining names */
	for (int i = 0; i < n; i++) {
		name[i] = names_str(VEC_AT(*messages, Message, i)->name);
		map_put(&last, name[i], (void *)(intptr_t)(i + 1));
		map_put(&remain, name[i], (void *)1);
	}
	/* the distinct dependency names of each last message, and who waits for a name */
	Map waiters = {0}; /* name -> Vec* of message indexes */
	int *blocked = xalloc(sizeof *blocked * (size_t)(n + 1));
	Vec *depnames = xalloc(sizeof *depnames * (size_t)(n + 1));
	for (int i = 0; i < n; i++) {
		if ((intptr_t)map_get(&last, name[i]) != i + 1)
			continue;
		Vec d = {0};
		message_deps(VEC_AT(*messages, Message, i), &d);
		Map seen = {0};
		for (int k = 0; k < d.n; k++)
			if (!map_has(&seen, d.v[k])) {
				map_put(&seen, d.v[k], (void *)1);
				vec_push(&depnames[i], d.v[k]);
			}
	}
	for (int i = 0; i < n; i++) {
		const Vec *d = &depnames[(intptr_t)map_get(&last, name[i]) - 1];
		for (int k = 0; k < d->n; k++) {
			if (!map_get(&remain, d->v[k]))
				continue;
			blocked[i]++;
			Vec *w = map_get(&waiters, d->v[k]);
			if (!w) {
				w = xalloc(sizeof *w);
				map_put(&waiters, d->v[k], w);
			}
			vec_push(w, (void *)(intptr_t)i);
		}
	}
	Heap ready = {xalloc(sizeof(int) * (size_t)(n + 1)), 0};
	for (int i = 0; i < n; i++)
		if (!blocked[i])
			heap_push(&ready, i);
	bool *done = xalloc((size_t)n + 1);
	int first = 0, circular_logs = 0;
	for (int left = n; left; left--) {
		while (ready.n && done[ready.v[0]])
			heap_pop(&ready);
		int pick;
		bool circular = !ready.n;
		if (!circular) {
			pick = heap_pop(&ready);
		} else {
			while (done[first])
				first++;
			pick = first;
			if (++circular_logs > 1000) {
				nb_stderr_truncated = true;
			} else {
				Buf b = {0};
				buf_adds(&b, "Circular dependency in messages: ");
				Map listed = {0};
				for (int i = first; i < n; i++)
					if (!done[i] && map_get(&remain, name[i]) && !map_has(&listed, name[i])) {
						buf_fmt(&b, "%s%s", listed.n ? ", " : "", name[i]);
						map_put(&listed, name[i], (void *)1);
					}
				buf_adds(&b, " (consider changing to FT_POINTER or FT_CALLBACK)\n");
				nb_log(buf_str(&b));
			}
		}
		if (!map_get(&remain, name[pick])) {
			TB_PUSH(site);
			nb_raise(circular ? S_SORT_REMOVE2 : S_SORT_REMOVE, xfmt("KeyError: %s\n", py_repr_str(str_c(name[pick]))));
		}
		map_put(&remain, name[pick], NULL);
		done[pick] = true;
		vec_push(&out, VEC_AT(*messages, Message, pick));
		Vec *w = map_get(&waiters, name[pick]);
		for (int k = 0; w && k < w->n; k++) {
			int j = (int)(intptr_t)w->v[k];
			if (!--blocked[j] && !done[j])
				heap_push(&ready, j);
		}
	}
	return out;
}
