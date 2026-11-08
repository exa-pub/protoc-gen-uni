/* Helpers over the dart_style node API for the constructs protoc_plugin prints. */

static NodeList nl_of(Node *first, ...)
{
	NodeList l = {0};
	va_list ap;
	va_start(ap, first);
	for (Node *n = first; n; n = va_arg(ap, Node *))
		ds_push(&l, n);
	va_end(ap);
	return l;
}

static const DType *dt_new(const char *prefix, const char *name)
{
	DType *t = xalloc(sizeof *t);
	t->prefix = prefix;
	t->name = name;
	return t;
}

static const DType *dt_gen(const char *prefix, const char *name, const DType *a0, const DType *a1)
{
	DType *t = xalloc(sizeof *t);
	t->prefix = prefix;
	t->name = name;
	t->args[0] = a0;
	t->args[1] = a1;
	t->nargs = a1 ? 2 : a0 ? 1 : 0;
	return t;
}

static Node *dt_node(const DType *t, bool nullable)
{
	Node *args = NULL;
	if (t->nargs)
		args = ds_targs(dt_node(t->args[0], false), t->nargs > 1 ? dt_node(t->args[1], false) : NULL, (Node *)0);
	return ds_type(t->prefix, t->name, args, nullable);
}

static Node *dt_expr(const DType *t) { return t->prefix ? ds_path(t->prefix, t->name, (char *)0) : ds_id(t->name); }

static void dt_write(Buf *b, const DType *t)
{
	if (t->prefix) {
		buf_adds(b, t->prefix);
		buf_addc(b, '.');
	}
	buf_adds(b, t->name);
	if (t->nargs) {
		buf_addc(b, '<');
		for (int i = 0; i < t->nargs; i++) {
			if (i)
				buf_adds(b, ", ");
			dt_write(b, t->args[i]);
		}
		buf_addc(b, '>');
	}
}

static const char *dt_str(const DType *t)
{
	Buf b = {0};
	dt_write(&b, t);
	return buf_str(&b);
}

static Node *core_type(const char *name) { return ds_type("$core", name, NULL, false); }
static Node *pb_type(const char *name) { return ds_type("$pb", name, NULL, false); }

static Node *annot_call(const char *prefix, const char *name, Node *arg)
{
	return ds_annot(ds_path(prefix, name, (char *)0), arg ? ds_args(arg, (Node *)0) : NULL);
}

static Node *quoted_node(Str s) { return ds_str(quoted(s)); }
