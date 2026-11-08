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

/* Port of TypeScript 3.9.10 src/compiler/emitter.ts: createPrinter (printFile, printNode) with
 * printer options {newLine: LineFeed} and no handlers.
 *
 * What the port leaves out, because it cannot be reached when the printed source file has empty
 * text (as in protobuf-ts) and there are no transformations:
 *   - comments of the source text (emitLeadingComments, emitTrailingComments,
 *     emitDetachedComments, ...): they read currentSourceFile.text, which is "", so they never
 *     emit anything; synthetic comments are emitted;
 *   - line queries on positions (rangeIsOnSingleLine, positionsAreOnSameLine, ...): every
 *     position of an empty text is on line 0, so they all answer "same line";
 *   - source maps, bundles, emit helpers, name generation (no generated identifiers),
 *     preserveSourceNewlines, substitutions and notifications, JSX and JSDoc nodes.
 * Node kinds outside the supported set fail with a message. */

typedef struct {
	TextWriter w;
	/* `write` of the printer: writeBase or the writer of emitNodeWithWriter (all of them
	 * write the same way with createTextWriter). */
} Printer;

typedef enum {
	HINT_SourceFile,
	HINT_Expression,
	HINT_IdentifierName,
	HINT_MappedTypeParameter,
	HINT_Unspecified,
	HINT_EmbeddedStatement,
} EmitHint;

static void pipeline_emit(Printer *p, EmitHint hint, TsNode *node);

static void emit(Printer *p, TsNode *node)
{
	if (node)
		pipeline_emit(p, HINT_Unspecified, node);
}

static void emit_identifier_name(Printer *p, TsNode *node)
{
	if (node)
		pipeline_emit(p, HINT_IdentifierName, node);
}

static void emit_expression(Printer *p, TsNode *node)
{
	if (node)
		pipeline_emit(p, HINT_Expression, node);
}

/* ---- writers ---- */

static void write_s(Printer *p, const char *s)
{
	writer_write(&p->w, s, strlen(s));
}

#define write_punctuation write_s
#define write_keyword write_s
#define write_operator write_s

static void write_space(Printer *p)
{
	writer_write(&p->w, " ", 1);
}

static void write_trailing_semicolon(Printer *p)
{
	writer_write(&p->w, ";", 1);
}

static void write_line(Printer *p, int count)
{
	for (int i = 0; i < count; i++)
		writer_write_line(&p->w, i > 0);
}

static void increase_indent(Printer *p)
{
	p->w.indent++;
}

static void decrease_indent(Printer *p)
{
	p->w.indent--;
}

static void write_token_text(Printer *p, int token)
{
	const char *s = ts_token_to_string((TsSyntaxKind)token);
	if (!s)
		fail("typescript 3.9.10: no text for token %d", token);
	write_s(p, s);
}

/* writeTokenNode. */
static void write_token_node(Printer *p, const TsNode *node)
{
	write_token_text(p, node->kind);
}

/* emitTokenWithComment: comments of the (empty) source text never exist. */
static void emit_token_with_comment(Printer *p, int token)
{
	write_token_text(p, token);
}

static void write_line_or_space(Printer *p, const TsNode *node)
{
	if (node->emit_flags & TS_EmitFlags_SingleLine)
		write_space(p);
	else
		write_line(p, 1);
}

/* writeLinesAndIndent. */
static void write_lines_and_indent(Printer *p, int line_count, bool write_space_if_not_indenting)
{
	if (line_count) {
		increase_indent(p);
		write_line(p, line_count);
	} else if (write_space_if_not_indenting) {
		write_space(p);
	}
}

/* ---- lists ---- */

static const char *get_opening_bracket(int format)
{
	switch (format & LF_BracketsMask) {
	case LF_Braces: return "{";
	case LF_Parenthesis: return "(";
	case LF_AngleBrackets: return "<";
	default: return "[";
	}
}

static const char *get_closing_bracket(int format)
{
	switch (format & LF_BracketsMask) {
	case LF_Braces: return "}";
	case LF_Parenthesis: return ")";
	case LF_AngleBrackets: return ">";
	default: return "]";
	}
}

static void write_delimiter(Printer *p, int format)
{
	switch (format & LF_DelimitersMask) {
	case LF_None:
		break;
	case LF_CommaDelimited:
		write_punctuation(p, ",");
		break;
	case LF_BarDelimited:
		write_space(p);
		write_punctuation(p, "|");
		break;
	case LF_AsteriskDelimited:
		write_space(p);
		write_punctuation(p, "*");
		write_space(p);
		break;
	case LF_AmpersandDelimited:
		write_space(p);
		write_punctuation(p, "&");
		break;
	}
}

/* synthesizedNodeStartsOnNewLine (emitNode.startsOnNewLine is never set). */
static bool synthesized_node_starts_on_new_line(const TsNode *node, int format)
{
	(void)node;
	return (format & LF_PreferNewLine) != 0;
}

/* getLeadingLineTerminatorCount. A parsed parent with a parsed first child is on one line. */
static int get_leading_line_terminator_count(int parent_pos, const TsNodeArray *children, int format)
{
	if (format & LF_PreserveLines) {
		if (format & LF_PreferNewLine)
			return 1;
		if (!children || !children->n)
			return 0; /* rangeIsOnSingleLine(parentNode) */
		const TsNode *first = children->v[0];
		if (!position_is_synthesized(parent_pos) && !node_is_synthesized(first))
			return 0; /* rangeStartPositionsAreOnSameLine */
		if (synthesized_node_starts_on_new_line(first, format))
			return 1;
	}
	return format & LF_MultiLine ? 1 : 0;
}

/* getSeparatingLineTerminatorCount. */
static int get_separating_line_terminator_count(const TsNode *previous, const TsNode *next, int format)
{
	if (format & LF_PreserveLines) {
		if (!previous || !next)
			return 0;
		if (!node_is_synthesized(previous) && !node_is_synthesized(next))
			return 0; /* rangeEndIsOnSameLineAsRangeStart */
		if (synthesized_node_starts_on_new_line(previous, format) || synthesized_node_starts_on_new_line(next, format))
			return 1;
	}
	return format & LF_MultiLine ? 1 : 0;
}

/* getClosingLineTerminatorCount. */
static int get_closing_line_terminator_count(int parent_pos, const TsNodeArray *children, int format)
{
	if (format & LF_PreserveLines) {
		if (format & LF_PreferNewLine)
			return 1;
		if (!children || !children->n)
			return 0;
		const TsNode *last = children->v[children->n - 1];
		if (!position_is_synthesized(parent_pos) && !node_is_synthesized(last))
			return 0; /* rangeEndPositionsAreOnSameLine */
		if (synthesized_node_starts_on_new_line(last, format))
			return 1;
	}
	if ((format & LF_MultiLine) && !(format & LF_NoTrailingNewLine))
		return 1;
	return 0;
}

