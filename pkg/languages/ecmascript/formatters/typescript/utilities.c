/*! *****************************************************************************
Copyright (c) Microsoft Corporation. All rights reserved.
Licensed under the Apache License, Version 2.0 (the "License"); you may not use
this file except in compliance with the License. You may obtain a copy of the
License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED
WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE,
MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions
and limitations under the License.
***************************************************************************** */

/* Port of TypeScript 3.9.10 src/compiler/utilities.ts and utilitiesPublic.ts (the parts the
 * printer and the factory use): createTextWriter, escapeString / escapeNonAsciiString,
 * getLiteralText, writeCommentRange, operator precedence and associativity, node kind
 * predicates; plus isLineBreak / isWhiteSpace* / computeLineStarts of scanner.ts. */

/* ---- UTF-8 helpers (TypeScript reads UTF-16 code units) ---- */

/* Decodes one code point; invalid bytes decode as U+FFFD (1 byte). A surrogate encoded like a
 * code point (WTF-8, what the scanner makes of a lone \uD800 escape) decodes to that UTF-16
 * code unit, as in a JavaScript string. */
static int utf8_rune(const char *p, size_t n, uint32_t *r)
{
	const unsigned char *u = (const unsigned char *)p;
	if (n >= 3 && u[0] == 0xED && (u[1] & 0xE0) == 0xA0 && (u[2] & 0xC0) == 0x80) {
		*r = 0xD000 | ((uint32_t)(u[1] & 0x3F) << 6) | (u[2] & 0x3F);
		return 3;
	}
	return utf8_decode(p, n, r);
}

/* scanner.ts isLineBreak. */
static bool is_line_break(uint32_t ch)
{
	return ch == '\n' || ch == '\r' || ch == 0x2028 || ch == 0x2029;
}

/* scanner.ts isWhiteSpaceSingleLine. */
static bool is_white_space_single_line(uint32_t ch)
{
	return ch == ' ' || ch == '\t' || ch == '\v' || ch == '\f' || ch == 0xA0 || ch == 0x85 || ch == 0x1680 ||
	       (ch >= 0x2000 && ch <= 0x200B) || ch == 0x202F || ch == 0x205F || ch == 0x3000 || ch == 0xFEFF;
}

/* scanner.ts isWhiteSpaceLike. */
static bool is_white_space_like(uint32_t ch)
{
	return is_white_space_single_line(ch) || is_line_break(ch);
}

/* The JavaScript RegExp class \s (WhiteSpace and LineTerminator of ECMAScript). */
static bool js_is_space(uint32_t ch)
{
	return ch == '\t' || ch == '\n' || ch == '\v' || ch == '\f' || ch == '\r' || ch == ' ' || ch == 0xA0 ||
	       ch == 0x1680 || (ch >= 0x2000 && ch <= 0x200A) || ch == 0x2028 || ch == 0x2029 || ch == 0x202F ||
	       ch == 0x205F || ch == 0x3000 || ch == 0xFEFF;
}

/* ---- createTextWriter ---- */

static void writer_reset(TextWriter *w)
{
	w->out = (Buf){0};
	w->indent = 0;
	w->line_start = true;
	w->has_trailing_comment = false;
}

/* updateLineCountAndPosFor: lineStart becomes true iff s contains a line terminator and ends
 * with one (computeLineStarts: \r\n, \r, \n, U+2028, U+2029). */
static void update_line_start_for(TextWriter *w, const char *s, size_t n)
{
	bool any = false, at_end = false;
	for (size_t i = 0; i < n;) {
		uint32_t r;
		int k = utf8_rune(s + i, n - i, &r);
		i += (size_t)k;
		if (r == '\r' && i < n && s[i] == '\n')
			i++;
		if (is_line_break(r)) {
			any = true;
			at_end = i == n;
		} else {
			at_end = false;
		}
	}
	w->line_start = any && at_end;
}

/* getIndentString(level): four spaces per level. */
static void add_indent(Buf *b, int level)
{
	for (int i = 0; i < level; i++)
		buf_add(b, "    ", 4);
}

