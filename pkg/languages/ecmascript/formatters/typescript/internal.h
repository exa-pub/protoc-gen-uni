/* Internal header of the TypeScript 3.9.10 printer port (unity build, see typescript.c).
 *
 * Everything below is `static` and visible only inside the translation unit typescript.c; the public
 * API is typescript.h. Files mirror TypeScript's src/compiler (emitter.ts -> emitter.c, scanner.ts ->
 * scanner.c, ...) and keep the upstream function names in snake_case (emitNodeList ->
 * emit_node_list), in upstream order where practical.
 *
 * Strings: TypeScript works on UTF-16 code units, the port on UTF-8 bytes. Where the difference
 * is observable (escapes of non-ASCII characters, the line terminators U+2028/U+2029, Unicode
 * white space in comments) the code decodes UTF-8 explicitly. */
#ifndef TYPESCRIPT_INTERNAL_H
#define TYPESCRIPT_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "typescript.h"
#include "util.h"

#if defined(__GNUC__)
#define TS_UNUSED __attribute__((unused))
#else
#define TS_UNUSED
#endif

/* ts.ListFormat. */
enum {
	LF_None = 0,
	LF_SingleLine = 0,
	LF_MultiLine = 1 << 0,
	LF_PreserveLines = 1 << 1,
	LF_LinesMask = LF_SingleLine | LF_MultiLine | LF_PreserveLines,
	LF_NotDelimited = 0,
	LF_BarDelimited = 1 << 2,
	LF_AmpersandDelimited = 1 << 3,
	LF_CommaDelimited = 1 << 4,
	LF_AsteriskDelimited = 1 << 5,
	LF_DelimitersMask = LF_BarDelimited | LF_AmpersandDelimited | LF_CommaDelimited | LF_AsteriskDelimited,
	LF_AllowTrailingComma = 1 << 6,
	LF_Indented = 1 << 7,
	LF_SpaceBetweenBraces = 1 << 8,
	LF_SpaceBetweenSiblings = 1 << 9,
	LF_Braces = 1 << 10,
	LF_Parenthesis = 1 << 11,
	LF_AngleBrackets = 1 << 12,
	LF_SquareBrackets = 1 << 13,
	LF_BracketsMask = LF_Braces | LF_Parenthesis | LF_AngleBrackets | LF_SquareBrackets,
	LF_OptionalIfUndefined = 1 << 14,
	LF_OptionalIfEmpty = 1 << 15,
	LF_Optional = LF_OptionalIfUndefined | LF_OptionalIfEmpty,
	LF_PreferNewLine = 1 << 16,
	LF_NoTrailingNewLine = 1 << 17,
	LF_NoInterveningComments = 1 << 18,
	LF_NoSpaceIfEmpty = 1 << 19,
	LF_SingleElement = 1 << 20,
	LF_SpaceAfterList = 1 << 21,

	LF_Modifiers = LF_SingleLine | LF_SpaceBetweenSiblings | LF_NoInterveningComments,
	LF_HeritageClauses = LF_SingleLine | LF_SpaceBetweenSiblings,
	LF_SingleLineTypeLiteralMembers = LF_SingleLine | LF_SpaceBetweenBraces | LF_SpaceBetweenSiblings,
	LF_MultiLineTypeLiteralMembers = LF_MultiLine | LF_Indented | LF_OptionalIfEmpty,
	LF_TupleTypeElements = LF_CommaDelimited | LF_SpaceBetweenSiblings | LF_SingleLine,
	LF_UnionTypeConstituents = LF_BarDelimited | LF_SpaceBetweenSiblings | LF_SingleLine,
	LF_IntersectionTypeConstituents = LF_AmpersandDelimited | LF_SpaceBetweenSiblings | LF_SingleLine,
	LF_ObjectBindingPatternElements = LF_SingleLine | LF_AllowTrailingComma | LF_SpaceBetweenBraces |
					  LF_CommaDelimited | LF_SpaceBetweenSiblings | LF_NoSpaceIfEmpty,
	LF_ArrayBindingPatternElements =
		LF_SingleLine | LF_AllowTrailingComma | LF_CommaDelimited | LF_SpaceBetweenSiblings | LF_NoSpaceIfEmpty,
	LF_ObjectLiteralExpressionProperties = LF_PreserveLines | LF_CommaDelimited | LF_SpaceBetweenSiblings |
					       LF_SpaceBetweenBraces | LF_Indented | LF_Braces | LF_NoSpaceIfEmpty,
	LF_ArrayLiteralExpressionElements = LF_PreserveLines | LF_CommaDelimited | LF_SpaceBetweenSiblings |
					    LF_AllowTrailingComma | LF_Indented | LF_SquareBrackets,
	LF_CommaListElements = LF_CommaDelimited | LF_SpaceBetweenSiblings | LF_SingleLine,
	LF_CallExpressionArguments = LF_CommaDelimited | LF_SpaceBetweenSiblings | LF_SingleLine | LF_Parenthesis,
	LF_NewExpressionArguments =
		LF_CommaDelimited | LF_SpaceBetweenSiblings | LF_SingleLine | LF_Parenthesis | LF_OptionalIfUndefined,
	LF_TemplateExpressionSpans = LF_SingleLine | LF_NoInterveningComments,
	LF_SingleLineBlockStatements = LF_SpaceBetweenBraces | LF_SpaceBetweenSiblings | LF_SingleLine,
	LF_MultiLineBlockStatements = LF_Indented | LF_MultiLine,
	LF_VariableDeclarationList = LF_CommaDelimited | LF_SpaceBetweenSiblings | LF_SingleLine,
	LF_SingleLineFunctionBodyStatements = LF_SingleLine | LF_SpaceBetweenSiblings | LF_SpaceBetweenBraces,
	LF_MultiLineFunctionBodyStatements = LF_MultiLine,
	LF_ClassHeritageClauses = LF_SingleLine,
	LF_ClassMembers = LF_Indented | LF_MultiLine,
	LF_InterfaceMembers = LF_Indented | LF_MultiLine,
	LF_EnumMembers = LF_CommaDelimited | LF_Indented | LF_MultiLine,
	LF_CaseBlockClauses = LF_Indented | LF_MultiLine,
	LF_NamedImportsOrExportsElements = LF_CommaDelimited | LF_SpaceBetweenSiblings | LF_AllowTrailingComma |
					   LF_SingleLine | LF_SpaceBetweenBraces | LF_NoSpaceIfEmpty,
	LF_CaseOrDefaultClauseStatements = LF_Indented | LF_MultiLine | LF_NoTrailingNewLine | LF_OptionalIfEmpty,
	LF_HeritageClauseTypes = LF_CommaDelimited | LF_SpaceBetweenSiblings | LF_SingleLine,
	LF_SourceFileStatements = LF_MultiLine | LF_NoTrailingNewLine,
	LF_Decorators = LF_MultiLine | LF_Optional | LF_SpaceAfterList,
	LF_TypeArguments = LF_CommaDelimited | LF_SpaceBetweenSiblings | LF_SingleLine | LF_AngleBrackets | LF_Optional,
	LF_TypeParameters = LF_CommaDelimited | LF_SpaceBetweenSiblings | LF_SingleLine | LF_AngleBrackets | LF_Optional,
	LF_Parameters = LF_CommaDelimited | LF_SpaceBetweenSiblings | LF_SingleLine | LF_Parenthesis,
	LF_IndexSignatureParameters =
		LF_CommaDelimited | LF_SpaceBetweenSiblings | LF_SingleLine | LF_Indented | LF_SquareBrackets,
};

