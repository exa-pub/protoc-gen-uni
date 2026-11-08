// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* Port of src/google/protobuf/io/printer.{h,cc}:
 *   - the legacy Printer::Print(text, "var", value, ...) path of PrintImpl (TokenizeFormat without
 *     raw-string handling, $var$ substitution, $$, indent of the lines after the first, empty
 *     variables) and Indent/Outdent; format errors are skipped like in release builds
 *     (checks_are_debug_only); its substitution map is kept only with an annotation collector,
 *     for Annotate(varname, descriptor);
 *   - Emit with Subs (WithDefs/WithVars frames, callbacks with their consume_after suffixes,
 *     ConditionalFunctionCall, $_start$/$_end$ annotation frames and AnnotatedAs) over the full
 *     PrintImpl and TokenizeFormat (raw-string indentation, preprocessor lines, `//~` comments),
 *     and AnnotationProtoCollector<GeneratedCodeInfo>. The codegen trace (PROTOC_CODEGEN_TRACE)
 *     and the substitution listener are not ported. */

void lp_printer_init(LpPrinter *p, Buf *out, char delim)
{
	*p = (LpPrinter){0};
	p->out = out;
	p->delim = delim;
	p->indent = 0;
	p->spaces_per_indent = 2;
	p->at_start_of_line = true;
	p->base = out->n;
}

/* Printer::IndentIfAtStart */
static void printer_indent_if_at_start(LpPrinter *p)
{
	if (!p->at_start_of_line)
		return;
	for (size_t i = 0; i < p->indent; i++)
		buf_addc(p->out, ' ');
	p->at_start_of_line = false;
}

/* An entry of Printer::substitutions_ (legacy Print with an annotation collector). */
typedef struct {
	const char *var;
	size_t len;
	size_t begin, end;  /* {1, 0}: substituted more than once */
	bool at_line_start; /* line_start_variables_: an empty value at the start of a line */
} PrintSubst;

/* Printer::WriteRaw */
static void printer_write_raw(LpPrinter *p, const char *data, size_t size)
{
	if (size == 0)
		return;
	if (p->at_start_of_line && data[0] != '\n') {
		printer_indent_if_at_start(p);
		/* empty variables at the start of the line come after the indent */
		for (int i = 0; i < p->n_subst; i++) {
			PrintSubst *s = &((PrintSubst *)p->subst)[i];
			if (s->at_line_start) {
				s->begin += p->indent;
				s->end += p->indent;
			}
		}
	}
	for (int i = 0; i < p->n_subst; i++)
		((PrintSubst *)p->subst)[i].at_line_start = false;
	if (!p->n_paren_omit) {
		buf_add(p->out, data, size);
		return;
	}
	for (size_t i = 0; i < size; ++i) {
		char c = data[i];
		if (c == '(') {
			p->paren_depth++;
			if (p->n_paren_omit && p->paren_omit[p->n_paren_omit - 1] == p->paren_depth)
				continue;
		} else if (c == ')') {
			if (p->n_paren_omit && p->paren_omit[p->n_paren_omit - 1] == p->paren_depth) {
				p->n_paren_omit--;
				p->paren_depth--;
				continue;
			}
			p->paren_depth--;
		}
		buf_addc(p->out, c);
	}
}

static const char *printer_lookup(int n_vars, const char *const *vars, const char *name, size_t n)
{
	for (int i = 0; i < n_vars; i++)
		if (strlen(vars[2 * i]) == n && memcmp(vars[2 * i], name, n) == 0)
			return vars[2 * i + 1];
	return NULL;
}