static void writer_write_text(TextWriter *w, const char *s, size_t n)
{
	if (n) {
		if (w->line_start) {
			add_indent(&w->out, w->indent);
			w->line_start = false;
		}
		buf_add(&w->out, s, n);
		update_line_start_for(w, s, n);
	}
}

static void writer_write(TextWriter *w, const char *s, size_t n)
{
	if (n)
		w->has_trailing_comment = false;
	writer_write_text(w, s, n);
}

static void writer_write_comment(TextWriter *w, const char *s, size_t n)
{
	if (n)
		w->has_trailing_comment = true;
	writer_write_text(w, s, n);
}

static void writer_raw_write(TextWriter *w, const char *s, size_t n)
{
	buf_add(&w->out, s, n);
	update_line_start_for(w, s, n);
	w->has_trailing_comment = false;
}

static void writer_write_line(TextWriter *w, bool force)
{
	if (!w->line_start || force) {
		buf_addc(&w->out, '\n');
		w->line_start = true;
		w->has_trailing_comment = false;
	}
}

static bool writer_has_trailing_whitespace(const TextWriter *w)
{
	if (!w->out.n)
		return false;
	/* last UTF-16 code unit of the output: decode the last UTF-8 sequence */
	size_t i = w->out.n - 1;
	while (i > 0 && ((unsigned char)w->out.p[i] & 0xC0) == 0x80)
		i--;
	uint32_t r;
	utf8_rune(w->out.p + i, w->out.n - i, &r);
	return is_white_space_like(r);
}

/* ---- escapeString / escapeNonAsciiString ---- */

static void add_utf16_escape(Buf *b, uint32_t unit)
{
	static const char hex[] = "0123456789ABCDEF";
	char e[6] = {'\\', 'u', hex[(unit >> 12) & 15], hex[(unit >> 8) & 15], hex[(unit >> 4) & 15], hex[unit & 15]};
	buf_add(b, e, 6);
}

/* escapeString(s, quote) (getReplacement, escapedCharsMap) and, if non_ascii, the replacement of
 * every non-ASCII UTF-16 code unit by \uXXXX of escapeNonAsciiString. */
static void escape_string_into(Buf *b, Str s, char quote, bool non_ascii)
{
	for (size_t i = 0; i < s.n;) {
		uint32_t c;
		int k = utf8_rune(s.p + i, s.n - i, &c);
		i += (size_t)k;
		bool esc;
		if (quote == '`')
			esc = c == '\\' || c == '`';
		else
			esc = c == '\\' || c == (uint32_t)quote || c <= 0x1F || c == 0x2028 || c == 0x2029 || c == 0x85;
		if (esc) {
			switch (c) {
			case 0:
				buf_adds(b, i < s.n && s.p[i] >= '0' && s.p[i] <= '9' ? "\\x00" : "\\0");
				break;
			case '\t': buf_adds(b, "\\t"); break;
			case '\v': buf_adds(b, "\\v"); break;
			case '\f': buf_adds(b, "\\f"); break;
			case '\b': buf_adds(b, "\\b"); break;
			case '\r': buf_adds(b, "\\r"); break;
			case '\n': buf_adds(b, "\\n"); break;
			case '\\': buf_adds(b, "\\\\"); break;
			case '"': buf_adds(b, "\\\""); break;
			case '\'': buf_adds(b, "\\'"); break;
			case '`': buf_adds(b, "\\`"); break;
			case 0x2028: buf_adds(b, "\\u2028"); break;
			case 0x2029: buf_adds(b, "\\u2029"); break;
			case 0x85: buf_adds(b, "\\u0085"); break;
			default: add_utf16_escape(b, c); break;
			}
		} else if (non_ascii && c > 0x7F) {
			if (c >= 0x10000) {
				c -= 0x10000;
				add_utf16_escape(b, 0xD800 + (c >> 10));
				add_utf16_escape(b, 0xDC00 + (c & 0x3FF));
			} else {
				add_utf16_escape(b, c);
			}
		} else {
			buf_add(b, s.p + i - k, (size_t)k);
		}
	}
}