/* ts.Associativity. */
enum { ASSOC_Left, ASSOC_Right };

/* ---- core (node allocation; factoryPublic.ts createNode/createSynthesizedNode) ---- */
static TsNode *new_node(TsSyntaxKind kind, int pos, int end);
static TsNode *create_synthesized_node(TsSyntaxKind kind);
static TsNodeArray *create_node_array(TsNodeArray *elements);
static TsNodeArray *as_node_array(TsNodeArray *array);
static TsNodeArray *node_array_with_range(int pos);

/* ---- utilities.c ---- */

/* createTextWriter(newLine = "\n"). */
typedef struct {
	Buf out;
	int indent;
	bool line_start;
	bool has_trailing_comment;
} TextWriter;
static void writer_reset(TextWriter *w);
static void writer_write(TextWriter *w, const char *s, size_t n);
static void writer_write_comment(TextWriter *w, const char *s, size_t n);
static void writer_raw_write(TextWriter *w, const char *s, size_t n);
static void writer_write_line(TextWriter *w, bool force);
static bool writer_has_trailing_whitespace(const TextWriter *w);

static int utf8_rune(const char *p, size_t n, uint32_t *r);
static bool is_line_break(uint32_t ch);
static bool is_white_space_single_line(uint32_t ch);
static bool is_white_space_like(uint32_t ch);
static void escape_string_into(Buf *b, Str s, char quote, bool non_ascii);
static Str get_literal_text(const TsNode *node);
static void write_comment_range(TextWriter *w, Str text, size_t comment_pos, size_t comment_end);

static int get_operator(const TsNode *expression);
static int get_operator_precedence(TsSyntaxKind node_kind, int operator_kind, bool has_arguments);
static int get_expression_precedence(const TsNode *expression);
static int get_operator_associativity(TsSyntaxKind kind, int operator_kind, bool has_arguments);
static int get_expression_associativity(const TsNode *expression);
static int get_binary_operator_precedence(int kind);
static bool is_left_hand_side_expression_kind(TsSyntaxKind kind);
static bool is_unary_expression_kind(TsSyntaxKind kind);
static bool is_expression_kind(TsSyntaxKind kind);
static bool is_keyword(int kind);
static bool is_token_kind(int kind);
static bool is_literal_kind(int kind);
static bool node_is_synthesized(const TsNode *n);
static bool position_is_synthesized(int pos);
static bool is_prologue_directive(const TsNode *n);

/* ---- factory.c (parenthesization) ---- */
static TsNode *parenthesize_binary_operand(int binary_operator, TsNode *operand, bool is_left_side_of_binary,
					     TsNode *left_operand);
static TsNode *parenthesize_for_conditional_head(TsNode *condition);
static TsNode *parenthesize_subexpression_of_conditional_expression(TsNode *e);
static TsNode *parenthesize_for_new(TsNode *expression);
static TsNode *parenthesize_for_access(TsNode *expression);
static TsNode *parenthesize_postfix_operand(TsNode *operand);
static TsNode *parenthesize_prefix_operand(TsNode *operand);
static TsNodeArray *parenthesize_list_elements(TsNodeArray *elements);
static TsNode *parenthesize_expression_for_list(TsNode *expression);
static TsNode *parenthesize_expression_for_expression_statement(TsNode *expression);
static TsNode *parenthesize_element_type_member(TsNode *member);
static TsNode *parenthesize_array_type_member(TsNode *member);
static TsNodeArray *parenthesize_element_type_members(TsNodeArray *members);
static TsNodeArray *parenthesize_type_parameters(TsNodeArray *type_parameters);
static TsNode *parenthesize_concise_body(TsNode *body);
static bool is_comma_sequence(const TsNode *node);

#endif
