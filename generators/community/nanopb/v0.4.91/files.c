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

/* nanopb_generator.py: ProtoFile.generate_header, generate_source, parse_file and process_file. */

/* The command line options (process_cmdline). */
typedef struct {
	Vec exclude, settings;
	const char *extension, *header_extension, *source_extension, *options_file, *genformat, *libformat;
	bool strip_path, cpp_descriptors, notimestamp, quiet, verbose, version;
} CmdOpts;

static CmdOpts cmd;

/* make_identifier: uppercase, every character that is not alphanumeric becomes '_' */
static COLD char *make_identifier(const char *s)
{
	char *r = xstrdup(s);
	for (char *p = r; *p; p++) {
		if ((unsigned char)*p >= 0x80)
			fail("unsupported by protoc-gen-uni: non-ASCII characters in a file or package name");
		if (is_low(*p))
			*p = (char)(*p - 32);
		else if (!is_up(*p) && !(*p >= '0' && *p <= '9'))
			*p = '_';
	}
	return r;
}

/* os.path.splitext(p)[0] */
static const char *splitext0(const char *p)
{
	const char *slash = strrchr(p, '/'), *dot = strrchr(p, '.');
	if (!dot || (slash && dot < slash))
		return p;
	for (const char *q = slash ? slash + 1 : p; q < dot; q++)
		if (*q != '.')
			return xstrndup(p, (size_t)(dot - p));
	return p;
}

/* fmt % arg: TypeError and ValueError propagate (site: the frame of the formatting line). */
static void percent(Buf *b, const char *fmt, const char *arg, const char *site)
{
	char *msg;
	Buf r = {0};
	PyFormat e = py_percent_format(str_c(fmt), str_c(arg), &r, &msg);
	if (e != PY_FORMAT_OK)
		nb_raise(site, xcat(e == PY_FORMAT_TYPE_ERROR ? "TypeError: " : "ValueError: ", msg, "\n"));
	buf_add(b, r.p, r.n);
}

static const char S_GENFORMAT_INC[] = NBF(2062, "generate_header", "yield options.genformat % incfile")
	"          ~~~~~~~~~~~~~~~~~~^~~~~~~~~\n";
static const char S_GENFORMAT_DEP[] = NBF(2067, "generate_header",
	"yield options.genformat % (noext + options.extension + options.header_extension)")
	"          ~~~~~~~~~~~~~~~~~~^~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n";
static const char S_LIBFORMAT[] = NBF(2051, "generate_header", "yield options.libformat % ('pb.h')")
	"          ~~~~~~~~~~~~~~~~~~^~~~~~~~~~\n";
static const char S_GENFORMAT_SRC[] = NBF(2266, "generate_source", "yield options.genformat % (headername)")
	"          ~~~~~~~~~~~~~~~~~~^~~~~~~~~~~~~~\n";

static void extension_decl(Buf *b, const Field *e)
{
	const char *fn = names_str(e->fullname);
	if (e->skip) {
		buf_fmt(b, "/* Extension field %s was skipped because only \"optional\"\n", fn);
		buf_adds(b, "   type of extension fields is currently supported. */\n");
		return;
	}
	Buf s = {0};
	field_str(&s, e);
	buf_fmt(b, "extern const pb_extension_type_t %s; /* field type: ", ns_var_name(fn));
	buf_add_str(b, py_strip(buf_view(&s)));
	buf_adds(b, " */\n");
}

static void extension_def(Buf *b, const Field *e, Deps *d)
{
	if (e->skip)
		return;
	const char *fn = names_str(e->fullname);
	buf_fmt(b, "/* Definition for extension field %s */\n", fn);
	message_str(b, e->msg);
	fields_declaration(b, e->msg, d);
	buf_fmt(b, "pb_byte_t %s_default[] = {0x00};\n", names_str(e->msg->name));
	fields_definition(b, e->msg, d);
	buf_fmt(b, "const pb_extension_type_t %s = {\n    NULL,\n    NULL,\n    &%s_msg\n};\n", ns_var_name(fn),
		ns_type_name(names_str(e->msg->name)));
}

static const char *HEADER_VERSION_CHECK = "#if PB_PROTO_HEADER_VERSION != 40\n"
	"#error Regenerate this file with the current version of nanopb generator.\n"
	"#endif\n\n";

