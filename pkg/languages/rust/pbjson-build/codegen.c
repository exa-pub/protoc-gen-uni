/* Copyright (c) 2020 InfluxData. Licensed under the MIT License (full text in pbjson_build.c). */

/* Port of src/generator.rs of pbjson-build 0.8.0: Indent and the shared impl blocks. Output is
 * `write!`/`writeln!` into one buffer per package. */

/* Indent(n): four spaces per level. */
static void indent(Buf *w, int n)
{
	for (int i = 0; i < n; i++)
		buf_adds(w, "    ");
}

/* write!(w, "{}<fmt>", Indent(n), ...) */
__attribute__((format(printf, 3, 4))) static void put(Buf *w, int n, const char *fmt, ...)
{
	indent(w, n);
	va_list ap;
	va_start(ap, fmt);
	buf_vfmt(w, fmt, ap);
	va_end(ap);
}

/* writeln!(w, "{}<fmt>", Indent(n), ...) */
__attribute__((format(printf, 3, 4))) static void line(Buf *w, int n, const char *fmt, ...)
{
	indent(w, n);
	va_list ap;
	va_start(ap, fmt);
	buf_vfmt(w, fmt, ap);
	va_end(ap);
	buf_addc(w, '\n');
}

/* writeln! of a raw string whose non-empty lines start with {indent}: the indent is added to
 * the format before the arguments are substituted. */
__attribute__((format(printf, 3, 4))) static void block(Buf *w, int n, const char *fmt, ...)
{
	Buf f = {0};
	for (const char *p = fmt; *p;) {
		const char *nl = strchr(p, '\n');
		size_t len = nl ? (size_t)(nl - p) : strlen(p);
		if (len)
			indent(&f, n);
		buf_add(&f, p, len);
		if (!nl)
			break;
		buf_addc(&f, '\n');
		p = nl + 1;
	}
	va_list ap;
	va_start(ap, fmt);
	buf_vfmt(w, buf_str(&f), ap);
	va_end(ap);
	buf_addc(w, '\n');
}

static void write_fields_array(Buf *w, int n, const Vec *names)
{
	line(w, n, "const FIELDS: &[&str] = &[");
	for (int i = 0; i < names->n; i++)
		line(w, n + 1, "\"%s\",", (const char *)names->v[i]);
	line(w, n, "];");
	buf_addc(w, '\n');
}

static void write_serialize_start(int n, const char *rust_type, Buf *w)
{
	block(w, n,
	      "impl serde::Serialize for %s {\n"
	      "    #[allow(deprecated)]\n"
	      "    fn serialize<S>(&self, serializer: S) -> std::result::Result<S::Ok, S::Error>\n"
	      "    where\n"
	      "        S: serde::Serializer,\n"
	      "    {",
	      rust_type);
}

static void write_serialize_end(int n, Buf *w) { block(w, n, "    }\n}"); }

static void write_deserialize_start(int n, const char *rust_type, Buf *w)
{
	block(w, n,
	      "impl<'de> serde::Deserialize<'de> for %s {\n"
	      "    #[allow(deprecated)]\n"
	      "    fn deserialize<D>(deserializer: D) -> std::result::Result<Self, D::Error>\n"
	      "    where\n"
	      "        D: serde::Deserializer<'de>,\n"
	      "    {",
	      rust_type);
}

static void write_deserialize_end(int n, Buf *w) { block(w, n, "    }\n}"); }