/* emitNodeList; parent_pos is parentNode.pos. */
static void emit_node_list(Printer *p, void (*emit_fn)(Printer *, TsNode *), int parent_pos,
			   const TsNodeArray *children, int format, int start, int count)
{
	bool is_undefined = children == NULL;
	if (is_undefined && (format & LF_OptionalIfUndefined))
		return;
	bool is_empty = is_undefined || start >= children->n || count == 0;
	if (is_empty && (format & LF_OptionalIfEmpty))
		return;
	if (format & LF_BracketsMask)
		write_punctuation(p, get_opening_bracket(format));
	if (is_empty) {
		if (format & LF_MultiLine)
			write_line(p, 1);
		else if ((format & LF_SpaceBetweenBraces) && !(format & LF_NoSpaceIfEmpty))
			write_space(p);
	} else {
		int leading = get_leading_line_terminator_count(parent_pos, children, format);
		if (leading)
			write_line(p, leading);
		else if (format & LF_SpaceBetweenBraces)
			write_space(p);
		if (format & LF_Indented)
			increase_indent(p);
		const TsNode *previous = NULL;
		bool should_decrease_indent_after_emit = false;
		for (int i = 0; i < count; i++) {
			TsNode *child = children->v[start + i];
			if (format & LF_AsteriskDelimited) {
				write_line(p, 1);
				write_delimiter(p, format);
			} else if (previous) {
				write_delimiter(p, format);
				int sep = get_separating_line_terminator_count(previous, child, format);
				if (sep > 0) {
					if ((format & (LF_LinesMask | LF_Indented)) == LF_SingleLine) {
						increase_indent(p);
						should_decrease_indent_after_emit = true;
					}
					write_line(p, sep);
				} else if (format & LF_SpaceBetweenSiblings) {
					write_space(p);
				}
			}
			emit_fn(p, child);
			if (should_decrease_indent_after_emit) {
				decrease_indent(p);
				should_decrease_indent_after_emit = false;
			}
			previous = child;
		}
		if ((format & LF_CommaDelimited) && (format & LF_AllowTrailingComma) && children->has_trailing_comma)
			write_punctuation(p, ",");
		if (format & LF_Indented)
			decrease_indent(p);
		int closing = get_closing_line_terminator_count(parent_pos, children, format);
		if (closing)
			write_line(p, closing);
		else if (format & (LF_SpaceAfterList | LF_SpaceBetweenBraces))
			write_space(p);
	}
	if (format & LF_BracketsMask)
		write_punctuation(p, get_closing_bracket(format));
}

static void emit_list_from(Printer *p, const TsNode *parent, const TsNodeArray *children, int format, int start)
{
	emit_node_list(p, emit, parent->pos, children, format, start, children ? children->n - start : 0);
}

static void emit_list(Printer *p, const TsNode *parent, const TsNodeArray *children, int format)
{
	emit_list_from(p, parent, children, format, 0);
}

static void emit_expression_list(Printer *p, const TsNode *parent, const TsNodeArray *children, int format)
{
	emit_node_list(p, emit_expression, parent->pos, children, format, 0, children ? children->n : 0);
}

/* ---- helpers ---- */

static void emit_modifiers(Printer *p, const TsNode *node, const TsNodeArray *modifiers)
{
	if (modifiers && modifiers->n) {
		emit_list(p, node, modifiers, LF_Modifiers);
		write_space(p);
	}
}

static void emit_decorators(Printer *p, const TsNode *node, const TsNodeArray *decorators)
{
	emit_list(p, node, decorators, LF_Decorators);
}

static void emit_type_annotation(Printer *p, TsNode *node)
{
	if (node) {
		write_punctuation(p, ":");
		write_space(p);
		emit(p, node);
	}
}

static void emit_initializer(Printer *p, TsNode *node)
{
	if (node) {
		write_space(p);
		emit_token_with_comment(p, TS_EqualsToken);
		write_space(p);
		emit_expression(p, node);
	}
}

static void emit_with_leading_space(Printer *p, TsNode *node)
{
	if (node) {
		write_space(p);
		emit(p, node);
	}
}

static void emit_expression_with_leading_space(Printer *p, TsNode *node)
{
	if (node) {
		write_space(p);
		emit_expression(p, node);
	}
}

static void emit_with_trailing_space(Printer *p, TsNode *node)
{
	if (node) {
		emit(p, node);
		write_space(p);
	}
}

static void emit_embedded_statement(Printer *p, const TsNode *parent, TsNode *node)
{
	if (node->kind == TS_Block || (parent->emit_flags & TS_EmitFlags_SingleLine)) {
		write_space(p);
		emit(p, node);
	} else {
		write_line(p, 1);
		increase_indent(p);
		if (node->kind == TS_EmptyStatement)
			pipeline_emit(p, HINT_EmbeddedStatement, node);
		else
			emit(p, node);
		decrease_indent(p);
	}
}

static void emit_type_arguments(Printer *p, const TsNode *parent, const TsNodeArray *type_arguments)
{
	emit_list(p, parent, type_arguments, LF_TypeArguments);
}

static void emit_type_parameters(Printer *p, const TsNode *parent, const TsNodeArray *type_parameters)
{
	/* isFunctionLike(parentNode) && parentNode.typeArguments: quick info only */
	emit_list(p, parent, type_parameters, LF_TypeParameters);
}

static void emit_parameters(Printer *p, const TsNode *parent, const TsNodeArray *parameters)
{
	emit_list(p, parent, parameters, LF_Parameters);
}

static bool can_emit_simple_arrow_head(const TsNode *parent, const TsNodeArray *parameters)
{
	if (!parameters || parameters->n != 1)
		return false;
	const TsNode *parameter = parameters->v[0];
	return parameter->pos == parent->pos && parent->kind == TS_ArrowFunction && !parent->type &&
	       !(parent->decorators && parent->decorators->n) && !(parent->modifiers && parent->modifiers->n) &&
	       !(parent->type_parameters && parent->type_parameters->n) &&
	       !(parameter->decorators && parameter->decorators->n) &&
	       !(parameter->modifiers && parameter->modifiers->n) && !parameter->dot_dot_dot_token &&
	       !parameter->question_token && !parameter->type && !parameter->initializer &&
	       parameter->name->kind == TS_Identifier;
}

static void emit_parameters_for_arrow(Printer *p, const TsNode *parent, const TsNodeArray *parameters)
{
	if (can_emit_simple_arrow_head(parent, parameters))
		emit_list(p, parent, parameters, LF_Parameters & ~LF_Parenthesis);
	else
		emit_parameters(p, parent, parameters);
}