/* substitutions_.emplace(var, range) of PrintImpl: a second use makes the range negative. */
static void printer_record_subst(LpPrinter *p, const char *var, size_t len, size_t vn, bool line_start)
{
	size_t end = p->out->n - p->base;
	PrintSubst *subst = p->subst;
	for (int i = 0; i < p->n_subst; i++)
		if (subst[i].len == len && memcmp(subst[i].var, var, len) == 0) {
			subst[i].begin = 1;
			subst[i].end = 0;
			return;
		}
	if (p->n_subst == p->cap_subst) {
		int cap = p->cap_subst ? 2 * p->cap_subst : 8;
		p->subst = xrealloc(p->subst, sizeof(PrintSubst) * (size_t)p->cap_subst, sizeof(PrintSubst) * (size_t)cap);
		p->cap_subst = cap;
		subst = p->subst;
	}
	subst[p->n_subst++] = (PrintSubst){var, len, end - vn, end, line_start};
}

/* PrintImpl with the options of the legacy Print: TokenizeFormat splits the format into lines
 * (the leading spaces of every line but the first become its extra indent) and the lines into
 * chunks at the delimiters. */
void lp_printv(LpPrinter *p, const char *text, int n_vars, const char *const *vars)
{
	size_t original_indent = p->indent;
	p->n_subst = 0; /* substitutions_.clear() */
	const char *line = text;
	bool is_first = true;
	for (;;) {
		const char *nl = strchr(line, '\n');
		size_t len = nl ? (size_t)(nl - line) : strlen(line);
		const char *s = line, *e = line + len;
		if (!is_first) {
			/* a newline between format lines */
			buf_addc(p->out, '\n');
			p->at_start_of_line = true;
		}
		size_t line_indent = 0;
		while (!is_first && s < e && *s == ' ')
			s++, line_indent++;
		is_first = false;
		p->indent = original_indent + line_indent;
		/* chunks: text, var, text, var, ... (a missing closing delimiter: skip the line's
		 * last chunk, like the failed Validate in release builds keeps the chunks) */
		bool is_var = false;
		while (s <= e) {
			const char *d = memchr(s, p->delim, (size_t)(e - s));
			const char *ce = d ? d : e;
			size_t cn = (size_t)(ce - s);
			if (!is_var) {
				printer_write_raw(p, s, cn);
			} else if (cn == 0) {
				printer_write_raw(p, &p->delim, 1); /* $$ */
			} else {
				const char *v = printer_lookup(n_vars, vars, s, cn);
				if (!v && p->debug_checks)
					fail("undefined variable: \"%s\"", lp_chexescape((Str){s, cn}));
				size_t vn = v ? strlen(v) : 0;
				bool line_start = p->at_start_of_line && v && !vn;
				if (vn)
					printer_write_raw(p, v, vn);
				if (v && p->annotations)
					printer_record_subst(p, s, cn, vn, line_start);
			}
			is_var = !is_var;
			if (!d)
				break;
			s = d + 1;
		}
		if (!nl)
			break;
		line = nl + 1;
	}
	p->indent = original_indent;
}

void lp_print(LpPrinter *p, const char *text, ...)
{
	const char *vars[32];
	int n = 0;
	va_list ap;
	va_start(ap, text);
	for (const char *k = va_arg(ap, const char *); k && n < 16; k = va_arg(ap, const char *)) {
		vars[2 * n] = k;
		vars[2 * n + 1] = va_arg(ap, const char *);
		n++;
	}
	va_end(ap);
	lp_printv(p, text, n, vars);
}

void lp_write_raw(LpPrinter *p, Str data) { printer_write_raw(p, data.p, data.n); }

void lp_indent(LpPrinter *p) { p->indent += p->spaces_per_indent; }

void lp_outdent(LpPrinter *p)
{
	if (p->indent >= p->spaces_per_indent)
		p->indent -= p->spaces_per_indent;
}

/* ---- Emit: PrintImpl and TokenizeFormat with the PrintOptions of Emit ---- */

/* Printer::PrintOptions (loc and use_substitution_map are not ported) */
typedef struct {
	bool checks_are_debug_only;
	bool use_curly_brace_substitutions;
	bool allow_digit_substitutions;
	bool strip_spaces_around_vars;
	bool strip_raw_string_indentation;
	bool use_annotation_frames;
} PrintOptions;

/* Format::Chunk and Format::Line, kept on stacks owned by the printer (nested Emits from
 * callbacks push above the lines being printed; everything is addressed by index). */