static int cmp_pystr(const void *a, const void *b)
{
	return py_str_cmp(str_c((const char *)a), str_c((const char *)b));
}

/* EncodedSize.get_cpp_guard */
static const char *cpp_guard(const ESize *e, const Map *local_defines)
{
	Buf b = {0};
	const char **req = sl_items(e->required_defines);
	bool any = false;
	for (size_t i = 0; i < sl_len(e->required_defines); i++) {
		if (map_has(local_defines, req[i]))
			continue;
		buf_fmt(&b, "%sdefined(%s)", any ? " && " : "#if ", req[i]);
		any = true;
	}
	if (any)
		buf_addc(&b, '\n');
	return buf_str(&b);
}

static void generate_header(Buf *out, ProtoFile *f, const Vec *includes, const char *headername, Deps *d)
{
	buf_adds(out, "/* Automatically generated nanopb header */\n");
	if (!cmd.notimestamp)
		fail("unsupported by protoc-gen-uni: -t/--timestamp (the output would contain the time of generation)");
	buf_adds(out, "/* Generated by nanopb-0.4.9.1 */\n\n");
	const char *symbol = make_identifier(*f->fdesc->package ? xcat(f->fdesc->package, "_", headername) : headername);
	buf_fmt(out, "#ifndef PB_%s_INCLUDED\n#define PB_%s_INCLUDED\n", symbol, symbol);
	if (f->math_include_required)
		buf_adds(out, "#include <math.h>\n");
	{
		char *msg;
		Buf r = {0};
		PyFormat e = py_percent_format(str_c(cmd.libformat), STR_LIT("pb.h"), &r, &msg);
		if (e == PY_FORMAT_VALUE_ERROR)
			nb_raise(S_LIBFORMAT, xcat("ValueError: ", msg, "\n"));
		if (e == PY_FORMAT_OK)
			buf_add(out, r.p, r.n);
		else
			buf_adds(out, cmd.libformat);
	}
	buf_addc(out, '\n');
	for (int i = 0; i < f->file_options->include.n; i++) {
		const char *inc = f->file_options->include.v[i];
		if (inc[0] == '<') {
			buf_fmt(out, "#include %s\n", inc);
		} else {
			percent(out, cmd.genformat, inc, S_GENFORMAT_INC);
			buf_addc(out, '\n');
		}
	}
	for (int i = 0; i < includes->n; i++) {
		const char *inc = includes->v[i];
		percent(out, cmd.genformat, xcat(splitext0(inc), cmd.extension, cmd.header_extension), S_GENFORMAT_DEP);
		buf_addc(out, '\n');
	}
	if (protoc_insertion_points)
		buf_adds(out, "/* @@protoc_insertion_point(includes) */\n");
	buf_addc(out, '\n');
	buf_adds(out, HEADER_VERSION_CHECK);
	if (f->enums.n) {
		buf_adds(out, "/* Enum definitions */\n");
		for (int i = 0; i < f->enums.n; i++) {
			enum_str(out, VEC_AT(f->enums, Enum, i));
			buf_adds(out, "\n\n");
		}
	}
	if (f->messages.n) {
		buf_adds(out, "/* Struct definitions */\n");
		Vec sorted = sort_dependencies(&f->messages, F_GEN_SORT1);
		for (int i = 0; i < sorted.n; i++) {
			message_types(out, VEC_AT(sorted, Message, i));
			message_str(out, VEC_AT(sorted, Message, i));
			buf_addc(out, '\n');
		}
		buf_addc(out, '\n');
	}
	if (f->extensions.n) {
		buf_adds(out, "/* Extensions */\n");
		for (int i = 0; i < f->extensions.n; i++)
			extension_decl(out, VEC_AT(f->extensions, Field, i));
		buf_addc(out, '\n');
	}
	buf_adds(out, "#ifdef __cplusplus\nextern \"C\" {\n#endif\n\n");
	if (f->enums.n) {
		buf_adds(out, "/* Helper constants for enums */\n");
		for (int i = 0; i < f->enums.n; i++) {
			TB_PUSH(F_GEN_AUX);
			enum_aux_defines(out, VEC_AT(f->enums, Enum, i));
			TB_POP();
			buf_addc(out, '\n');
		}
		for (int i = 0; i < f->messages.n; i++) {
			enumtype_defines(out, VEC_AT(f->messages, Message, i));
			buf_addc(out, '\n');
		}
		buf_addc(out, '\n');
	}
	Map local_defines = {0};
	if (f->messages.n) {
		buf_adds(out, "/* Initializer values for message structs */\n");
		for (int z = 0; z < 2; z++) {
			const char *sfx = z ? "_init_zero" : "_init_default";
			for (int i = 0; i < f->messages.n; i++) {
				Message *m = VEC_AT(f->messages, Message, i);
				const char *id = ns_define_name(xcat(names_str(m->name), sfx));
				buf_adds(out, "#define ");
				add_ljust(out, id, 40);
				if (!z)
					TB_PUSH(F_GEN_INIT_DEFAULT);
				buf_fmt(out, " %s\n", message_initializer(m, z));
				if (!z)
					TB_POP();
				PyVal un = mangle_unmangle(f, m->name);
				if (pv_truthy(un))
					reverse_put(f, id, pv_str(ns_define_name(xcat(pv_text(un), sfx))));
			}
		}
		buf_addc(out, '\n');
		buf_adds(out, "/* Field tags (for use in manual encoding/decoding) */\n");
		Vec sorted = sort_dependencies(&f->messages, F_GEN_SORT2);
		for (int i = 0; i < sorted.n; i++) {
			Message *m = VEC_AT(sorted, Message, i);
			for (int k = 0; k < m->fields.n; k++)
				field_tags(out, VEC_AT(m->fields, Field, k));
		}
		for (int i = 0; i < f->extensions.n; i++)
			field_tags(out, VEC_AT(f->extensions, Field, i));
		buf_addc(out, '\n');
		buf_adds(out, "/* Struct field encoding specification for nanopb */\n");
		for (int i = 0; i < f->messages.n; i++) {
			TB_PUSH(F_GEN_FIELDS_DECL);
			fields_declaration(out, VEC_AT(f->messages, Message, i), d);
			TB_POP();
			buf_addc(out, '\n');
		}
		for (int i = 0; i < f->messages.n; i++)
			buf_fmt(out, "extern const pb_msgdesc_t %s_msg;\n", ns_type_name(names_str(VEC_AT(f->messages, Message, i)->name)));
		buf_addc(out, '\n');
		buf_adds(out, "/* Defines for backwards compatibility with code written before nanopb-0.4.0 */\n");
		for (int i = 0; i < f->messages.n; i++) {
			const char *ns = names_str(VEC_AT(f->messages, Message, i)->name);
			buf_fmt(out, "#define %s &%s_msg\n", ns_define_name(xcat(ns, "_fields")), ns_type_name(ns));
		}
		buf_addc(out, '\n');
		buf_adds(out, "/* Maximum encoded size of messages (where known) */\n");
		int n = f->messages.n;
		const ESize **sizes = xalloc(sizeof *sizes * (size_t)n);
		const char **ids = xalloc(sizeof *ids * (size_t)n);
		for (int i = 0; i < n; i++) {
			Message *m = VEC_AT(f->messages, Message, i);
			ids[i] = xcat(names_str(m->name), "_size");
			TB_PUSH(F_GEN_MSGSIZE);
			sizes[i] = msg_encoded_size(m, d);
			TB_POP();
			if (sizes[i])
				map_put(&local_defines, ids[i], (void *)1);
		}
		for (int i = 0; i < n; i++) {
			Message *m = VEC_AT(f->messages, Message, i);
			for (int k = 0; k < m->fields.n; k++) {
				Field *fl = VEC_AT(m->fields, Field, k);
				if (fl->fk != FK_ONEOF)
					continue;
				TB_PUSH(F_GEN_ONEOFSIZE);
				const ESize *ms = field_encoded_size(fl, d);
				TB_POP();
				if (!ms)
					continue;
				const char *g = cpp_guard(ms, &local_defines);
				buf_adds(out, g);
				const char **decl = sl_items(ms->declarations);
				for (size_t j = 0; j < sl_len(ms->declarations); j++) {
					if (j)
						buf_addc(out, '\n');
					buf_adds(out, decl[j]);
				}
				if (*g)
					buf_adds(out, "#endif\n");
			}
		}
		int maxi = 0;
		for (int i = 1; i < n; i++)
			if ((sizes[i] ? sizes[i]->value : 0) > (sizes[maxi] ? sizes[maxi]->value : 0))
				maxi = i;
		Vec gkeys = {0};
		Map gvals = {0}; /* guard -> Vec of lines (a set) */
		for (int i = 0; i < n; i++) {
			if (!sizes[i]) {
				buf_fmt(out, "/* %s depends on runtime parameters */\n", ids[i]);
				continue;
			}
			const char *g = cpp_guard(sizes[i], &local_defines);
			Vec *vals = map_get(&gvals, g);
			if (!vals) {
				vals = xalloc(sizeof *vals);
				map_put(&gvals, g, vals);
				vec_push(&gkeys, (void *)g);
			}
			Buf l = {0};
			buf_adds(&l, "#define ");
			add_ljust(&l, ns_define_name(ids[i]), 40);
			buf_fmt(&l, " %s", es_str(sizes[i]));
			vec_push(vals, (void *)buf_str(&l));
			if (str_eq(ids[i], ids[maxi])) {
				Buf l2 = {0};
				buf_adds(&l2, "#define ");
				add_ljust(&l2, ns_define_name(xcat(symbol, "_MAX_SIZE")), 40);
				buf_fmt(&l2, " %s", ns_define_name(ids[i]));
				vec_push(vals, (void *)buf_str(&l2));
			}
		}
		for (int i = 0; i < gkeys.n; i++) {
			const char *g = gkeys.v[i];
			Vec *vals = map_get(&gvals, g);
			buf_adds(out, g);
			stable_sort(vals->v, vals->n, cmp_pystr);
			for (int k = 0; k < vals->n; k++) {
				if (k && str_eq(vals->v[k], vals->v[k - 1]))
					continue;
				buf_fmt(out, "%s\n", (const char *)vals->v[k]);
			}
			if (*g)
				buf_adds(out, "#endif\n");
		}
		buf_addc(out, '\n');
		bool any_msgid = false;
		for (int i = 0; i < n; i++)
			any_msgid = any_msgid || VEC_AT(f->messages, Message, i)->has_msgid;
		if (any_msgid) {
			buf_adds(out, "/* Message IDs (where set with \"msgid\" option) */\n");
			for (int i = 0; i < n; i++) {
				Message *m = VEC_AT(f->messages, Message, i);
				if (m->has_msgid)
					buf_fmt(out, "#define PB_MSG_%lld %s\n", (long long)m->msgid, names_str(m->name));
			}
			buf_addc(out, '\n');
			const char *dot = strchr(headername, '.');
			buf_fmt(out, "#define %s_MESSAGES \\\n",
				make_identifier(dot ? xstrndup(headername, (size_t)(dot - headername)) : headername));
			for (int i = 0; i < n; i++) {
				Message *m = VEC_AT(f->messages, Message, i);
				TB_PUSH(F_GEN_MSGSIZE);
				const ESize *ms = msg_encoded_size(m, d);
				TB_POP();
				if (m->has_msgid)
					buf_fmt(out, "\tPB_MSG(%lld,%s,%s) \\\n", (long long)m->msgid, ms ? es_str(ms) : "-1",
						names_str(m->name));
			}
			buf_addc(out, '\n');
			for (int i = 0; i < n; i++) {
				Message *m = VEC_AT(f->messages, Message, i);
				if (m->has_msgid)
					buf_fmt(out, "#define %s_msgid %lld\n", names_str(m->name), (long long)m->msgid);
			}
			buf_addc(out, '\n');
		}
	}
	bool any_pair = false;
	ODict *rev = &f->mn.reverse;
	for (int i = 0; i < rev->keys.n; i++) {
		const char *k = rev->keys.v[i], *v = pv_text(rev->vals[i]);
		if (str_eq(k, v))
			continue;
		if (!any_pair)
			buf_adds(out, "/* Mapping from canonical names (mangle_names or overridden package name) */\n");
		any_pair = true;
		buf_fmt(out, "#define %s %s\n", v, k);
	}
	if (any_pair)
		buf_addc(out, '\n');
	buf_adds(out, "#ifdef __cplusplus\n} /* extern \"C\" */\n#endif\n");
	if (cmd.cpp_descriptors) {
		buf_adds(out, "\n#ifdef __cplusplus\n/* Message descriptors for nanopb */\nnamespace nanopb {\n");
		for (int i = 0; i < f->messages.n; i++) {
			cpp_lookup(out, VEC_AT(f->messages, Message, i), &local_defines);
			buf_addc(out, '\n');
		}
		buf_adds(out, "}  // namespace nanopb\n\n#endif  /* __cplusplus */\n\n");
	}
	if (protoc_insertion_points)
		buf_adds(out, "/* @@protoc_insertion_point(eof) */\n");
	buf_adds(out, "\n#endif\n");
}