/* ---- names ---- */

static void emit_identifier(Printer *p, TsNode *node)
{
	writer_write(&p->w, node->text.p, node->text.n);
	emit_list(p, node, node->type_arguments, LF_TypeParameters);
}

static void emit_qualified_name(Printer *p, TsNode *node)
{
	if (node->left->kind == TS_Identifier)
		emit_expression(p, node->left);
	else
		emit(p, node->left);
	write_punctuation(p, ".");
	emit(p, node->right);
}

static void emit_computed_property_name(Printer *p, TsNode *node)
{
	write_punctuation(p, "[");
	emit_expression(p, node->expression);
	write_punctuation(p, "]");
}

/* ---- signature elements ---- */

static void emit_type_parameter(Printer *p, TsNode *node)
{
	emit(p, node->name);
	if (node->constraint) {
		write_space(p);
		write_keyword(p, "extends");
		write_space(p);
		emit(p, node->constraint);
	}
	if (node->default_) {
		write_space(p);
		write_operator(p, "=");
		write_space(p);
		emit(p, node->default_);
	}
}

static void emit_parameter(Printer *p, TsNode *node)
{
	emit_decorators(p, node, node->decorators);
	emit_modifiers(p, node, node->modifiers);
	emit(p, node->dot_dot_dot_token);
	emit(p, node->name); /* emitNodeWithWriter(node.name, writeParameter) */
	emit(p, node->question_token);
	emit_type_annotation(p, node->type);
	emit_initializer(p, node->initializer);
}

/* ---- type members ---- */

static void emit_property_signature(Printer *p, TsNode *node)
{
	emit_decorators(p, node, node->decorators);
	emit_modifiers(p, node, node->modifiers);
	emit(p, node->name); /* emitNodeWithWriter(node.name, writeProperty) */
	emit(p, node->question_token);
	emit_type_annotation(p, node->type);
	write_trailing_semicolon(p);
}

static void emit_property_declaration(Printer *p, TsNode *node)
{
	emit_decorators(p, node, node->decorators);
	emit_modifiers(p, node, node->modifiers);
	emit(p, node->name);
	emit(p, node->question_token);
	emit(p, node->exclamation_token);
	emit_type_annotation(p, node->type);
	emit_initializer(p, node->initializer);
	write_trailing_semicolon(p);
}

static void emit_method_signature(Printer *p, TsNode *node)
{
	emit_decorators(p, node, node->decorators);
	emit_modifiers(p, node, node->modifiers);
	emit(p, node->name);
	emit(p, node->question_token);
	emit_type_parameters(p, node, node->type_parameters);
	emit_parameters(p, node, node->parameters);
	emit_type_annotation(p, node->type);
	write_trailing_semicolon(p);
}

static void emit_signature_head(Printer *p, TsNode *node)
{
	emit_type_parameters(p, node, node->type_parameters);
	emit_parameters(p, node, node->parameters);
	emit_type_annotation(p, node->type);
}

static void emit_arrow_function_head(Printer *p, TsNode *node)
{
	emit_type_parameters(p, node, node->type_parameters);
	emit_parameters_for_arrow(p, node, node->parameters);
	emit_type_annotation(p, node->type);
	write_space(p);
	emit(p, node->equals_greater_than_token);
}

static int emit_prologue_directives(Printer *p, const TsNodeArray *statements);

/* shouldEmitBlockFunctionBodyOnSingleLine. */
static bool should_emit_block_function_body_on_single_line(const TsNode *body)
{
	if (body->emit_flags & TS_EmitFlags_SingleLine)
		return true;
	if (body->multi_line)
		return false;
	/* !nodeIsSynthesized(body) && !rangeIsOnSingleLine(body): parsed bodies are on one line */
	if (get_leading_line_terminator_count(body->pos, body->statements, LF_PreserveLines) ||
	    get_closing_line_terminator_count(body->pos, body->statements, LF_PreserveLines))
		return false;
	const TsNode *previous = NULL;
	for (int i = 0; i < body->statements->n; i++) {
		const TsNode *statement = body->statements->v[i];
		if (get_separating_line_terminator_count(previous, statement, LF_PreserveLines) > 0)
			return false;
		previous = statement;
	}
	return true;
}

static void emit_block_function_body_worker(Printer *p, TsNode *body, bool on_single_line)
{
	int statement_offset = emit_prologue_directives(p, body->statements);
	if (statement_offset == 0 && on_single_line) {
		decrease_indent(p);
		emit_list(p, body, body->statements, LF_SingleLineFunctionBodyStatements);
		increase_indent(p);
	} else {
		emit_list_from(p, body, body->statements, LF_MultiLineFunctionBodyStatements, statement_offset);
	}
}

static void emit_block_function_body(Printer *p, TsNode *body)
{
	write_space(p);
	write_punctuation(p, "{");
	increase_indent(p);
	emit_block_function_body_worker(p, body, should_emit_block_function_body_on_single_line(body));
	decrease_indent(p);
	write_token_text(p, TS_CloseBraceToken);
}

static void emit_signature_and_body(Printer *p, TsNode *node, void (*head)(Printer *, TsNode *))
{
	TsNode *body = node->body;
	if (body) {
		if (body->kind == TS_Block) {
			bool indented = (node->emit_flags & TS_EmitFlags_Indented) != 0;
			if (indented)
				increase_indent(p);
			head(p, node);
			emit_block_function_body(p, body);
			if (indented)
				decrease_indent(p);
		} else {
			head(p, node);
			write_space(p);
			emit_expression(p, body);
		}
	} else {
		head(p, node);
		write_trailing_semicolon(p);
	}
}

static void emit_method_declaration(Printer *p, TsNode *node)
{
	emit_decorators(p, node, node->decorators);
	emit_modifiers(p, node, node->modifiers);
	emit(p, node->asterisk_token);
	emit(p, node->name);
	emit(p, node->question_token);
	emit_signature_and_body(p, node, emit_signature_head);
}

static void emit_constructor(Printer *p, TsNode *node)
{
	emit_modifiers(p, node, node->modifiers);
	write_keyword(p, "constructor");
	emit_signature_and_body(p, node, emit_signature_head);
}

static void emit_index_signature(Printer *p, TsNode *node)
{
	emit_decorators(p, node, node->decorators);
	emit_modifiers(p, node, node->modifiers);
	emit_list(p, node, node->parameters, LF_IndexSignatureParameters);
	emit_type_annotation(p, node->type);
	write_trailing_semicolon(p);
}

/* ---- types ---- */

static void emit_type_reference(Printer *p, TsNode *node)
{
	emit(p, node->type_name);
	emit_type_arguments(p, node, node->type_arguments);
}