typedef struct {
	const char *text;
	size_t len;
	bool is_var;
} PrintChunk;

typedef struct {
	int first, n; /* chunks[first, first + n) */
	size_t indent;
} PrintLine;

typedef struct {
	PrintChunk *chunks;
	int n_chunks, cap_chunks;
	PrintLine *lines;
	int n_lines, cap_lines;
} PrintTokens;

static const char kIgnoredCommentStart[] = "//~"; /* Options::ignored_comment_start */

/* Printer::Validate: a failed check is fatal unless checks_are_debug_only (release build). */
#define PRINT_VALIDATE(cond, opts, ...)                                                            \
	((cond) ? true : ((opts)->checks_are_debug_only ? false : (fail(__VA_ARGS__), false)))

static bool print_is_space(char c) { return c == ' ' || (c >= '\t' && c <= '\r'); }

static bool print_eq(const char *s, size_t n, const char *lit)
{
	return strlen(lit) == n && memcmp(s, lit, n) == 0;
}

static PrintTokens *print_tokens(LpPrinter *p)
{
	if (!p->tok)
		p->tok = xalloc(sizeof(PrintTokens));
	return p->tok;
}

static void print_push_chunk(PrintTokens *t, PrintChunk c)
{
	if (t->n_chunks == t->cap_chunks) {
		int cap = t->cap_chunks ? 2 * t->cap_chunks : 64;
		t->chunks = xrealloc(t->chunks, sizeof(PrintChunk) * (size_t)t->cap_chunks, sizeof(PrintChunk) * (size_t)cap);
		t->cap_chunks = cap;
	}
	t->chunks[t->n_chunks++] = c;
}

static PrintLine *print_push_line(PrintTokens *t)
{
	if (t->n_lines == t->cap_lines) {
		int cap = t->cap_lines ? 2 * t->cap_lines : 32;
		t->lines = xrealloc(t->lines, sizeof(PrintLine) * (size_t)t->cap_lines, sizeof(PrintLine) * (size_t)cap);
		t->cap_lines = cap;
	}
	PrintLine *l = &t->lines[t->n_lines++];
	*l = (PrintLine){t->n_chunks, 0, 0};
	return l;
}

/* Printer::TokenizeFormat: appends the lines of format to the token stacks; returns
 * Format::is_raw_string. */