static void generate_source(Buf *out, ProtoFile *f, const char *headername, Deps *d)
{
	buf_adds(out, "/* Automatically generated nanopb constant definitions */\n");
	buf_adds(out, "/* Generated by nanopb-0.4.9.1 */\n\n");
	percent(out, cmd.genformat, headername, S_GENFORMAT_SRC);
	buf_addc(out, '\n');
	if (protoc_insertion_points)
		buf_adds(out, "/* @@protoc_insertion_point(includes) */\n");
	buf_adds(out, HEADER_VERSION_CHECK);
	Buf big = {0};
	for (int i = 0; i < f->messages.n; i++) {
		Message *m = VEC_AT(f->messages, Message, i);
		if (msg_data_size(m, d) >= 65536)
			buf_fmt(&big, "%s%s", big.n ? ", " : "", names_str(m->name));
	}
	if (big.n) {
		buf_fmt(out, "\n/* The following messages exceed 64kB in size: %s */\n", buf_str(&big));
		buf_adds(out, "\n/* The PB_FIELD_32BIT compilation option must be defined to support messages that exceed 64 kB in size. */\n");
		buf_fmt(out, "#ifndef PB_FIELD_32BIT\n#error Enable PB_FIELD_32BIT to support messages exceeding 64kB in size: %s\n#endif\n",
			buf_str(&big));
	}
	for (int i = 0; i < f->messages.n; i++) {
		fields_definition(out, VEC_AT(f->messages, Message, i), d);
		buf_adds(out, "\n\n");
	}
	for (int i = 0; i < f->extensions.n; i++) {
		extension_def(out, VEC_AT(f->extensions, Field, i), d);
		buf_addc(out, '\n');
	}
	for (int i = 0; i < f->enums.n; i++) {
		enum_to_string_definition(out, VEC_AT(f->enums, Enum, i));
		buf_addc(out, '\n');
	}
	for (int i = 0; i < f->enums.n; i++) {
		enum_validate(out, VEC_AT(f->enums, Enum, i));
		buf_addc(out, '\n');
	}
	if (f->messages.n) {
		Message *largest = VEC_AT(f->messages, Message, 0);
		int count = count_required_fields(largest);
		for (int i = 1; i < f->messages.n; i++) {
			int c = count_required_fields(VEC_AT(f->messages, Message, i));
			if (c > count) {
				count = c;
				largest = VEC_AT(f->messages, Message, i);
			}
		}
		if (count > 64) {
			buf_adds(out, "\n/* Check that missing required fields will be properly detected */\n");
			buf_fmt(out, "#if PB_MAX_REQUIRED_FIELDS < %d\n", count);
			buf_fmt(out, "#error Properly detecting missing required fields in %s requires \\\n", names_str(largest->name));
			buf_fmt(out, "       setting PB_MAX_REQUIRED_FIELDS to %d or more.\n#endif\n", count);
		}
	}
	bool has_double = false;
	for (int i = 0; i < f->messages.n && !has_double; i++) {
		Vec all = all_fields(VEC_AT(f->messages, Message, i));
		for (int k = 0; k < all.n; k++) {
			const Field *fl = VEC_AT(all, Field, k);
			if (fl->ctype.kind == PV_STR && str_eq(fl->ctype.s, "double"))
				has_double = true;
		}
	}
	if (has_double)
		buf_adds(out, "\n#ifndef PB_CONVERT_DOUBLE_FLOAT\n"
			      "/* On some platforms (such as AVR), double is really float.\n"
			      " * To be able to encode/decode double on these platforms, you need.\n"
			      " * to define PB_CONVERT_DOUBLE_FLOAT in pb.h or compiler command line.\n"
			      " */\n"
			      "PB_STATIC_ASSERT(sizeof(double) == 8, DOUBLE_MUST_BE_8_BYTES)\n"
			      "#endif\n");
	buf_addc(out, '\n');
	if (protoc_insertion_points)
		buf_adds(out, "/* @@protoc_insertion_point(eof) */\n");
}

