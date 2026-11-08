/* Copyright (c) 2019 Daniel G. Taylor, MIT (license text in plugin.c). */

/* Port of betterproto/templates/template.py.j2 of betterproto 1.2.5 (Jinja2 3.1.6 with
 * trim_blocks and lstrip_blocks): the rendered source as black nodes. `pending` is the text that
 * the template renders between two statements (blank lines, "# " comment lines): blib2to3 puts
 * it in the prefix of the next token, so it becomes the prefix of the next statement (with the
 * indentation of its line), or of ENDMARKER. */

typedef struct {
	Buf pending;
	BkNode **stmts; /* statements of the block being built */
	int n, cap;
} Block;

static void block_push(Block *b, BkNode *s)
{
	if (b->n == b->cap) {
		int nc = b->cap ? b->cap * 2 : 8;
		b->stmts = xrealloc(b->stmts, sizeof *b->stmts * (size_t)b->cap, sizeof *b->stmts * (size_t)nc);
		b->cap = nc;
	}
	b->stmts[b->n++] = s;
}

/* The prefix of a statement at indentation indent (pending is consumed). */
static const char *take_prefix(Buf *pending, int indent)
{
	Buf b = {0};
	buf_add(&b, pending->p, pending->n);
	for (int i = 0; i < indent; i++)
		buf_addc(&b, ' ');
	*pending = (Buf){0};
	return buf_str(&b);
}

/* {{ x.comment }} followed by a newline: "# " lines go to pending, a docstring is a statement. */
static void render_comment(Block *b, const Comment *c, int indent)
{
	if (c->kind == 1) {
		buf_adds(&b->pending, c->text);
		buf_addc(&b->pending, '\n');
	} else if (c->kind == 2) {
		block_push(b, bk_stmt(take_prefix(&b->pending, indent), bk_string(c->doc)));
	}
}

/* The block of a class or def; pending carries over to the statement after it. */
static BkNode *close_block(Block *b, Buf *outer_pending, const char *what)
{
	if (!b->n)
		fail("black.parsing.InvalidInput: Cannot parse for target version Python 3.7: %s without a body",
		     what);
	buf_add(outer_pending, b->pending.p, b->pending.n);
	return bk_suite(b->n, b->stmts);
}

static BkNode *betterproto_attr(const char *name) { return bk_attr(bk_name("betterproto"), name); }

static BkNode *int_node(int64_t v)
{
	if (v < 0)
		return bk_neg(bk_number(xfmt("%lld", -(long long)v)));
	return bk_number(xfmt("%lld", (long long)v));
}

static BkNode *zero_node(const char *zero)
{
	if (!strcmp(zero, "[]"))
		return bk_empty_list();
	if (zero[0] == '"' || zero[0] == 'b')
		return bk_string(zero);
	if (zero[0] >= '0' && zero[0] <= '9')
		return bk_number(zero);
	return bk_name(zero);
}

/* betterproto.{{ field_type }}_field(...) */
static BkNode *field_call(const Prop *f)
{
	BkNode *args[4];
	int n = 0;
	args[n++] = int_node(f->number);
	if (!strcmp(f->field_type, "map")) {
		args[n++] = betterproto_attr(f->map_types[0]);
		args[n++] = betterproto_attr(f->map_types[1]);
	}
	if (*f->one_of)
		args[n++] = bk_kwarg("group", bk_string(xfmt("\"%s\"", f->one_of)));
	if (*f->field_wraps)
		args[n++] = bk_kwarg("wraps", bk_dotted(f->field_wraps));
	return bk_call(betterproto_attr(xcat(f->field_type, "_field")), n, args);
}

/* self._unary_unary("route", request, Output,) with the trailing comma of the template */
static BkNode *unary_call(const char *fn, const Method *m)
{
	BkNode *args[3] = {bk_string(xfmt("\"%s\"", m->route)), bk_name("request"), ty_node(m->output)};
	return bk_call_trailing_comma(bk_attr(bk_name("self"), fn), 3, args);
}