static bool print_tokenize(LpPrinter *p, const char *format, const PrintOptions *opts)
{
	PrintTokens *t = print_tokens(p);
	const char *s = format, *e = format + strlen(format);
	size_t raw_string_indent = 0;
	bool is_raw_string = false;
	if (opts->strip_raw_string_indentation) {
		/* The indent of a raw string is that of its first line with non-whitespace characters;
		 * lines starting with '#' (preprocessor directives clang-format moved to column 0) are
		 * skipped for that but not dropped. */
		const char *orig = s, *first_pp_directive = NULL;
		while (s < e && *s == '\n') {
			s++;
			if (s < e && *s == '#') {
				if (!first_pp_directive)
					first_pp_directive = s;
				const char *nl = memchr(s, '\n', (size_t)(e - s));
				if (nl) {
					s = nl;
					continue;
				}
			}
			raw_string_indent = 0;
			is_raw_string = true;
			while (s < e && *s == ' ') {
				s++;
				++raw_string_indent;
			}
		}
		if (first_pp_directive)
			s = first_pp_directive;
		if (s == e) {
			/* a couple of explicit newlines, not a raw string */
			s = orig;
			is_raw_string = false;
			raw_string_indent = 0;
		}
		/* a preprocessor directive: the newline must not be eaten */
		if (!p->at_start_of_line && s < e && *s == '#')
			s = orig;
	}

	int first_line = t->n_lines;
	bool is_first = true;
	for (const char *ls = s;;) {
		const char *nl = memchr(ls, '\n', (size_t)(e - ls));
		const char *lt = ls;
		size_t ln = (size_t)((nl ? nl : e) - ls);
		bool skip = false;
		if (is_raw_string) {
			for (size_t i = 0; i + 3 <= ln; i++) {
				if (memcmp(lt + i, kIgnoredCommentStart, 3) == 0) {
					ln = i;
					size_t j = 0;
					while (j < ln && print_is_space(lt[j]))
						j++;
					if (j == ln) {
						/* a first line that is an ignored comment counts as the first line */
						is_first = false;
						skip = true;
					}
					break;
				}
			}
		}
		if (!skip) {
			size_t line_indent = 0;
			while (!is_first && ln > 0 && *lt == ' ') {
				lt++;
				ln--;
				++line_indent;
			}
			is_first = false;
			PrintLine *line = print_push_line(t);
			line->indent = line_indent > raw_string_indent ? line_indent - raw_string_indent : 0;

			bool is_var = false;
			size_t total_len = 0;
			const char *cs = lt, *le = lt + ln;
			for (;;) {
				const char *d = memchr(cs, p->delim, (size_t)(le - cs));
				size_t cn = (size_t)((d ? d : le) - cs);
				bool glommed = false;
				/* _start and _end glom the next chunk: $_start$foo$ */
				if (t->n_chunks > line->first && !is_var) {
					PrintChunk *prev = &t->chunks[t->n_chunks - 1];
					if (print_eq(prev->text, prev->len, "_start") || print_eq(prev->text, prev->len, "_end")) {
						prev->len += 1 + cn;
						total_len += cn + 1;
						glommed = true;
					}
				}
				if (!glommed) {
					if (is_var || cn)
						print_push_chunk(t, (PrintChunk){cs, cn, is_var});
					total_len += cn;
					if (is_var)
						total_len += 2;
					is_var = !is_var;
				}
				if (!d)
					break;
				cs = d + 1;
			}
			line->n = t->n_chunks - line->first;
			if (total_len != ln) {
				if (!line->n)
					PRINT_VALIDATE(false, opts, "wrong number of variable delimiters");
				PrintChunk *last = &t->chunks[t->n_chunks - 1];
				PRINT_VALIDATE(false, opts, "unclosed variable name: `%s`",
					       lp_chexescape((Str){last->text, last->len}));
			}
			/* trim empty, non-variable chunks at the end */
			while (line->n > 0) {
				PrintChunk *last = &t->chunks[line->first + line->n - 1];
				if (last->is_var || last->len)
					break;
				line->n--;
				t->n_chunks--;
			}
		}
		if (!nl)
			break;
		ls = nl + 1;
	}

	/* trailing lines without chunks of a raw string */
	if (is_raw_string)
		while (t->n_lines > first_line && t->lines[t->n_lines - 1].n == 0)
			t->n_lines--;
	return is_raw_string;
}

static LpSub *print_lookup(const LpPrinter *p, const char *var, size_t n, bool annotation)
{
	for (int i = p->n_frames; i >= 1; --i) {
		const LpVarFrame *f = &p->frames[i - 1];
		for (int j = 0; j < f->n; j++) {
			LpSub *s = &f->subs[j];
			if (print_eq(var, n, s->key) && (!annotation || s->annotate || s->annotate_file))
				return s;
		}
	}
	return NULL;
}

static size_t print_bytes_written(const LpPrinter *p) { return p->out->n - p->base; }

/* AnnotationProtoCollector::AddAnnotation: a GeneratedCodeInfo.Annotation (path, source_file,
 * begin, end, semantic) appended to the serialized GeneratedCodeInfo. */