char *ts_escape_string(Str s, char quote, bool non_ascii)
{
	Buf b = {0};
	escape_string_into(&b, s, quote, non_ascii);
	return (char *)buf_str(&b);
}

/* getLiteralText for nodes without a parent (synthesized nodes and nodes of a parsed text, whose
 * parent pointers TypeScript sets only with setParentNodes). */
static Str get_literal_text(const TsNode *node)
{
	bool non_ascii = !(node->emit_flags & TS_EmitFlags_NoAsciiEscaping);
	Buf b = {0};
	switch (node->kind) {
	case TS_StringLiteral: {
		char q = node->single_quote ? '\'' : '"';
		buf_addc(&b, q);
		escape_string_into(&b, node->text, q, non_ascii);
		buf_addc(&b, q);
		return buf_view(&b);
	}
	case TS_NoSubstitutionTemplateLiteral:
	case TS_TemplateHead:
	case TS_TemplateMiddle:
	case TS_TemplateTail: {
		Buf raw = {0};
		if (node->has_raw_text && node->raw_text.n) {
			buf_add_str(&raw, node->raw_text);
		} else {
			/* escapeTemplateSubstitution(escapeText(node.text, backtick)): "${" -> "\${" */
			Buf e = {0};
			escape_string_into(&e, node->text, '`', non_ascii);
			for (size_t i = 0; i < e.n; i++) {
				if (e.p[i] == '$' && i + 1 < e.n && e.p[i + 1] == '{')
					buf_addc(&raw, '\\');
				buf_addc(&raw, e.p[i]);
			}
		}
		const char *open = node->kind == TS_NoSubstitutionTemplateLiteral || node->kind == TS_TemplateHead ? "`" : "}";
		const char *close = node->kind == TS_NoSubstitutionTemplateLiteral || node->kind == TS_TemplateTail ? "`" : "${";
		buf_adds(&b, open);
		buf_add(&b, raw.p, raw.n);
		buf_adds(&b, close);
		return buf_view(&b);
	}
	case TS_NumericLiteral:
	case TS_BigIntLiteral:
	case TS_RegularExpressionLiteral:
		return node->text;
	default:
		fail("typescript 3.9.10: literal of unexpected kind %d", node->kind);
	}
}

/* ---- writeCommentRange ---- */

/* computeLineStarts with, per line, the byte length of the last UTF-16 code unit of its line
 * terminator (what `nextLineStart - 1` skips: \n of \r\n, or the whole U+2028). */
typedef struct {
	size_t *start;
	size_t *term;
	int n;
} LineMap;

static LineMap compute_line_starts(Str text)
{
	LineMap m = {0};
	int cap = 8;
	m.start = xalloc((size_t)cap * sizeof *m.start);
	m.term = xalloc((size_t)cap * sizeof *m.term);
	size_t line_start = 0;
	for (size_t pos = 0; pos < text.n;) {
		uint32_t ch;
		int k = utf8_rune(text.p + pos, text.n - pos, &ch);
		pos += (size_t)k;
		size_t term = 0;
		if (ch == '\r') {
			term = 1;
			if (pos < text.n && text.p[pos] == '\n')
				pos++;
		} else if (ch == '\n') {
			term = 1;
		} else if (ch > 0x7F && is_line_break(ch)) {
			term = (size_t)k;
		}
		if (term) {
			if (m.n + 1 >= cap) {
				m.start = xrealloc(m.start, (size_t)cap * sizeof *m.start, (size_t)cap * 2 * sizeof *m.start);
				m.term = xrealloc(m.term, (size_t)cap * sizeof *m.term, (size_t)cap * 2 * sizeof *m.term);
				cap *= 2;
			}
			m.start[m.n] = line_start;
			m.term[m.n] = term;
			m.n++;
			line_start = pos;
		}
	}
	m.start[m.n] = line_start;
	m.term[m.n] = 1;
	m.n++;
	return m;
}