static BkNode *render_method(const Method *m, Buf *pending)
{
	BkNode *params[64];
	BkNode **ps = params;
	int np = 0;
	MsgData *im = m->input_message;
	if (im && im->nprops + 2 > 64)
		ps = xalloc(sizeof *ps * (size_t)(im->nprops + 2));
	ps[np++] = bk_name("self");
	if (im && im->nprops) {
		ps[np++] = bk_star_param();
		for (int i = 0; i < im->nprops; i++) {
			const Prop *f = &im->props[i];
			Ty *t = f->type;
			if (!strcmp(f->zero, "None") && !has_prefix(ty_str(t), "Optional["))
				t = ty_sub("Optional", t, NULL);
			ps[np++] = bk_param(name_ok(f->py_name), ty_node(t), zero_node(f->zero));
		}
	}
	BkNode *returns = m->server_streaming ? bk_subscript(bk_name("AsyncGenerator"), 2,
							     (BkNode *const[]){ty_node(m->output), bk_name("None")})
					      : ty_node(m->output);
	Block body = {0};
	render_comment(&body, &m->comment, 8);
	if (m->comment.kind)
		buf_addc(&body.pending, '\n');
	/* request = {{ method.input }}() */
	BkNode *init = m->input->k == TY_NAME && !*m->input->s
			       ? bk_empty_tuple()
			       : bk_call(ty_node(m->input), 0, NULL);
	block_push(&body, bk_stmt(take_prefix(&body.pending, 8), bk_assign(bk_name("request"), init)));
	for (int i = 0; im && i < im->nprops; i++) {
		const Prop *f = &im->props[i];
		const char *prefix = take_prefix(&body.pending, 8);
		BkNode *set = bk_assign(bk_attr(bk_name("request"), f->py_name), bk_name(f->py_name));
		if (!strcmp(f->field_type, "message")) {
			BkNode *s = bk_stmt("            ", set);
			block_push(&body, bk_if(prefix, bk_is_not(bk_name(f->py_name), bk_name("None")), bk_suite(1, &s)));
		} else {
			block_push(&body, bk_stmt(prefix, set));
		}
	}
	buf_addc(&body.pending, '\n');
	if (m->server_streaming) {
		BkNode *y = bk_stmt("            ", bk_yield(bk_name("response")));
		block_push(&body, bk_async(take_prefix(&body.pending, 8),
					   bk_for("", bk_name("response"), unary_call("_unary_stream", m), bk_suite(1, &y))));
	} else {
		block_push(&body, bk_stmt(take_prefix(&body.pending, 8), bk_return(bk_await(unary_call("_unary_unary", m)))));
	}
	buf_addc(&body.pending, '\n');
	const char *prefix = take_prefix(pending, 4);
	BkNode *suite = close_block(&body, pending, "a method");
	return bk_async(prefix, bk_funcdef("", name_ok(m->py_name), np, ps, returns, suite));
}

static BkNode *import_stmt(const char *prefix, const Import *im)
{
	if (*im->module)
		dotted_ok(im->module);
	name_ok(im->name);
	return bk_stmt(prefix, bk_import_from(im->dots, im->module, 1, &im->name));
}