static void print_add_annotation(LpPrinter *p, size_t begin, size_t end, const LpSub *record)
{
	if (!p->annotations)
		return;
	Buf a = {0};
	const char *file_path = record->annotate_file ? record->annotate_file : "";
	if (record->annotate) {
		int n = 0;
		int32_t *path = proto_desc_source_path(record->annotate, &n);
		file_path = proto_desc_file(record->annotate)->name;
		if (n) {
			Buf packed = {0};
			for (int i = 0; i < n; i++)
				pb_put_varint(&packed, (uint64_t)(int64_t)path[i]);
			pb_put_bytes(&a, 1, buf_view(&packed));
		}
	}
	pb_put_bytes(&a, 2, str_c(file_path));
	pb_put_tag(&a, 3, 0);
	pb_put_varint(&a, (uint64_t)(int64_t)(int32_t)begin);
	pb_put_tag(&a, 4, 0);
	pb_put_varint(&a, (uint64_t)(int64_t)(int32_t)end);
	if (record->semantic) {
		pb_put_tag(&a, 5, 0);
		pb_put_varint(&a, (uint64_t)(int64_t)(record->semantic - 1));
	}
	pb_put_bytes(p->annotations, 1, buf_view(&a));
}

/* Printer::Annotate(varname, varname, descriptor) with GetSubstitutionRange */
void lp_annotate(LpPrinter *p, const char *varname, const void *desc)
{
	if (!p->annotations)
		return;
	size_t len = strlen(varname);
	for (int i = 0; i < p->n_subst; i++) {
		const PrintSubst *s = &((const PrintSubst *)p->subst)[i];
		if (s->len != len || memcmp(s->var, varname, len) != 0)
			continue;
		if (s->begin > s->end)
			return; /* used multiple times */
		LpSub record = {.annotate = desc};
		print_add_annotation(p, s->begin, s->end, &record);
		return;
	}
}

/* Printer::ValidateIndexLookupInBounds */
static bool print_validate_index(LpPrinter *p, size_t index, size_t current_arg_index, size_t args_len,
				 const PrintOptions *opts)
{
	if (!PRINT_VALIDATE(index < args_len, opts, "annotation %c{%lld%c is out of bounds", p->delim,
			    (long long)index + 1, p->delim))
		return false;
	return PRINT_VALIDATE(index <= current_arg_index, opts,
			      "annotation arg must be in correct order as given; expected %c{%lld%c but got %c{%lld%c",
			      p->delim, (long long)current_arg_index + 1, p->delim, p->delim, (long long)index + 1,
			      p->delim);
}

typedef struct {
	const char *var;
	size_t len;
	size_t begin;
} PrintAnnotRecord;