/* calculateIndent: tabs advance to the next multiple of 4, other single-line white space by 1. */
static int calculate_indent(Str text, size_t pos, size_t end)
{
	int indent = 0;
	while (pos < end && pos < text.n) {
		uint32_t ch;
		int k = utf8_rune(text.p + pos, text.n - pos, &ch);
		if (!is_white_space_single_line(ch))
			break;
		if (ch == '\t')
			indent += 4 - (indent % 4);
		else
			indent++;
		pos += (size_t)k;
	}
	return indent;
}

/* writeTrimmedCurrentLine. */
static void write_trimmed_current_line(TextWriter *w, Str text, size_t comment_end, size_t pos, size_t end)
{
	size_t a = pos, z = end;
	while (a < z) {
		uint32_t ch;
		int k = utf8_rune(text.p + a, z - a, &ch);
		if (!js_is_space(ch))
			break;
		a += (size_t)k;
	}
	while (z > a) {
		size_t i = z - 1;
		while (i > a && ((unsigned char)text.p[i] & 0xC0) == 0x80)
			i--;
		uint32_t ch;
		utf8_rune(text.p + i, z - i, &ch);
		if (!js_is_space(ch))
			break;
		z = i;
	}
	if (z > a) {
		writer_write_comment(w, text.p + a, z - a);
		if (end != comment_end)
			writer_write_line(w, false);
	} else {
		writer_raw_write(w, "\n", 1);
	}
}

static void write_comment_range(TextWriter *w, Str text, size_t comment_pos, size_t comment_end)
{
	if (comment_pos + 1 < text.n && text.p[comment_pos + 1] == '*') {
		LineMap m = compute_line_starts(text);
		int first_line = 0;
		while (first_line + 1 < m.n && m.start[first_line + 1] <= comment_pos)
			first_line++;
		int first_indent = -1;
		for (size_t pos = comment_pos; pos < comment_end;) {
			int line = first_line;
			while (line + 1 < m.n && m.start[line + 1] <= pos)
				line++;
			size_t next_start, last_unit;
			if (line + 1 == m.n) {
				next_start = text.n + 1;
				last_unit = 1;
			} else {
				next_start = m.start[line + 1];
				last_unit = m.term[line];
			}
			if (pos != comment_pos) {
				if (first_indent < 0)
					first_indent = calculate_indent(text, m.start[first_line], comment_pos);
				int spaces = w->indent * 4 - first_indent + calculate_indent(text, pos, next_start);
				if (spaces > 0) {
					int single = spaces % 4;
					Buf ind = {0};
					add_indent(&ind, (spaces - single) / 4);
					writer_raw_write(w, ind.p ? ind.p : "", ind.n);
					while (single--)
						writer_raw_write(w, " ", 1);
				} else {
					writer_raw_write(w, "", 0);
				}
			}
			size_t end = next_start - last_unit;
			if (end > comment_end)
				end = comment_end;
			write_trimmed_current_line(w, text, comment_end, pos, end);
			pos = next_start;
		}
	} else {
		writer_write_comment(w, text.p + comment_pos, comment_end - comment_pos);
	}
}

/* ---- operators ---- */

static int get_operator(const TsNode *expression)
{
	if (expression->kind == TS_BinaryExpression)
		return expression->operator_token->kind;
	if (expression->kind == TS_PrefixUnaryExpression || expression->kind == TS_PostfixUnaryExpression)
		return expression->operator_;
	return expression->kind;
}