/* template.render(description=output) as black nodes, formatted. */
static COLD char *render(Output *o)
{
	Block file = {0};
	/* header comments */
	buf_adds(&file.pending, "# Generated by the protocol buffer compiler.  DO NOT EDIT!\n# sources: ");
	for (int i = 0; i < o->files.n; i++) {
		if (i)
			buf_adds(&file.pending, ", ");
		buf_adds(&file.pending, VEC_AT(o->files, char, i));
	}
	buf_adds(&file.pending, "\n# plugin: python-betterproto\n");
	{
		/* the sources line must stay a plain comment */
		const char *s = file.pending.p + strlen("# Generated by the protocol buffer compiler.  DO NOT EDIT!\n");
		const char *e = strchr(s, '\n');
		if (!bk_comment_ok((Str){s, (size_t)(e - s)}))
			fail("unsupported by protoc-gen-uni: file names that turn black formatting off");
	}
	const char *dataclass = "dataclass";
	block_push(&file, bk_stmt(take_prefix(&file.pending, 0), bk_import_from(0, "dataclasses", 1, &dataclass)));
	if (o->datetime_list.n) {
		py_sort_cstrs((const char **)o->datetime_list.v, o->datetime_list.n);
		block_push(&file, bk_stmt(take_prefix(&file.pending, 0),
					  bk_import_from(0, "datetime", o->datetime_list.n, (const char *const *)o->datetime_list.v)));
	}
	if (o->typing_list.n) {
		py_sort_cstrs((const char **)o->typing_list.v, o->typing_list.n);
		block_push(&file, bk_stmt(take_prefix(&file.pending, 0),
					  bk_import_from(0, "typing", o->typing_list.n, (const char *const *)o->typing_list.v)));
	}
	buf_addc(&file.pending, '\n');
	block_push(&file, bk_stmt(take_prefix(&file.pending, 0), bk_import("betterproto")));
	if (o->services.n)
		block_push(&file, bk_stmt(take_prefix(&file.pending, 0), bk_import("grpclib")));
	buf_addc(&file.pending, '\n');
	py_sort_cstrs((const char **)o->import_list.v, o->import_list.n);
	for (int i = 0; i < o->import_list.n; i++)
		block_push(&file, import_stmt(take_prefix(&file.pending, 0),
					      map_get(&o->imports, VEC_AT(o->import_list, char, i))));
	buf_adds(&file.pending, "\n\n");

	for (int i = 0; i < o->enums.n; i++) {
		EnumData *e = VEC_AT(o->enums, EnumData, i);
		const char *prefix = take_prefix(&file.pending, 0);
		Block body = {0};
		render_comment(&body, &e->comment, 4);
		if (e->comment.kind)
			buf_addc(&body.pending, '\n');
		for (int j = 0; j < e->n; j++) {
			render_comment(&body, &e->entries[j].comment, 4);
			block_push(&body, bk_stmt(take_prefix(&body.pending, 4),
						  bk_assign(bk_name(name_ok(e->entries[j].name)), int_node(e->entries[j].value))));
		}
		buf_adds(&body.pending, "\n\n");
		BkNode *suite = close_block(&body, &file.pending, "an enum");
		block_push(&file, bk_classdef(prefix, e->py_name, betterproto_attr("Enum"), suite));
	}
	for (int i = 0; i < o->messages.n; i++) {
		MsgData *m = VEC_AT(o->messages, MsgData, i);
		const char *prefix = take_prefix(&file.pending, 0);
		Block body = {0};
		render_comment(&body, &m->comment, 4);
		if (m->comment.kind)
			buf_addc(&body.pending, '\n');
		for (int j = 0; j < m->nprops; j++) {
			const Prop *f = &m->props[j];
			render_comment(&body, &f->comment, 4);
			block_push(&body, bk_stmt(take_prefix(&body.pending, 4),
						  bk_annassign(bk_name(name_ok(f->py_name)), ty_node(f->type), field_call(f))));
		}
		if (!m->nprops)
			block_push(&body, bk_stmt(take_prefix(&body.pending, 4), bk_name("pass")));
		buf_adds(&body.pending, "\n\n");
		BkNode *suite = close_block(&body, &file.pending, "a message");
		block_push(&file, bk_decorated(prefix, "dataclass", bk_classdef("", m->py_name, betterproto_attr("Message"), suite)));
	}
	for (int i = 0; i < o->services.n; i++) {
		Service *s = VEC_AT(o->services, Service, i);
		const char *prefix = take_prefix(&file.pending, 0);
		Block body = {0};
		render_comment(&body, &s->comment, 4);
		if (s->comment.kind)
			buf_addc(&body.pending, '\n');
		for (int j = 0; j < s->n; j++)
			block_push(&body, render_method(&s->methods[j], &body.pending));
		BkNode *suite = close_block(&body, &file.pending, "a service");
		block_push(&file, bk_classdef(prefix, s->py_name, betterproto_attr("ServiceStub"), suite));
	}
	return bk_format_str(bk_file(file.n, file.stmts, buf_str(&file.pending)));
}
