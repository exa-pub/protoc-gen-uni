/* Port of src/generator/include_file.rs of protoc-gen-prost-crate 0.5.0
 * (https://github.com/neoeinstein/protoc-gen-prost, Apache-2.0): the include file, nested
 * `pub mod` blocks with an include! of every module's output file. */

static const ProstModule root_module;

/* CodeGenContext */
typedef struct {
	const ProstModule *last;
	int indent; /* levels of four spaces */
	Buf buf;
} CodeGenContext;

static void push_indent(CodeGenContext *c)
{
	for (int i = 0; i < c->indent; i++)
		buf_adds(&c->buf, "    ");
}

static void close_module(CodeGenContext *c)
{
	c->indent--;
	push_indent(c);
	buf_adds(&c->buf, "}\n");
}

static void open_module(CodeGenContext *c, const char *module_name)
{
	push_indent(c);
	buf_adds(&c->buf, "pub mod ");
	buf_adds(&c->buf, module_name);
	buf_adds(&c->buf, " {\n");
	c->indent++;
}

static void push_insertion_point(CodeGenContext *c, const char *prefix, const char *package_name)
{
	push_indent(c);
	buf_adds(&c->buf, "// @@protoc_insertion_point(");
	buf_adds(&c->buf, prefix);
	buf_adds(&c->buf, package_name);
	buf_adds(&c->buf, ")\n");
}

/* difference: the number of trailing parts of left that are not in right, and the length of the
 * common prefix. */
static int difference(const ProstModule *left, const ProstModule *right, int *prefix)
{
	int i = 0;
	while (i < left->n && i < right->n && str_eq(left->parts[i], right->parts[i]))
		i++;
	*prefix = i;
	return i < left->n ? left->n - i : 0;
}

static void move_to(CodeGenContext *c, const ProstModule *next, const char *package)
{
	int prefix;
	int down = difference(c->last, next, &prefix);
	for (int i = 0; i < down; i++)
		close_module(c);
	/* `next.len() - prefix - 1` wraps around (release build) when it would be negative; the parts
	 * after prefix are then all taken, i.e. none. */
	for (int i = prefix; i < next->n - 1; i++)
		open_module(c, next->parts[i]);
	push_insertion_point(c, "attribute:", package);
	if (!next->n) /* next.parts().last().unwrap() of the root module */
		fail("called `Option::unwrap()` on a `None` value");
	open_module(c, next->parts[next->n - 1]);
	c->last = next;
}

/* IncludeFileGenerator::generate */
static void include_file_generate(const ProstModuleRequestSet *set, const char *filename,
				  const PackageLimiter *limiter, Vec *files)
{
	CodeGenContext c = {&root_module, 0, {0}};
	buf_adds(&c.buf, "// @generated\n");
	for (int i = 0; i < set->requests.n; i++) {
		const ProstModuleRequest *r = set->requests.v[i];
		char *filepath = prost_module_request_output_filepath(r);
		if (!filepath || !limiter_is_allowed(limiter, r->proto_package_name))
			continue;
		move_to(&c, r->module, r->proto_package_name);
		push_indent(&c);
		buf_adds(&c.buf, "include!(\"");
		buf_adds(&c.buf, filepath);
		buf_adds(&c.buf, "\");\n");
		push_insertion_point(&c, "", r->proto_package_name);
	}
	while (c.indent)
		close_module(&c);
	ProstFile *f = xalloc(sizeof *f);
	*f = (ProstFile){filename, NULL, buf_view(&c.buf)};
	vec_push(files, f);
}