/* Printer::PrintImpl */
static void print_impl(LpPrinter *p, const char *format, const char *const *args, size_t n_args,
		       const PrintOptions *opts)
{
	size_t original_indent = p->indent;
	PrintTokens *t = print_tokens(p);
	int line_base = t->n_lines, chunk_base = t->n_chunks;
	bool is_raw_string = print_tokenize(p, format, opts);
	int n_lines = t->n_lines - line_base;

	size_t arg_index = 0;
	bool skip_next_newline = false;
	Vec annot_stack = {0};   /* size_t* begin offsets of $1$ ... $}$ (FormatInternal) */
	Vec annot_records = {0}; /* PrintAnnotRecord* of $_start$var$ */
	for (int line_idx = 0; line_idx < n_lines; ++line_idx) {
		PrintLine line = t->lines[line_base + line_idx];
		/* A newline only between format lines; in a raw string none when already at the start
		 * of a line, unless the previous format line is empty. */
		if (line_idx > 0) {
			bool prev_was_empty = t->lines[line_base + line_idx - 1].n == 0;
			bool should_skip_newline = skip_next_newline || (is_raw_string && (p->at_start_of_line && !prev_was_empty));
			if (!should_skip_newline) {
				buf_addc(p->out, '\n');
				p->at_start_of_line = true;
			}
		}
		skip_next_newline = false;
		p->indent = original_indent + line.indent;

		for (int chunk_idx = 0; chunk_idx < line.n; ++chunk_idx) {
			PrintChunk chunk = t->chunks[line.first + chunk_idx];
			if (!chunk.is_var) {
				printer_write_raw(p, chunk.text, chunk.len);
				continue;
			}
			if (chunk.len == 0) {
				/* $$ is an escape for $ */
				printer_write_raw(p, &p->delim, 1);
				continue;
			}

			const char *var = chunk.text;
			size_t vn = chunk.len;
			if (opts->use_curly_brace_substitutions && *var == '{') {
				var++, vn--;
				if (!PRINT_VALIDATE(vn == 1, opts, "expected single-digit variable"))
					continue;
				if (!PRINT_VALIDATE(ascii_isdigit((unsigned char)var[0]), opts, "expected digit after {"))
					continue;
				size_t idx = (size_t)(var[0] - '1');
				if (!print_validate_index(p, idx, arg_index, n_args, opts))
					continue;
				if (idx == arg_index)
					++arg_index;
				printer_indent_if_at_start(p);
				size_t *begin = xalloc(2 * sizeof(size_t));
				begin[0] = print_bytes_written(p);
				begin[1] = idx;
				vec_push(&annot_stack, begin);
				continue;
			}
			if (opts->use_curly_brace_substitutions && *var == '}') {
				/* the rest of var is ignored */
				if (!PRINT_VALIDATE(annot_stack.n > 0, opts, "unexpected end of annotation"))
					continue;
				size_t *begin = annot_stack.v[--annot_stack.n];
				if (p->annotations) {
					/* AddAnnotationNew: args[idx] is a serialized Annotation */
					Buf a = {0};
					buf_adds(&a, args[begin[1]]);
					pb_put_tag(&a, 3, 0);
					pb_put_varint(&a, (uint64_t)(int64_t)(int32_t)begin[0]);
					pb_put_tag(&a, 4, 0);
					pb_put_varint(&a, (uint64_t)(int64_t)(int32_t)print_bytes_written(p));
					pb_put_bytes(p->annotations, 1, buf_view(&a));
				}
				continue;
			}

			const char *prefix = "", *suffix = "";
			size_t prefix_len = 0, suffix_len = 0;
			if (opts->strip_spaces_around_vars) {
				while (vn > 0 && print_is_space(*var))
					var++, vn--, prefix_len++;
				prefix = chunk.text;
				while (vn > 0 && print_is_space(var[vn - 1]))
					vn--, suffix_len++;
				suffix = var + vn;
			}
			if (!PRINT_VALIDATE(vn > 0, opts, "unexpected empty variable"))
				continue;

			bool is_start = vn > 7 && memcmp(var, "_start$", 7) == 0;
			if (is_start)
				var += 7, vn -= 7;
			bool is_end = !is_start && vn > 5 && memcmp(var, "_end$", 5) == 0;
			if (is_end)
				var += 5, vn -= 5;
			if (opts->use_annotation_frames && (is_start || is_end)) {
				if (is_start) {
					printer_indent_if_at_start(p);
					PrintAnnotRecord *r = xalloc(sizeof *r);
					*r = (PrintAnnotRecord){var, vn, print_bytes_written(p)};
					vec_push(&annot_records, r);
					/* skip the whitespace right after a _start */
					++chunk_idx;
					if (chunk_idx < line.n) {
						PrintChunk next = t->chunks[line.first + chunk_idx];
						while (next.len > 0 && *next.text == ' ')
							next.text++, next.len--;
						printer_write_raw(p, next.text, next.len);
					}
				} else {
					/* a line that is only an _end: no blank line after it */
					if (line.n == 1)
						skip_next_newline = true;
					if (!annot_records.n)
						fail("_end without _start: %s", lp_chexescape((Str){var, vn}));
					PrintAnnotRecord *r = annot_records.v[--annot_records.n];
					if (!PRINT_VALIDATE(r->len == vn && memcmp(r->var, var, vn) == 0, opts,
							    "_start and _end variables must match, but got %s and %s, "
							    "respectively",
							    xstrndup(r->var, r->len), xstrndup(var, vn)))
						continue;
					LpSub *record = print_lookup(p, var, vn, true);
					if (!PRINT_VALIDATE(record != NULL, opts, "undefined annotation variable: \"%s\"",
							    lp_chexescape((Str){var, vn})))
						continue;
					print_add_annotation(p, r->begin, print_bytes_written(p), record);
				}
				continue;
			}

			LpSub *sub = NULL, digit_sub = {0};
			LpSub *same_name_record = NULL;
			if (opts->allow_digit_substitutions && ascii_isdigit((unsigned char)var[0])) {
				if (!PRINT_VALIDATE(vn == 1, opts, "expected single-digit variable"))
					continue;
				size_t idx = (size_t)(var[0] - '1');
				if (!print_validate_index(p, idx, arg_index, n_args, opts))
					continue;
				if (idx == arg_index)
					++arg_index;
				digit_sub.value = args[idx];
				sub = &digit_sub;
			} else {
				sub = print_lookup(p, var, vn, false);
				if (opts->use_annotation_frames && p->annotations)
					same_name_record = print_lookup(p, var, vn, true);
			}
			/* an undefined variable also skips the spaces inside the $...$ */
			if (!PRINT_VALIDATE(sub != NULL, opts, "undefined variable: \"%s\"", lp_chexescape((Str){var, vn})))
				continue;

			size_t range_start = print_bytes_written(p), range_end = range_start;
			const char *consume_after = sub->consume_after;
			if (sub->value) {
				size_t n = strlen(sub->value);
				if (n) {
					/* the spaces around an empty value are not printed */
					printer_write_raw(p, prefix, prefix_len);
					printer_write_raw(p, sub->value, n);
					range_end = print_bytes_written(p);
					range_start = range_end - n;
					printer_write_raw(p, suffix, suffix_len);
				}
				if (!consume_after)
					consume_after = "";
			} else {
				PRINT_VALIDATE(prefix_len == 0 && suffix_len == 0, opts,
					       "substitution that resolves to callback cannot contain whitespace");
				range_start = print_bytes_written(p);
				if (sub->is_called)
					fail("recursive call encountered while evaluating \"%s\"", xstrndup(var, vn));
				sub->is_called = true;
				sub->cb(p, sub->arg);
				sub->is_called = false;
				range_end = print_bytes_written(p);
				if (!consume_after)
					consume_after = ";,";
			}

			if (range_start == range_end && sub->consume_parens_if_empty) {
				if (p->n_paren_omit == p->cap_paren_omit) {
					int cap = p->cap_paren_omit ? 2 * p->cap_paren_omit : 8;
					p->paren_omit = xrealloc(p->paren_omit, sizeof(size_t) * (size_t)p->cap_paren_omit,
								 sizeof(size_t) * (size_t)cap);
					p->cap_paren_omit = cap;
				}
				p->paren_omit[p->n_paren_omit++] = p->paren_depth + 1;
			}

			/* Drop one consume_after character from the text after the variable, so that
			 * "$methods$;" does not print a stray ';' after a callback. */
			int next_idx = chunk_idx + 1;
			if (*consume_after && next_idx < line.n && !t->chunks[line.first + next_idx].is_var) {
				chunk_idx = next_idx;
				PrintChunk next = t->chunks[line.first + chunk_idx];
				for (const char *c = consume_after; *c; c++) {
					if (next.len > 0 && *next.text == *c) {
						next.text++, next.len--;
						break;
					}
				}
				printer_write_raw(p, next.text, next.len);
			}

			if (same_name_record)
				print_add_annotation(p, range_start, range_end, same_name_record);
		}
	}

	PRINT_VALIDATE(arg_index == n_args, opts, "unused args: %s", format);
	PRINT_VALIDATE(annot_stack.n == 0, opts, "annotation range was not closed; expected %c}%c: %s", p->delim,
		       p->delim, format);

	/* a multiline raw string always ends on a newline */
	if (is_raw_string && !p->at_start_of_line) {
		printer_write_raw(p, "\n", 1);
		p->at_start_of_line = true;
	}

	t = print_tokens(p);
	t->n_lines = line_base;
	t->n_chunks = chunk_base;
	p->indent = original_indent;
}