/* parse_file */
static ProtoFile *parse_file(FileDesc *fd)
{
	NbOpts *fo = get_nanopb_suboptions(fd->nanopb, toplevel_options, str_eq(fd->syntax, "proto3"));
	TB_PUSH(F_PARSE_FILE_PROTOFILE);
	ProtoFile *pf = protofile_new(fd, fo);
	TB_POP();
	return pf;
}

static Map other_files; /* name -> ProtoFile* */

/* The walk over the imports in process_file: deps = list(f.fdesc.dependency); while deps:
 * dep = deps.pop(0); if dep in other_files: f.add_dependency(other_files[dep]); deps +=
 * other_files[dep].fdesc.dependency. It visits every path of the import graph, a breadth-first
 * walk of its unfolding: a file can come many times. Repeating add_dependency changes the fields
 * no more, so they are fixed once per file in the order of the first visits, but the dictionary
 * keeps the values of the last visit of the files: the deepest one (the longest path), and at
 * that depth the last in the walk. Files are ordered by (longest path, rank), where the rank of a
 * file at a depth orders the files by their best (rank of the parent, index in its imports). */
#define IMPORT_BUDGET 20000000
static int walk_gen;
static int64_t import_work; /* the upstream work of the walks of the request */

static void import_check(int64_t work)
{
	if (work > IMPORT_BUDGET)
		fail("unsupported by protoc-gen-uni: too many import paths to walk (upstream walks each one)");
}