static void emit_function_type(Printer *p, TsNode *node)
{
	emit_type_parameters(p, node, node->type_parameters);
	emit_parameters_for_arrow(p, node, node->parameters);
	write_space(p);
	write_punctuation(p, "=>");
	write_space(p);
	emit(p, node->type);
}

static void emit_type_query(Printer *p, TsNode *node)
{
	write_keyword(p, "typeof");
	write_space(p);
	emit(p, node->expression); /* exprName */
}

static void emit_type_literal(Printer *p, TsNode *node)
{
	write_punctuation(p, "{");
	int flags = node->emit_flags & TS_EmitFlags_SingleLine ? LF_SingleLineTypeLiteralMembers
								 : LF_MultiLineTypeLiteralMembers;
	emit_list(p, node, node->members, flags | LF_NoSpaceIfEmpty);
	write_punctuation(p, "}");
}

static void emit_array_type(Printer *p, TsNode *node)
{
	emit(p, node->element_type);
	write_punctuation(p, "[");
	write_punctuation(p, "]");
}

static void emit_tuple_type(Printer *p, TsNode *node)
{
	write_punctuation(p, "[");
	emit_list(p, node, node->elements, LF_TupleTypeElements);
	write_punctuation(p, "]");
}

static void emit_parenthesized_type(Printer *p, TsNode *node)
{
	write_punctuation(p, "(");
	emit(p, node->type);
	write_punctuation(p, ")");
}

static void emit_type_operator(Printer *p, TsNode *node)
{
	write_token_text(p, node->operator_);
	write_space(p);
	emit(p, node->type);
}

static void emit_indexed_access_type(Printer *p, TsNode *node)
{
	emit(p, node->object_type);
	write_punctuation(p, "[");
	emit(p, node->index_type);
	write_punctuation(p, "]");
}

/* ---- binding patterns ---- */

static void emit_object_binding_pattern(Printer *p, TsNode *node)
{
	write_punctuation(p, "{");
	emit_list(p, node, node->elements, LF_ObjectBindingPatternElements);
	write_punctuation(p, "}");
}

static void emit_array_binding_pattern(Printer *p, TsNode *node)
{
	write_punctuation(p, "[");
	emit_list(p, node, node->elements, LF_ArrayBindingPatternElements);
	write_punctuation(p, "]");
}

static void emit_binding_element(Printer *p, TsNode *node)
{
	emit(p, node->dot_dot_dot_token);
	if (node->property_name) {
		emit(p, node->property_name);
		write_punctuation(p, ":");
		write_space(p);
	}
	emit(p, node->name);
	emit_initializer(p, node->initializer);
}

/* ---- expressions ---- */

static void emit_literal(Printer *p, TsNode *node)
{
	Str text = get_literal_text(node);
	writer_write(&p->w, text.p, text.n);
}

static void emit_array_literal_expression(Printer *p, TsNode *node)
{
	int prefer_new_line = node->multi_line ? LF_PreferNewLine : LF_None;
	emit_expression_list(p, node, node->elements, LF_ArrayLiteralExpressionElements | prefer_new_line);
}

static void emit_object_literal_expression(Printer *p, TsNode *node)
{
	bool indented = (node->emit_flags & TS_EmitFlags_Indented) != 0;
	if (indented)
		increase_indent(p);
	int prefer_new_line = node->multi_line ? LF_PreferNewLine : LF_None;
	/* languageVersion Latest >= ES5, not a JSON file */
	emit_list(p, node, node->properties, LF_ObjectLiteralExpressionProperties | LF_AllowTrailingComma | prefer_new_line);
	if (indented)
		decrease_indent(p);
}

/* mayNeedDotDotForPropertyAccess (getConstantValue is undefined without a checker). */
static bool may_need_dot_dot_for_property_access(const TsNode *expression)
{
	if (expression->kind == TS_NumericLiteral) {
		Str text = get_literal_text(expression);
		return !expression->numeric_literal_flags && !memchr(text.p, '.', text.n);
	}
	return false;
}

static void emit_property_access_expression(Printer *p, TsNode *node)
{
	emit_expression(p, node->expression);
	/* getLinesBetweenNodes: 0 (NoIndentation, synthesized, or parsed on the one line) */
	bool should_emit_dot_dot = !node->question_dot_token && may_need_dot_dot_for_property_access(node->expression) &&
				   !p->w.has_trailing_comment && !writer_has_trailing_whitespace(&p->w);
	if (should_emit_dot_dot)
		write_punctuation(p, ".");
	if (node->question_dot_token)
		emit(p, node->question_dot_token);
	else
		emit_token_with_comment(p, TS_DotToken);
	emit(p, node->name);
}

static void emit_element_access_expression(Printer *p, TsNode *node)
{
	emit_expression(p, node->expression);
	emit(p, node->question_dot_token);
	emit_token_with_comment(p, TS_OpenBracketToken);
	emit_expression(p, node->argument_expression);
	emit_token_with_comment(p, TS_CloseBracketToken);
}

static void emit_call_expression(Printer *p, TsNode *node)
{
	emit_expression(p, node->expression);
	emit(p, node->question_dot_token);
	emit_type_arguments(p, node, node->type_arguments);
	emit_expression_list(p, node, node->arguments, LF_CallExpressionArguments);
}

static void emit_new_expression(Printer *p, TsNode *node)
{
	emit_token_with_comment(p, TS_NewKeyword);
	write_space(p);
	emit_expression(p, node->expression);
	emit_type_arguments(p, node, node->type_arguments);
	emit_expression_list(p, node, node->arguments, LF_NewExpressionArguments);
}

static void emit_type_assertion_expression(Printer *p, TsNode *node)
{
	write_punctuation(p, "<");
	emit(p, node->type);
	write_punctuation(p, ">");
	emit_expression(p, node->expression);
}

static void emit_parenthesized_expression(Printer *p, TsNode *node)
{
	emit_token_with_comment(p, TS_OpenParenToken);
	emit_expression(p, node->expression);
	emit_token_with_comment(p, TS_CloseParenToken);
}

static void emit_function_declaration_or_expression(Printer *p, TsNode *node)
{
	emit_decorators(p, node, node->decorators);
	emit_modifiers(p, node, node->modifiers);
	write_keyword(p, "function");
	emit(p, node->asterisk_token);
	write_space(p);
	emit_identifier_name(p, node->name);
	emit_signature_and_body(p, node, emit_signature_head);
}

static void emit_arrow_function(Printer *p, TsNode *node)
{
	emit_decorators(p, node, node->decorators);
	emit_modifiers(p, node, node->modifiers);
	emit_signature_and_body(p, node, emit_arrow_function_head);
}