void lp_push_vars(LpPrinter *p, LpSub *subs, int n)
{
	if (p->n_frames == p->cap_frames) {
		int cap = p->cap_frames ? 2 * p->cap_frames : 16;
		p->frames = xrealloc(p->frames, sizeof(LpVarFrame) * (size_t)p->cap_frames, sizeof(LpVarFrame) * (size_t)cap);
		p->cap_frames = cap;
	}
	p->frames[p->n_frames++] = (LpVarFrame){subs, n};
}

void lp_pop_vars(LpPrinter *p)
{
	if (p->n_frames > 0)
		p->n_frames--;
}

const char *lp_lookup_var(LpPrinter *p, const char *var)
{
	LpSub *s = print_lookup(p, var, strlen(var), false);
	if (!s)
		fail("could not find %s", var);
	if (!s->value)
		fail("could not find %s; found callback instead", var);
	return s->value;
}

void lp_emit(LpPrinter *p, LpSub *subs, int n, const char *format)
{
	PrintOptions opts = {
		.allow_digit_substitutions = true,
		.strip_spaces_around_vars = true,
		.strip_raw_string_indentation = true,
		.use_annotation_frames = true,
	};
	lp_push_vars(p, subs, n);
	print_impl(p, format, NULL, 0, &opts);
	lp_pop_vars(p);
}