/* The files of other_files that pf imports (NULL for the others), looked up once. */
static ProtoFile **walk_deps(ProtoFile *pf)
{
	if (!pf->deps_pf) {
		pf->deps_pf = xalloc(sizeof *pf->deps_pf * (size_t)(pf->fdesc->ndeps + 1));
		for (int i = 0; i < pf->fdesc->ndeps; i++)
			pf->deps_pf[i] = map_get(&other_files, pf->fdesc->deps[i]);
	}
	return pf->deps_pf;
}

/* The k-th import of pf if it is in other_files, with its state in this walk. */
static ProtoFile *walk_node(ProtoFile *pf, int k)
{
	ProtoFile *n = walk_deps(pf)[k];
	if (n && n->walk.gen != walk_gen)
		n->walk = (WalkNode){.gen = walk_gen};
	return n;
}

/* The upstream work of one visit of v: add_dependency(v) (its types, and the fields of self when
 * it changes them) and the imports it queues. */
static int64_t walk_cost(ProtoFile *v, int64_t selffields)
{
	int64_t c = 1 + v->fdesc->ndeps + v->enums.n + v->messages.n;
	dep_cache(v);
	return v->dc_canon.n || v->dc_longnames.n || v->dc_nonneg.n ? c + selffields : c;
}