static void emit_keyword_operand_expression(Printer *p, TsNode *node, int keyword)
{
	emit_token_with_comment(p, keyword);
	write_space(p);
	emit_expression(p, node->expression);
}

/* shouldEmitWhitespaceBeforeOperand. */
static bool should_emit_whitespace_before_operand(const TsNode *node)
{
	const TsNode *operand = node->operand;
	return operand->kind == TS_PrefixUnaryExpression &&
	       ((node->operator_ == TS_PlusToken &&
		 (operand->operator_ == TS_PlusToken || operand->operator_ == TS_PlusPlusToken)) ||
		(node->operator_ == TS_MinusToken &&
		 (operand->operator_ == TS_MinusToken || operand->operator_ == TS_MinusMinusToken)));
}

static void emit_prefix_unary_expression(Printer *p, TsNode *node)
{
	write_token_text(p, node->operator_);
	if (should_emit_whitespace_before_operand(node))
		write_space(p);
	emit_expression(p, node->operand);
}

static void emit_postfix_unary_expression(Printer *p, TsNode *node)
{
	emit_expression(p, node->operand);
	write_token_text(p, node->operator_);
}

/* emitBinaryExpression (recursive; the work stack of TypeScript only avoids deep recursion). */
static void emit_binary_expression(Printer *p, TsNode *node)
{
	emit_expression(p, node->left);
	/* linesBeforeOperator / linesAfterOperator are 0 */
	bool is_comma_operator = node->operator_token->kind != TS_CommaToken; /* sic */
	write_lines_and_indent(p, 0, is_comma_operator);
	write_token_node(p, node->operator_token);
	write_lines_and_indent(p, 0, true);
	emit_expression(p, node->right);
}

static void emit_conditional_expression(Printer *p, TsNode *node)
{
	emit_expression(p, node->condition);
	write_lines_and_indent(p, 0, true);
	emit(p, node->question_token);
	write_lines_and_indent(p, 0, true);
	emit_expression(p, node->when_true);
	write_lines_and_indent(p, 0, true);
	emit(p, node->colon_token);
	write_lines_and_indent(p, 0, true);
	emit_expression(p, node->when_false);
}

static void emit_template_expression(Printer *p, TsNode *node)
{
	emit(p, node->head);
	emit_list(p, node, node->template_spans, LF_TemplateExpressionSpans);
}

static void emit_spread_expression(Printer *p, TsNode *node)
{
	emit_token_with_comment(p, TS_DotDotDotToken);
	emit_expression(p, node->expression);
}

static void emit_heritage_clause(Printer *p, TsNode *node);

static void emit_expression_with_type_arguments(Printer *p, TsNode *node)
{
	emit_expression(p, node->expression);
	emit_type_arguments(p, node, node->type_arguments);
}

static void emit_as_expression(Printer *p, TsNode *node)
{
	emit_expression(p, node->expression);
	if (node->type) {
		write_space(p);
		write_keyword(p, "as");
		write_space(p);
		emit(p, node->type);
	}
}

static void emit_non_null_expression(Printer *p, TsNode *node)
{
	emit_expression(p, node->expression);
	write_operator(p, "!");
}

static void emit_template_span(Printer *p, TsNode *node)
{
	emit_expression(p, node->expression);
	emit(p, node->literal);
}

/* ---- statements ---- */

static void emit_block_statements(Printer *p, TsNode *node, bool force_single_line)
{
	emit_token_with_comment(p, TS_OpenBraceToken);
	int format = force_single_line || (node->emit_flags & TS_EmitFlags_SingleLine) ? LF_SingleLineBlockStatements
											 : LF_MultiLineBlockStatements;
	emit_list(p, node, node->statements, format);
	emit_token_with_comment(p, TS_CloseBraceToken);
}

/* isEmptyBlock: no statements (and its range on one line, always true here). */
static bool is_empty_block(const TsNode *block)
{
	return block->statements->n == 0;
}

static void emit_block(Printer *p, TsNode *node)
{
	emit_block_statements(p, node, !node->multi_line && is_empty_block(node));
}

static void emit_variable_statement(Printer *p, TsNode *node)
{
	emit_modifiers(p, node, node->modifiers);
	emit(p, node->declaration_list);
	write_trailing_semicolon(p);
}

static void emit_expression_statement(Printer *p, TsNode *node)
{
	emit_expression(p, node->expression);
	write_trailing_semicolon(p);
}

static void emit_if_statement(Printer *p, TsNode *node)
{
	emit_token_with_comment(p, TS_IfKeyword);
	write_space(p);
	emit_token_with_comment(p, TS_OpenParenToken);
	emit_expression(p, node->expression);
	emit_token_with_comment(p, TS_CloseParenToken);
	emit_embedded_statement(p, node, node->then_statement);
	if (node->else_statement) {
		write_line_or_space(p, node);
		emit_token_with_comment(p, TS_ElseKeyword);
		if (node->else_statement->kind == TS_IfStatement) {
			write_space(p);
			emit(p, node->else_statement);
		} else {
			emit_embedded_statement(p, node, node->else_statement);
		}
	}
}

static void emit_while_statement(Printer *p, TsNode *node)
{
	emit_token_with_comment(p, TS_WhileKeyword);
	write_space(p);
	emit_token_with_comment(p, TS_OpenParenToken);
	emit_expression(p, node->expression);
	emit_token_with_comment(p, TS_CloseParenToken);
	emit_embedded_statement(p, node, node->statement);
}

static void emit_for_binding(Printer *p, TsNode *node)
{
	if (node) {
		if (node->kind == TS_VariableDeclarationList)
			emit(p, node);
		else
			emit_expression(p, node);
	}
}

static void emit_for_statement(Printer *p, TsNode *node)
{
	emit_token_with_comment(p, TS_ForKeyword);
	write_space(p);
	emit_token_with_comment(p, TS_OpenParenToken);
	emit_for_binding(p, node->initializer);
	emit_token_with_comment(p, TS_SemicolonToken);
	emit_expression_with_leading_space(p, node->condition);
	emit_token_with_comment(p, TS_SemicolonToken);
	emit_expression_with_leading_space(p, node->incrementor);
	emit_token_with_comment(p, TS_CloseParenToken);
	emit_embedded_statement(p, node, node->statement);
}

static void emit_for_in_or_of_statement(Printer *p, TsNode *node, int keyword)
{
	emit_token_with_comment(p, TS_ForKeyword);
	write_space(p);
	emit_with_trailing_space(p, node->await_modifier);
	emit_token_with_comment(p, TS_OpenParenToken);
	emit_for_binding(p, node->initializer);
	write_space(p);
	emit_token_with_comment(p, keyword);
	write_space(p);
	emit_expression(p, node->expression);
	emit_token_with_comment(p, TS_CloseParenToken);
	emit_embedded_statement(p, node, node->statement);
}