static int get_operator_associativity(TsSyntaxKind kind, int operator_kind, bool has_arguments)
{
	switch (kind) {
	case TS_NewExpression:
		return has_arguments ? ASSOC_Left : ASSOC_Right;
	case TS_PrefixUnaryExpression:
	case TS_TypeOfExpression:
	case TS_VoidExpression:
	case TS_DeleteExpression:
	case TS_AwaitExpression:
	case TS_ConditionalExpression:
	case TS_YieldExpression:
		return ASSOC_Right;
	case TS_BinaryExpression:
		switch (operator_kind) {
		case TS_AsteriskAsteriskToken:
		case TS_EqualsToken:
		case TS_PlusEqualsToken:
		case TS_MinusEqualsToken:
		case TS_AsteriskAsteriskEqualsToken:
		case TS_AsteriskEqualsToken:
		case TS_SlashEqualsToken:
		case TS_PercentEqualsToken:
		case TS_LessThanLessThanEqualsToken:
		case TS_GreaterThanGreaterThanEqualsToken:
		case TS_GreaterThanGreaterThanGreaterThanEqualsToken:
		case TS_AmpersandEqualsToken:
		case TS_CaretEqualsToken:
		case TS_BarEqualsToken:
			return ASSOC_Right;
		}
		break;
	default:
		break;
	}
	return ASSOC_Left;
}

static int get_expression_associativity(const TsNode *expression)
{
	bool has_arguments = expression->kind == TS_NewExpression && expression->arguments != NULL;
	return get_operator_associativity(expression->kind, get_operator(expression), has_arguments);
}

static int get_binary_operator_precedence(int kind)
{
	switch (kind) {
	case TS_QuestionQuestionToken:
		return 4;
	case TS_BarBarToken:
		return 5;
	case TS_AmpersandAmpersandToken:
		return 6;
	case TS_BarToken:
		return 7;
	case TS_CaretToken:
		return 8;
	case TS_AmpersandToken:
		return 9;
	case TS_EqualsEqualsToken:
	case TS_ExclamationEqualsToken:
	case TS_EqualsEqualsEqualsToken:
	case TS_ExclamationEqualsEqualsToken:
		return 10;
	case TS_LessThanToken:
	case TS_GreaterThanToken:
	case TS_LessThanEqualsToken:
	case TS_GreaterThanEqualsToken:
	case TS_InstanceOfKeyword:
	case TS_InKeyword:
	case TS_AsKeyword:
		return 11;
	case TS_LessThanLessThanToken:
	case TS_GreaterThanGreaterThanToken:
	case TS_GreaterThanGreaterThanGreaterThanToken:
		return 12;
	case TS_PlusToken:
	case TS_MinusToken:
		return 13;
	case TS_AsteriskToken:
	case TS_SlashToken:
	case TS_PercentToken:
		return 14;
	case TS_AsteriskAsteriskToken:
		return 15;
	}
	return -1;
}

static int get_operator_precedence(TsSyntaxKind node_kind, int operator_kind, bool has_arguments)
{
	switch (node_kind) {
	case TS_CommaListExpression:
		return 0;
	case TS_SpreadElement:
		return 1;
	case TS_YieldExpression:
		return 2;
	case TS_ConditionalExpression:
		return 4;
	case TS_BinaryExpression:
		switch (operator_kind) {
		case TS_CommaToken:
			return 0;
		case TS_EqualsToken:
		case TS_PlusEqualsToken:
		case TS_MinusEqualsToken:
		case TS_AsteriskAsteriskEqualsToken:
		case TS_AsteriskEqualsToken:
		case TS_SlashEqualsToken:
		case TS_PercentEqualsToken:
		case TS_LessThanLessThanEqualsToken:
		case TS_GreaterThanGreaterThanEqualsToken:
		case TS_GreaterThanGreaterThanGreaterThanEqualsToken:
		case TS_AmpersandEqualsToken:
		case TS_CaretEqualsToken:
		case TS_BarEqualsToken:
			return 3;
		default:
			return get_binary_operator_precedence(operator_kind);
		}
	case TS_PrefixUnaryExpression:
	case TS_TypeOfExpression:
	case TS_VoidExpression:
	case TS_DeleteExpression:
	case TS_AwaitExpression:
		return 16;
	case TS_PostfixUnaryExpression:
		return 17;
	case TS_CallExpression:
		return 18;
	case TS_NewExpression:
		return has_arguments ? 19 : 18;
	case TS_TaggedTemplateExpression:
	case TS_PropertyAccessExpression:
	case TS_ElementAccessExpression:
		return 19;
	case TS_ThisKeyword:
	case TS_SuperKeyword:
	case TS_Identifier:
	case TS_NullKeyword:
	case TS_TrueKeyword:
	case TS_FalseKeyword:
	case TS_NumericLiteral:
	case TS_BigIntLiteral:
	case TS_StringLiteral:
	case TS_ArrayLiteralExpression:
	case TS_ObjectLiteralExpression:
	case TS_FunctionExpression:
	case TS_ArrowFunction:
	case TS_ClassExpression:
	case TS_JsxElement:
	case TS_JsxSelfClosingElement:
	case TS_JsxFragment:
	case TS_RegularExpressionLiteral:
	case TS_NoSubstitutionTemplateLiteral:
	case TS_TemplateExpression:
	case TS_ParenthesizedExpression:
	case TS_OmittedExpression:
		return 20;
	default:
		return -1;
	}
}