/* A topological order (parents first) by an iterative depth first walk; false for a cycle. Adds
 * the work of one visit of each new file to *work. */
static bool walk_topo(ProtoFile *root, Vec *post, Vec *stack, int64_t *work, int64_t selffields)
{
	if (root->walk.state == 2)
		return true;
	if (root->walk.state == 1)
		return false;
	stack->n = 0;
	root->walk.state = 1;
	vec_push(stack, root);
	*work += walk_cost(root, selffields);
	import_check(*work);
	while (stack->n) {
		ProtoFile *n = stack->v[stack->n - 1];
		if (n->walk.next < n->fdesc->ndeps) {
			ProtoFile *c = walk_node(n, n->walk.next++);
			if (!c || c->walk.state == 2)
				continue;
			if (c->walk.state == 1)
				return false;
			c->walk.state = 1;
			vec_push(stack, c);
			*work += walk_cost(c, selffields);
			import_check(*work);
		} else {
			n->walk.state = 2;
			vec_push(post, n);
			stack->n--;
		}
	}
	return true;
}

static int cmp_walk_key(const void *a, const void *b)
{
	const ProtoFile *x = a, *y = b;
	if (x->walk.key_rank != y->walk.key_rank)
		return x->walk.key_rank < y->walk.key_rank ? -1 : 1;
	return (x->walk.key_index > y->walk.key_index) - (x->walk.key_index < y->walk.key_index);
}