static void emit_break_or_continue_statement(Printer *p, TsNode *node, int keyword)
{
	emit_token_with_comment(p, keyword);
	emit_with_leading_space(p, node->label);
	write_trailing_semicolon(p);
}

static void emit_return_or_throw_statement(Printer *p, TsNode *node, int keyword)
{
	emit_token_with_comment(p, keyword);
	emit_expression_with_leading_space(p, node->expression);
	write_trailing_semicolon(p);
}

static void emit_switch_statement(Printer *p, TsNode *node)
{
	emit_token_with_comment(p, TS_SwitchKeyword);
	write_space(p);
	emit_token_with_comment(p, TS_OpenParenToken);
	emit_expression(p, node->expression);
	emit_token_with_comment(p, TS_CloseParenToken);
	write_space(p);
	emit(p, node->case_block);
}

/* ---- declarations ---- */

static void emit_variable_declaration(Printer *p, TsNode *node)
{
	emit(p, node->name);
	emit(p, node->exclamation_token);
	emit_type_annotation(p, node->type);
	emit_initializer(p, node->initializer);
}

static void emit_variable_declaration_list(Printer *p, TsNode *node)
{
	write_keyword(p, node->flags & TS_NodeFlags_Let ? "let" : node->flags & TS_NodeFlags_Const ? "const" : "var");
	write_space(p);
	emit_list(p, node, node->declarations, LF_VariableDeclarationList);
}

static void emit_class_declaration(Printer *p, TsNode *node)
{
	emit_decorators(p, node, node->decorators);
	emit_modifiers(p, node, node->modifiers);
	write_keyword(p, "class");
	if (node->name) {
		write_space(p);
		emit_identifier_name(p, node->name);
	}
	bool indented = (node->emit_flags & TS_EmitFlags_Indented) != 0;
	if (indented)
		increase_indent(p);
	emit_type_parameters(p, node, node->type_parameters);
	emit_list(p, node, node->heritage_clauses, LF_ClassHeritageClauses);
	write_space(p);
	write_punctuation(p, "{");
	emit_list(p, node, node->members, LF_ClassMembers);
	write_punctuation(p, "}");
	if (indented)
		decrease_indent(p);
}

static void emit_interface_declaration(Printer *p, TsNode *node)
{
	emit_decorators(p, node, node->decorators);
	emit_modifiers(p, node, node->modifiers);
	write_keyword(p, "interface");
	write_space(p);
	emit(p, node->name);
	emit_type_parameters(p, node, node->type_parameters);
	emit_list(p, node, node->heritage_clauses, LF_HeritageClauses);
	write_space(p);
	write_punctuation(p, "{");
	emit_list(p, node, node->members, LF_InterfaceMembers);
	write_punctuation(p, "}");
}

static void emit_enum_declaration(Printer *p, TsNode *node)
{
	emit_modifiers(p, node, node->modifiers);
	write_keyword(p, "enum");
	write_space(p);
	emit(p, node->name);
	write_space(p);
	write_punctuation(p, "{");
	emit_list(p, node, node->members, LF_EnumMembers);
	write_punctuation(p, "}");
}

static void emit_case_block(Printer *p, TsNode *node)
{
	emit_token_with_comment(p, TS_OpenBraceToken);
	emit_list(p, node, node->clauses, LF_CaseBlockClauses);
	emit_token_with_comment(p, TS_CloseBraceToken);
}

static void emit_import_declaration(Printer *p, TsNode *node)
{
	emit_modifiers(p, node, node->modifiers);
	emit_token_with_comment(p, TS_ImportKeyword);
	write_space(p);
	if (node->import_clause) {
		emit(p, node->import_clause);
		write_space(p);
		emit_token_with_comment(p, TS_FromKeyword);
		write_space(p);
	}
	emit_expression(p, node->module_specifier);
	write_trailing_semicolon(p);
}

static void emit_import_clause(Printer *p, TsNode *node)
{
	if (node->is_type_only) {
		emit_token_with_comment(p, TS_TypeKeyword);
		write_space(p);
	}
	emit(p, node->name);
	if (node->name && node->named_bindings) {
		emit_token_with_comment(p, TS_CommaToken);
		write_space(p);
	}
	emit(p, node->named_bindings);
}

static void emit_namespace_import(Printer *p, TsNode *node)
{
	emit_token_with_comment(p, TS_AsteriskToken);
	write_space(p);
	emit_token_with_comment(p, TS_AsKeyword);
	write_space(p);
	emit(p, node->name);
}

static void emit_named_imports(Printer *p, TsNode *node)
{
	write_punctuation(p, "{");
	emit_list(p, node, node->elements, LF_NamedImportsOrExportsElements);
	write_punctuation(p, "}");
}

static void emit_import_specifier(Printer *p, TsNode *node)
{
	if (node->property_name) {
		emit(p, node->property_name);
		write_space(p);
		emit_token_with_comment(p, TS_AsKeyword);
		write_space(p);
	}
	emit(p, node->name);
}

/* ---- clauses ---- */

static void emit_case_or_default_clause_rest(Printer *p, TsNode *parent, const TsNodeArray *statements)
{
	/* synthesized nodes count as on the same line, parsed ones are on the one line */
	bool emit_as_single_statement = statements->n == 1;
	int format = LF_CaseOrDefaultClauseStatements;
	if (emit_as_single_statement) {
		write_token_text(p, TS_ColonToken);
		write_space(p);
		format &= ~(LF_MultiLine | LF_Indented);
	} else {
		emit_token_with_comment(p, TS_ColonToken);
	}
	emit_list(p, parent, statements, format);
}

static void emit_case_clause(Printer *p, TsNode *node)
{
	emit_token_with_comment(p, TS_CaseKeyword);
	write_space(p);
	emit_expression(p, node->expression);
	emit_case_or_default_clause_rest(p, node, node->statements);
}

static void emit_default_clause(Printer *p, TsNode *node)
{
	emit_token_with_comment(p, TS_DefaultKeyword);
	emit_case_or_default_clause_rest(p, node, node->statements);
}

static void emit_heritage_clause(Printer *p, TsNode *node)
{
	write_space(p);
	write_token_text(p, node->token);
	write_space(p);
	emit_list(p, node, node->types, LF_HeritageClauseTypes);
}

/* ---- property assignments ---- */

static void emit_property_assignment(Printer *p, TsNode *node)
{
	emit(p, node->name);
	write_punctuation(p, ":");
	write_space(p);
	emit_expression(p, node->initializer);
}