void lp_print_subs(LpPrinter *p, LpSub *subs, int n, const char *text)
{
	PrintOptions opts = {
		.checks_are_debug_only = true,
		.strip_spaces_around_vars = true,
		.use_annotation_frames = true,
	};
	p->n_subst = 0; /* substitutions_.clear() (not recorded on this path) */
	lp_push_vars(p, subs, n);
	print_impl(p, text, NULL, 0, &opts);
	lp_pop_vars(p);
}

/* Printer::FormatInternal (compiler/cpp Formatter): $1$ ... arguments, ${1$ ... $}$ annotations
 * (args[i] of an annotation is a serialized GeneratedCodeInfo.Annotation without begin/end). */
void lp_format_internal(LpPrinter *p, const char *const *args, int n_args, LpSub *vars, int n_vars, const char *format)
{
	PrintOptions opts = {
		.use_curly_brace_substitutions = true,
		.allow_digit_substitutions = true,
		.strip_spaces_around_vars = true,
		.use_annotation_frames = true,
	};
	lp_push_vars(p, vars, n_vars);
	print_impl(p, format, args, (size_t)n_args, &opts);
	lp_pop_vars(p);
}

static int vars_find(const LpVars *v, const char *key)
{
	for (int i = 0; i < v->n; i++)
		if (str_eq(v->kv[2 * i], key))
			return i;
	return -1;
}

void lp_vars_set(LpVars *v, const char *key, const char *value)
{
	int i = vars_find(v, key);
	if (i >= 0) {
		v->kv[2 * i + 1] = value;
		return;
	}
	if (v->n == v->cap) {
		int cap = v->cap ? 2 * v->cap : 16;
		v->kv = xrealloc(v->kv, sizeof(char *) * 2 * (size_t)v->cap, sizeof(char *) * 2 * (size_t)cap);
		v->cap = cap;
	}
	v->kv[2 * v->n] = key;
	v->kv[2 * v->n + 1] = value;
	v->n++;
}

void lp_vars_insert(LpVars *v, const char *key, const char *value)
{
	if (vars_find(v, key) < 0)
		lp_vars_set(v, key, value);
}

const char *lp_vars_get(const LpVars *v, const char *key)
{
	int i = vars_find(v, key);
	return i < 0 ? NULL : v->kv[2 * i + 1];
}

LpVars lp_vars_copy(const LpVars *v)
{
	LpVars c = {0};
	if (v->n) {
		c.kv = xalloc(sizeof(char *) * 2 * (size_t)v->n);
		memcpy(c.kv, v->kv, sizeof(char *) * 2 * (size_t)v->n);
		c.n = c.cap = v->n;
	}
	return c;
}

void lp_print_vars(LpPrinter *p, const LpVars *v, const char *text) { lp_printv(p, text, v->n, v->kv); }