static int cmp_walk_last(const void *a, const void *b)
{
	const ProtoFile *x = a, *y = b;
	if (x->walk.depth != y->walk.depth)
		return x->walk.depth < y->walk.depth ? -1 : 1;
	return (x->walk.rank > y->walk.rank) - (x->walk.rank < y->walk.rank);
}

/* Scratch memory of the walks, reused by the next walk (the arena never frees). */
static struct {
	Vec post, stack, queue;
	void *level, *ints;
	int nlevel, nints;
} walk_tmp;

static void *walk_scratch(void **p, int *cap, int n, size_t size)
{
	if (*cap < n) {
		*cap = n + n / 2 + 16;
		*p = xalloc(size * (size_t)*cap);
	}
	memset(*p, 0, size * (size_t)n);
	return *p;
}

static int64_t sat_add(int64_t a, int64_t b)
{
	return a + b > IMPORT_BUDGET ? IMPORT_BUDGET + 1 : a + b;
}

static void walk_imports(ProtoFile *f)
{
	FileDesc *fd = f->fdesc;
	walk_gen++;
	Vec all = self_fields(f);
	int64_t selffields = all.n;
	/* a lower bound of the upstream work while the walk finds the files, its exact value after */
	int64_t work = import_work + fd->ndeps;
	Vec *post = &walk_tmp.post, *queue = &walk_tmp.queue;
	post->n = queue->n = 0;
	for (int i = 0; i < fd->ndeps; i++) {
		ProtoFile *n = walk_node(f, i);
		if (n && (str_eq(fd->deps[i], fd->name) || !walk_topo(n, post, &walk_tmp.stack, &work, selffields)))
			fail("unsupported by protoc-gen-uni: an import cycle (upstream does not terminate)");
	}
	for (int i = 0; i < fd->ndeps; i++) {
		ProtoFile *n = walk_node(f, i);
		if (n)
			n->walk.paths = sat_add(n->walk.paths, 1);
	}
	work = import_work + fd->ndeps;
	for (int i = post->n - 1; i >= 0; i--) {
		ProtoFile *n = post->v[i];
		int64_t c = walk_cost(n, selffields);
		work = n->walk.paths > (IMPORT_BUDGET + 1) / c ? IMPORT_BUDGET + 1 : sat_add(work, n->walk.paths * c);
		import_check(work);
		for (int k = 0; k < n->fdesc->ndeps; k++) {
			ProtoFile *ch = walk_node(n, k);
			if (ch)
				ch->walk.paths = sat_add(ch->walk.paths, n->walk.paths);
		}
	}
	import_work = work;
	/* the fields: the first visits, a breadth-first walk with each file once */
	for (int i = 0; i < fd->ndeps; i++)
		if (walk_node(f, i))
			vec_push(queue, walk_node(f, i));
	for (int qi = 0; qi < queue->n; qi++) {
		ProtoFile *other = queue->v[qi];
		if (other->walk.fixed)
			continue;
		other->walk.fixed = true;
		dep_fix(f, other, &all);
		for (int i = 0; i < other->fdesc->ndeps; i++)
			if (walk_node(other, i))
				vec_push(queue, walk_node(other, i));
	}
	/* the dictionary: the longest paths in topological order, then the ranks level by level */
	for (int i = 0; i < fd->ndeps; i++) {
		ProtoFile *n = walk_node(f, i);
		if (n && !n->walk.depth)
			n->walk.depth = 1;
	}
	for (int i = post->n - 1; i >= 0; i--) {
		ProtoFile *n = post->v[i];
		for (int k = 0; k < n->fdesc->ndeps; k++) {
			ProtoFile *c = walk_node(n, k);
			if (c && c->walk.depth < n->walk.depth + 1)
				c->walk.depth = n->walk.depth + 1;
		}
	}
	int maxdepth = 0;
	for (int i = 0; i < post->n; i++)
		if (VEC_AT(*post, ProtoFile, i)->walk.depth > maxdepth)
			maxdepth = VEC_AT(*post, ProtoFile, i)->walk.depth;
	/* the files of depth d: level[start[d]] to level[start[d + 1] - 1], in the order of post */
	int *start = walk_scratch(&walk_tmp.ints, &walk_tmp.nints, 2 * (maxdepth + 2), sizeof(int));
	int *fill = start + maxdepth + 2;
	void **level = walk_scratch(&walk_tmp.level, &walk_tmp.nlevel, post->n + 1, sizeof(void *));
	for (int i = 0; i < post->n; i++) {
		ProtoFile *n = post->v[i];
		n->walk.key_rank = n->walk.key_index = -1;
		start[n->walk.depth]++;
	}
	for (int d = 0, sum = 0; d <= maxdepth + 1; d++) {
		int c = start[d];
		start[d] = sum;
		sum += c;
	}
	for (int i = 0; i < post->n; i++) {
		ProtoFile *n = post->v[i];
		level[start[n->walk.depth] + fill[n->walk.depth]++] = n;
	}
	for (int i = 0; i < fd->ndeps; i++) {
		ProtoFile *n = walk_node(f, i);
		if (n && n->walk.depth == 1) {
			n->walk.key_rank = 0;
			n->walk.key_index = i;
		}
	}
	for (int d = 1; d <= maxdepth; d++) {
		if (d > 1)
			for (int i = start[d - 1]; i < start[d]; i++) {
				ProtoFile *p = level[i];
				for (int k = 0; k < p->fdesc->ndeps; k++) {
					ProtoFile *c = walk_node(p, k);
					if (c && c->walk.depth == d &&
						(p->walk.rank > c->walk.key_rank ||
							(p->walk.rank == c->walk.key_rank && k > c->walk.key_index))) {
						c->walk.key_rank = p->walk.rank;
						c->walk.key_index = k;
					}
				}
			}
		stable_sort(level + start[d], start[d + 1] - start[d], cmp_walk_key);
		for (int i = start[d]; i < start[d + 1]; i++)
			((ProtoFile *)level[i])->walk.rank = i - start[d];
	}
	stable_sort(post->v, post->n, cmp_walk_last);
	for (int i = 0; i < post->n; i++)
		dep_assign(f, VEC_AT(*post, ProtoFile, i));
}