static void emit_shorthand_property_assignment(Printer *p, TsNode *node)
{
	emit(p, node->name);
	if (node->initializer) { /* objectAssignmentInitializer */
		write_space(p);
		write_punctuation(p, "=");
		write_space(p);
		emit_expression(p, node->initializer);
	}
}

static void emit_spread_assignment(Printer *p, TsNode *node)
{
	if (node->expression) {
		emit_token_with_comment(p, TS_DotDotDotToken);
		emit_expression(p, node->expression);
	}
}

static void emit_enum_member(Printer *p, TsNode *node)
{
	emit(p, node->name);
	emit_initializer(p, node->initializer);
}

/* ---- top level ---- */

static int emit_prologue_directives(Printer *p, const TsNodeArray *statements)
{
	for (int i = 0; i < statements->n; i++) {
		TsNode *statement = statements->v[i];
		if (!is_prologue_directive(statement))
			return i;
		write_line(p, 1);
		emit(p, statement);
	}
	return statements->n;
}

static void emit_source_file(Printer *p, TsNode *node)
{
	write_line(p, 1);
	/* emitBodyWithDetachedComments: no comments in the empty text */
	const TsNodeArray *statements = node->statements;
	int index = 0;
	while (index < statements->n && is_prologue_directive(statements->v[index]))
		index++;
	emit_list_from(p, node, statements, LF_MultiLine, index);
}

/* ---- synthetic comments ---- */

static void write_synthesized_comment(Printer *p, const TsComment *comment)
{
	Buf text = {0};
	buf_adds(&text, comment->kind == TS_MultiLineCommentTrivia ? "/*" : "//");
	buf_add_str(&text, comment->text);
	if (comment->kind == TS_MultiLineCommentTrivia)
		buf_adds(&text, "*/");
	write_comment_range(&p->w, buf_view(&text), 0, text.n);
}

static void emit_leading_synthesized_comment(Printer *p, const TsComment *comment)
{
	if (comment->kind == TS_SingleLineCommentTrivia)
		writer_write_line(&p->w, false);
	write_synthesized_comment(p, comment);
	if (comment->has_trailing_new_line || comment->kind == TS_SingleLineCommentTrivia)
		writer_write_line(&p->w, false);
	else
		writer_write(&p->w, " ", 1);
}

static void emit_trailing_synthesized_comment(Printer *p, const TsComment *comment)
{
	if (!p->w.line_start)
		writer_write(&p->w, " ", 1);
	write_synthesized_comment(p, comment);
	if (comment->has_trailing_new_line)
		writer_write_line(&p->w, false);
}

/* ---- pipeline ---- */

static void pipeline_emit_with_hint(Printer *p, EmitHint hint, TsNode *node);

/* pipelineEmit -> pipelineEmitWithComments (all nodes but the SourceFile) -> pipelineEmitWithHint. */
static void pipeline_emit(Printer *p, EmitHint hint, TsNode *node)
{
	if (node->kind == TS_SourceFile) {
		pipeline_emit_with_hint(p, hint, node);
		return;
	}
	for (int i = 0; i < node->n_leading_comments; i++)
		emit_leading_synthesized_comment(p, &node->leading_comments[i]);
	pipeline_emit_with_hint(p, hint, node);
	for (int i = 0; i < node->n_trailing_comments; i++)
		emit_trailing_synthesized_comment(p, &node->trailing_comments[i]);
}

static void unsupported(const TsNode *node)
{
	fail("typescript 3.9.10: printing node kind %d is not supported", node->kind);
}