static int get_expression_precedence(const TsNode *expression)
{
	bool has_arguments = expression->kind == TS_NewExpression && expression->arguments != NULL;
	return get_operator_precedence(expression->kind, get_operator(expression), has_arguments);
}

/* ---- kind predicates (utilitiesPublic.ts) ---- */

static bool is_left_hand_side_expression_kind(TsSyntaxKind kind)
{
	switch (kind) {
	case TS_PropertyAccessExpression:
	case TS_ElementAccessExpression:
	case TS_NewExpression:
	case TS_CallExpression:
	case TS_JsxElement:
	case TS_JsxSelfClosingElement:
	case TS_JsxFragment:
	case TS_TaggedTemplateExpression:
	case TS_ArrayLiteralExpression:
	case TS_ParenthesizedExpression:
	case TS_ObjectLiteralExpression:
	case TS_ClassExpression:
	case TS_FunctionExpression:
	case TS_Identifier:
	case TS_RegularExpressionLiteral:
	case TS_NumericLiteral:
	case TS_BigIntLiteral:
	case TS_StringLiteral:
	case TS_NoSubstitutionTemplateLiteral:
	case TS_TemplateExpression:
	case TS_FalseKeyword:
	case TS_NullKeyword:
	case TS_ThisKeyword:
	case TS_TrueKeyword:
	case TS_SuperKeyword:
	case TS_NonNullExpression:
	case TS_MetaProperty:
	case TS_ImportKeyword:
		return true;
	default:
		return false;
	}
}

static bool is_unary_expression_kind(TsSyntaxKind kind)
{
	switch (kind) {
	case TS_PrefixUnaryExpression:
	case TS_PostfixUnaryExpression:
	case TS_DeleteExpression:
	case TS_TypeOfExpression:
	case TS_VoidExpression:
	case TS_AwaitExpression:
	case TS_TypeAssertionExpression:
		return true;
	default:
		return is_left_hand_side_expression_kind(kind);
	}
}

static bool is_expression_kind(TsSyntaxKind kind)
{
	switch (kind) {
	case TS_ConditionalExpression:
	case TS_YieldExpression:
	case TS_ArrowFunction:
	case TS_BinaryExpression:
	case TS_SpreadElement:
	case TS_AsExpression:
	case TS_OmittedExpression:
	case TS_CommaListExpression:
	case TS_PartiallyEmittedExpression:
		return true;
	default:
		return is_unary_expression_kind(kind);
	}
}

static bool is_keyword(int kind)
{
	return TS_FirstKeyword <= kind && kind <= TS_LastKeyword;
}

static bool is_token_kind(int kind)
{
	return kind >= TS_FirstToken && kind <= TS_LastToken;
}

static bool is_literal_kind(int kind)
{
	return TS_FirstLiteralToken <= kind && kind <= TS_LastLiteralToken;
}

static bool position_is_synthesized(int pos)
{
	return !(pos >= 0);
}

static bool node_is_synthesized(const TsNode *n)
{
	return position_is_synthesized(n->pos) || position_is_synthesized(n->end);
}

static bool is_prologue_directive(const TsNode *n)
{
	return n->kind == TS_ExpressionStatement && n->expression->kind == TS_StringLiteral;
}