/* process_file: the header and the source of one file to generate. */
static COLD void process_file(FileDesc *fd, Buf *resp)
{
	ProtoFile *f = parse_file(fd);
	walk_imports(f);
	const char *noext = splitext0(fd->name);
	const char *headername = xcat(noext, cmd.extension, cmd.header_extension);
	const char *sourcename = xcat(noext, cmd.extension, cmd.source_extension);
	const char *headerbase = headername;
	if (cmd.strip_path) {
		const char *s = strrchr(headername, '/');
		if (s)
			headerbase = s + 1;
	}
	Vec includes = {0};
	for (int i = 0; i < fd->ndeps; i++) {
		const char *dep = fd->deps[i];
		bool ex = str_eq(dep, "nanopb.proto") || str_eq(dep, "google/protobuf/descriptor.proto");
		for (int k = 0; k < cmd.exclude.n && !ex; k++)
			ex = str_eq(dep, cmd.exclude.v[k]);
		for (int k = 0; k < f->file_options->exclude.n && !ex; k++)
			ex = str_eq(dep, f->file_options->exclude.v[k]);
		if (!ex)
			vec_push(&includes, (void *)dep);
	}
	deps_gen++;
	Deps d = {.base = &f->dependencies};
	Buf h = {0}, s = {0};
	TB_PUSH(F_PROCESS_HEADER);
	generate_header(&h, f, &includes, headerbase, &d);
	TB_POP();
	TB_PUSH(F_PROCESS_SOURCE);
	generate_source(&s, f, headerbase, &d);
	TB_POP();
	Buf file = {0};
	pb_put_bytes(&file, CGRESP_FILE_NAME, str_c(headername));
	pb_put_bytes(&file, CGRESP_FILE_CONTENT, buf_view(&h));
	pb_put_bytes(resp, CGRESP_FILE, buf_view(&file));
	file.n = 0;
	pb_put_bytes(&file, CGRESP_FILE_NAME, str_c(sourcename));
	pb_put_bytes(&file, CGRESP_FILE_CONTENT, buf_view(&s));
	pb_put_bytes(resp, CGRESP_FILE, buf_view(&file));
}