static void pipeline_emit_with_hint(Printer *p, EmitHint hint, TsNode *node)
{
	if (hint == HINT_SourceFile || node->kind == TS_SourceFile) {
		emit_source_file(p, node);
		return;
	}
	if (hint == HINT_IdentifierName) {
		emit_identifier(p, node);
		return;
	}
	if (hint == HINT_EmbeddedStatement) {
		write_punctuation(p, ";");
		return;
	}
	if (hint == HINT_Unspecified) {
		if (is_keyword(node->kind)) {
			write_token_node(p, node);
			return;
		}
		switch (node->kind) {
		case TS_TemplateHead:
		case TS_TemplateMiddle:
		case TS_TemplateTail:
			emit_literal(p, node);
			return;
		case TS_Identifier:
			emit_identifier(p, node);
			return;
		case TS_QualifiedName:
			emit_qualified_name(p, node);
			return;
		case TS_ComputedPropertyName:
			emit_computed_property_name(p, node);
			return;
		case TS_TypeParameter:
			emit_type_parameter(p, node);
			return;
		case TS_Parameter:
			emit_parameter(p, node);
			return;
		case TS_PropertySignature:
			emit_property_signature(p, node);
			return;
		case TS_PropertyDeclaration:
			emit_property_declaration(p, node);
			return;
		case TS_MethodSignature:
			emit_method_signature(p, node);
			return;
		case TS_MethodDeclaration:
			emit_method_declaration(p, node);
			return;
		case TS_Constructor:
			emit_constructor(p, node);
			return;
		case TS_IndexSignature:
			emit_index_signature(p, node);
			return;
		case TS_TypeReference:
			emit_type_reference(p, node);
			return;
		case TS_FunctionType:
			emit_function_type(p, node);
			return;
		case TS_TypeQuery:
			emit_type_query(p, node);
			return;
		case TS_TypeLiteral:
			emit_type_literal(p, node);
			return;
		case TS_ArrayType:
			emit_array_type(p, node);
			return;
		case TS_TupleType:
			emit_tuple_type(p, node);
			return;
		case TS_UnionType:
			emit_list(p, node, node->types, LF_UnionTypeConstituents);
			return;
		case TS_IntersectionType:
			emit_list(p, node, node->types, LF_IntersectionTypeConstituents);
			return;
		case TS_ParenthesizedType:
			emit_parenthesized_type(p, node);
			return;
		case TS_ExpressionWithTypeArguments:
			emit_expression_with_type_arguments(p, node);
			return;
		case TS_ThisType:
			write_keyword(p, "this");
			return;
		case TS_TypeOperator:
			emit_type_operator(p, node);
			return;
		case TS_IndexedAccessType:
			emit_indexed_access_type(p, node);
			return;
		case TS_LiteralType:
			emit_expression(p, node->literal);
			return;
		case TS_ObjectBindingPattern:
			emit_object_binding_pattern(p, node);
			return;
		case TS_ArrayBindingPattern:
			emit_array_binding_pattern(p, node);
			return;
		case TS_BindingElement:
			emit_binding_element(p, node);
			return;
		case TS_TemplateSpan:
			emit_template_span(p, node);
			return;
		case TS_SemicolonClassElement:
			write_trailing_semicolon(p);
			return;
		case TS_Block:
			emit_block(p, node);
			return;
		case TS_VariableStatement:
			emit_variable_statement(p, node);
			return;
		case TS_EmptyStatement:
			write_trailing_semicolon(p);
			return;
		case TS_ExpressionStatement:
			emit_expression_statement(p, node);
			return;
		case TS_IfStatement:
			emit_if_statement(p, node);
			return;
		case TS_WhileStatement:
			emit_while_statement(p, node);
			return;
		case TS_ForStatement:
			emit_for_statement(p, node);
			return;
		case TS_ForInStatement:
			emit_for_in_or_of_statement(p, node, TS_InKeyword);
			return;
		case TS_ForOfStatement:
			emit_for_in_or_of_statement(p, node, TS_OfKeyword);
			return;
		case TS_ContinueStatement:
			emit_break_or_continue_statement(p, node, TS_ContinueKeyword);
			return;
		case TS_BreakStatement:
			emit_break_or_continue_statement(p, node, TS_BreakKeyword);
			return;
		case TS_ReturnStatement:
			emit_return_or_throw_statement(p, node, TS_ReturnKeyword);
			return;
		case TS_SwitchStatement:
			emit_switch_statement(p, node);
			return;
		case TS_ThrowStatement:
			emit_return_or_throw_statement(p, node, TS_ThrowKeyword);
			return;
		case TS_VariableDeclaration:
			emit_variable_declaration(p, node);
			return;
		case TS_VariableDeclarationList:
			emit_variable_declaration_list(p, node);
			return;
		case TS_FunctionDeclaration:
			emit_function_declaration_or_expression(p, node);
			return;
		case TS_ClassDeclaration:
			emit_class_declaration(p, node);
			return;
		case TS_InterfaceDeclaration:
			emit_interface_declaration(p, node);
			return;
		case TS_EnumDeclaration:
			emit_enum_declaration(p, node);
			return;
		case TS_CaseBlock:
			emit_case_block(p, node);
			return;
		case TS_ImportDeclaration:
			emit_import_declaration(p, node);
			return;
		case TS_ImportClause:
			emit_import_clause(p, node);
			return;
		case TS_NamespaceImport:
			emit_namespace_import(p, node);
			return;
		case TS_NamedImports:
			emit_named_imports(p, node);
			return;
		case TS_ImportSpecifier:
			emit_import_specifier(p, node);
			return;
		case TS_CaseClause:
			emit_case_clause(p, node);
			return;
		case TS_DefaultClause:
			emit_default_clause(p, node);
			return;
		case TS_HeritageClause:
			emit_heritage_clause(p, node);
			return;
		case TS_PropertyAssignment:
			emit_property_assignment(p, node);
			return;
		case TS_ShorthandPropertyAssignment:
			emit_shorthand_property_assignment(p, node);
			return;
		case TS_SpreadAssignment:
			emit_spread_assignment(p, node);
			return;
		case TS_EnumMember:
			emit_enum_member(p, node);
			return;
		default:
			break;
		}
		if (is_expression_kind(node->kind)) {
			hint = HINT_Expression;
		} else if (is_token_kind(node->kind)) {
			write_token_node(p, node);
			return;
		}
	}
	if (hint == HINT_Expression) {
		switch (node->kind) {
		case TS_NumericLiteral:
		case TS_BigIntLiteral:
		case TS_StringLiteral:
		case TS_RegularExpressionLiteral:
		case TS_NoSubstitutionTemplateLiteral:
			emit_literal(p, node);
			return;
		case TS_Identifier:
			emit_identifier(p, node);
			return;
		case TS_FalseKeyword:
		case TS_NullKeyword:
		case TS_SuperKeyword:
		case TS_TrueKeyword:
		case TS_ThisKeyword:
		case TS_ImportKeyword:
			write_token_node(p, node);
			return;
		case TS_ArrayLiteralExpression:
			emit_array_literal_expression(p, node);
			return;
		case TS_ObjectLiteralExpression:
			emit_object_literal_expression(p, node);
			return;
		case TS_PropertyAccessExpression:
			emit_property_access_expression(p, node);
			return;
		case TS_ElementAccessExpression:
			emit_element_access_expression(p, node);
			return;
		case TS_CallExpression:
			emit_call_expression(p, node);
			return;
		case TS_NewExpression:
			emit_new_expression(p, node);
			return;
		case TS_TypeAssertionExpression:
			emit_type_assertion_expression(p, node);
			return;
		case TS_ParenthesizedExpression:
			emit_parenthesized_expression(p, node);
			return;
		case TS_FunctionExpression:
			emit_function_declaration_or_expression(p, node);
			return;
		case TS_ArrowFunction:
			emit_arrow_function(p, node);
			return;
		case TS_DeleteExpression:
			emit_keyword_operand_expression(p, node, TS_DeleteKeyword);
			return;
		case TS_TypeOfExpression:
			emit_keyword_operand_expression(p, node, TS_TypeOfKeyword);
			return;
		case TS_VoidExpression:
			emit_keyword_operand_expression(p, node, TS_VoidKeyword);
			return;
		case TS_AwaitExpression:
			emit_keyword_operand_expression(p, node, TS_AwaitKeyword);
			return;
		case TS_PrefixUnaryExpression:
			emit_prefix_unary_expression(p, node);
			return;
		case TS_PostfixUnaryExpression:
			emit_postfix_unary_expression(p, node);
			return;
		case TS_BinaryExpression:
			emit_binary_expression(p, node);
			return;
		case TS_ConditionalExpression:
			emit_conditional_expression(p, node);
			return;
		case TS_TemplateExpression:
			emit_template_expression(p, node);
			return;
		case TS_SpreadElement:
			emit_spread_expression(p, node);
			return;
		case TS_OmittedExpression:
			return;
		case TS_AsExpression:
			emit_as_expression(p, node);
			return;
		case TS_NonNullExpression:
			emit_non_null_expression(p, node);
			return;
		default:
			break;
		}
	}
	unsupported(node);
}

/* ---- API ---- */

void ts_print_file(TsNode *source_file, Buf *out)
{
	Printer p;
	writer_reset(&p.w);
	/* writeFile: emitShebangIfNeeded (no text), emitPrologueDirectivesIfNeeded, print */
	emit_prologue_directives(&p, source_file->statements);
	pipeline_emit(&p, HINT_SourceFile, source_file);
	buf_add(out, p.w.out.p, p.w.out.n);
}

void ts_print_node(TsNode *node, Buf *out)
{
	Printer p;
	writer_reset(&p.w);
	pipeline_emit(&p, HINT_Unspecified, node);
	buf_add(out, p.w.out.p, p.w.out.n);
}
