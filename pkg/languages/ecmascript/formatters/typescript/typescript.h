/* TypeScript 3.9.10 printer subset, C port: the AST factory (`ts.createXxx` of factoryPublic.ts
 * with the parenthesization rules of factory.ts), synthetic comments and `ts.createPrinter(
 * {newLine: LineFeed}).printFile` (emitter.ts), plus parsed nodes decoded from a compact
 * encoding (decode.c) for code that upstream parses from constant text.
 *
 * Upstream: https://github.com/microsoft/TypeScript/tree/v3.9.10/src/compiler
 * (Apache License 2.0, Copyright (c) Microsoft Corporation).
 *
 * Model: nodes come either from the factory (synthesized, pos == end == -1) or from
 * ts_decode_parsed (parsed: real positions). Like protobuf-ts, a file is always printed through a
 * source file whose text is empty (`ts.createSourceFile(name, "")` + `ts.updateSourceFileNode`),
 * so all positions of parsed nodes count as one line; see README.md.
 *
 * Everything is allocated from the arena (pkg/core/util.h) and never freed. Strings are UTF-8;
 * JavaScript semantics on UTF-16 code units are reproduced where they matter (escaping, line
 * terminators). */
#ifndef TYPESCRIPT_H
#define TYPESCRIPT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "util.h"

/* ts.SyntaxKind of TypeScript 3.9.10 (same values). */
typedef enum {
	TS_Unknown = 0,
	TS_EndOfFileToken = 1,
	TS_SingleLineCommentTrivia = 2,
	TS_MultiLineCommentTrivia = 3,
	TS_NewLineTrivia = 4,
	TS_WhitespaceTrivia = 5,
	TS_ShebangTrivia = 6,
	TS_ConflictMarkerTrivia = 7,
	TS_NumericLiteral = 8,
	TS_BigIntLiteral = 9,
	TS_StringLiteral = 10,
	TS_JsxText = 11,
	TS_JsxTextAllWhiteSpaces = 12,
	TS_RegularExpressionLiteral = 13,
	TS_NoSubstitutionTemplateLiteral = 14,
	TS_TemplateHead = 15,
	TS_TemplateMiddle = 16,
	TS_TemplateTail = 17,
	TS_OpenBraceToken = 18,
	TS_CloseBraceToken = 19,
	TS_OpenParenToken = 20,
	TS_CloseParenToken = 21,
	TS_OpenBracketToken = 22,
	TS_CloseBracketToken = 23,
	TS_DotToken = 24,
	TS_DotDotDotToken = 25,
	TS_SemicolonToken = 26,
	TS_CommaToken = 27,
	TS_QuestionDotToken = 28,
	TS_LessThanToken = 29,
	TS_LessThanSlashToken = 30,
	TS_GreaterThanToken = 31,
	TS_LessThanEqualsToken = 32,
	TS_GreaterThanEqualsToken = 33,
	TS_EqualsEqualsToken = 34,
	TS_ExclamationEqualsToken = 35,
	TS_EqualsEqualsEqualsToken = 36,
	TS_ExclamationEqualsEqualsToken = 37,
	TS_EqualsGreaterThanToken = 38,
	TS_PlusToken = 39,
	TS_MinusToken = 40,
	TS_AsteriskToken = 41,
	TS_AsteriskAsteriskToken = 42,
	TS_SlashToken = 43,
	TS_PercentToken = 44,
	TS_PlusPlusToken = 45,
	TS_MinusMinusToken = 46,
	TS_LessThanLessThanToken = 47,
	TS_GreaterThanGreaterThanToken = 48,
	TS_GreaterThanGreaterThanGreaterThanToken = 49,
	TS_AmpersandToken = 50,
	TS_BarToken = 51,
	TS_CaretToken = 52,
	TS_ExclamationToken = 53,
	TS_TildeToken = 54,
	TS_AmpersandAmpersandToken = 55,
	TS_BarBarToken = 56,
	TS_QuestionToken = 57,
	TS_ColonToken = 58,
	TS_AtToken = 59,
	TS_QuestionQuestionToken = 60,
	TS_BacktickToken = 61,
	TS_EqualsToken = 62,
	TS_PlusEqualsToken = 63,
	TS_MinusEqualsToken = 64,
	TS_AsteriskEqualsToken = 65,
	TS_AsteriskAsteriskEqualsToken = 66,
	TS_SlashEqualsToken = 67,
	TS_PercentEqualsToken = 68,
	TS_LessThanLessThanEqualsToken = 69,
	TS_GreaterThanGreaterThanEqualsToken = 70,
	TS_GreaterThanGreaterThanGreaterThanEqualsToken = 71,
	TS_AmpersandEqualsToken = 72,
	TS_BarEqualsToken = 73,
	TS_CaretEqualsToken = 74,
	TS_Identifier = 75,
	TS_PrivateIdentifier = 76,
	TS_BreakKeyword = 77,
	TS_CaseKeyword = 78,
	TS_CatchKeyword = 79,
	TS_ClassKeyword = 80,
	TS_ConstKeyword = 81,
	TS_ContinueKeyword = 82,
	TS_DebuggerKeyword = 83,
	TS_DefaultKeyword = 84,
	TS_DeleteKeyword = 85,
	TS_DoKeyword = 86,
	TS_ElseKeyword = 87,
	TS_EnumKeyword = 88,
	TS_ExportKeyword = 89,
	TS_ExtendsKeyword = 90,
	TS_FalseKeyword = 91,
	TS_FinallyKeyword = 92,
	TS_ForKeyword = 93,
	TS_FunctionKeyword = 94,
	TS_IfKeyword = 95,
	TS_ImportKeyword = 96,
	TS_InKeyword = 97,
	TS_InstanceOfKeyword = 98,
	TS_NewKeyword = 99,
	TS_NullKeyword = 100,
	TS_ReturnKeyword = 101,
	TS_SuperKeyword = 102,
	TS_SwitchKeyword = 103,
	TS_ThisKeyword = 104,
	TS_ThrowKeyword = 105,
	TS_TrueKeyword = 106,
	TS_TryKeyword = 107,
	TS_TypeOfKeyword = 108,
	TS_VarKeyword = 109,
	TS_VoidKeyword = 110,
	TS_WhileKeyword = 111,
	TS_WithKeyword = 112,
	TS_ImplementsKeyword = 113,
	TS_InterfaceKeyword = 114,
	TS_LetKeyword = 115,
	TS_PackageKeyword = 116,
	TS_PrivateKeyword = 117,
	TS_ProtectedKeyword = 118,
	TS_PublicKeyword = 119,
	TS_StaticKeyword = 120,
	TS_YieldKeyword = 121,
	TS_AbstractKeyword = 122,
	TS_AsKeyword = 123,
	TS_AssertsKeyword = 124,
	TS_AnyKeyword = 125,
	TS_AsyncKeyword = 126,
	TS_AwaitKeyword = 127,
	TS_BooleanKeyword = 128,
	TS_ConstructorKeyword = 129,
	TS_DeclareKeyword = 130,
	TS_GetKeyword = 131,
	TS_InferKeyword = 132,
	TS_IsKeyword = 133,
	TS_KeyOfKeyword = 134,
	TS_ModuleKeyword = 135,
	TS_NamespaceKeyword = 136,
	TS_NeverKeyword = 137,
	TS_ReadonlyKeyword = 138,
	TS_RequireKeyword = 139,
	TS_NumberKeyword = 140,
	TS_ObjectKeyword = 141,
	TS_SetKeyword = 142,
	TS_StringKeyword = 143,
	TS_SymbolKeyword = 144,
	TS_TypeKeyword = 145,
	TS_UndefinedKeyword = 146,
	TS_UniqueKeyword = 147,
	TS_UnknownKeyword = 148,
	TS_FromKeyword = 149,
	TS_GlobalKeyword = 150,
	TS_BigIntKeyword = 151,
	TS_OfKeyword = 152,
	TS_QualifiedName = 153,
	TS_ComputedPropertyName = 154,
	TS_TypeParameter = 155,
	TS_Parameter = 156,
	TS_Decorator = 157,
	TS_PropertySignature = 158,
	TS_PropertyDeclaration = 159,
	TS_MethodSignature = 160,
	TS_MethodDeclaration = 161,
	TS_Constructor = 162,
	TS_GetAccessor = 163,
	TS_SetAccessor = 164,
	TS_CallSignature = 165,
	TS_ConstructSignature = 166,
	TS_IndexSignature = 167,
	TS_TypePredicate = 168,
	TS_TypeReference = 169,
	TS_FunctionType = 170,
	TS_ConstructorType = 171,
	TS_TypeQuery = 172,
	TS_TypeLiteral = 173,
	TS_ArrayType = 174,
	TS_TupleType = 175,
	TS_OptionalType = 176,
	TS_RestType = 177,
	TS_UnionType = 178,
	TS_IntersectionType = 179,
	TS_ConditionalType = 180,
	TS_InferType = 181,
	TS_ParenthesizedType = 182,
	TS_ThisType = 183,
	TS_TypeOperator = 184,
	TS_IndexedAccessType = 185,
	TS_MappedType = 186,
	TS_LiteralType = 187,
	TS_ImportType = 188,
	TS_ObjectBindingPattern = 189,
	TS_ArrayBindingPattern = 190,
	TS_BindingElement = 191,
	TS_ArrayLiteralExpression = 192,
	TS_ObjectLiteralExpression = 193,
	TS_PropertyAccessExpression = 194,
	TS_ElementAccessExpression = 195,
	TS_CallExpression = 196,
	TS_NewExpression = 197,
	TS_TaggedTemplateExpression = 198,
	TS_TypeAssertionExpression = 199,
	TS_ParenthesizedExpression = 200,
	TS_FunctionExpression = 201,
	TS_ArrowFunction = 202,
	TS_DeleteExpression = 203,
	TS_TypeOfExpression = 204,
	TS_VoidExpression = 205,
	TS_AwaitExpression = 206,
	TS_PrefixUnaryExpression = 207,
	TS_PostfixUnaryExpression = 208,
	TS_BinaryExpression = 209,
	TS_ConditionalExpression = 210,
	TS_TemplateExpression = 211,
	TS_YieldExpression = 212,
	TS_SpreadElement = 213,
	TS_ClassExpression = 214,
	TS_OmittedExpression = 215,
	TS_ExpressionWithTypeArguments = 216,
	TS_AsExpression = 217,
	TS_NonNullExpression = 218,
	TS_MetaProperty = 219,
	TS_SyntheticExpression = 220,
	TS_TemplateSpan = 221,
	TS_SemicolonClassElement = 222,
	TS_Block = 223,
	TS_EmptyStatement = 224,
	TS_VariableStatement = 225,
	TS_ExpressionStatement = 226,
	TS_IfStatement = 227,
	TS_DoStatement = 228,
	TS_WhileStatement = 229,
	TS_ForStatement = 230,
	TS_ForInStatement = 231,
	TS_ForOfStatement = 232,
	TS_ContinueStatement = 233,
	TS_BreakStatement = 234,
	TS_ReturnStatement = 235,
	TS_WithStatement = 236,
	TS_SwitchStatement = 237,
	TS_LabeledStatement = 238,
	TS_ThrowStatement = 239,
	TS_TryStatement = 240,
	TS_DebuggerStatement = 241,
	TS_VariableDeclaration = 242,
	TS_VariableDeclarationList = 243,
	TS_FunctionDeclaration = 244,
	TS_ClassDeclaration = 245,
	TS_InterfaceDeclaration = 246,
	TS_TypeAliasDeclaration = 247,
	TS_EnumDeclaration = 248,
	TS_ModuleDeclaration = 249,
	TS_ModuleBlock = 250,
	TS_CaseBlock = 251,
	TS_NamespaceExportDeclaration = 252,
	TS_ImportEqualsDeclaration = 253,
	TS_ImportDeclaration = 254,
	TS_ImportClause = 255,
	TS_NamespaceImport = 256,
	TS_NamedImports = 257,
	TS_ImportSpecifier = 258,
	TS_ExportAssignment = 259,
	TS_ExportDeclaration = 260,
	TS_NamedExports = 261,
	TS_NamespaceExport = 262,
	TS_ExportSpecifier = 263,
	TS_MissingDeclaration = 264,
	TS_ExternalModuleReference = 265,
	TS_JsxElement = 266,
	TS_JsxSelfClosingElement = 267,
	TS_JsxOpeningElement = 268,
	TS_JsxClosingElement = 269,
	TS_JsxFragment = 270,
	TS_JsxOpeningFragment = 271,
	TS_JsxClosingFragment = 272,
	TS_JsxAttribute = 273,
	TS_JsxAttributes = 274,
	TS_JsxSpreadAttribute = 275,
	TS_JsxExpression = 276,
	TS_CaseClause = 277,
	TS_DefaultClause = 278,
	TS_HeritageClause = 279,
	TS_CatchClause = 280,
	TS_PropertyAssignment = 281,
	TS_ShorthandPropertyAssignment = 282,
	TS_SpreadAssignment = 283,
	TS_EnumMember = 284,
	TS_UnparsedPrologue = 285,
	TS_UnparsedPrepend = 286,
	TS_UnparsedText = 287,
	TS_UnparsedInternalText = 288,
	TS_UnparsedSyntheticReference = 289,
	TS_SourceFile = 290,
	TS_Bundle = 291,
	TS_UnparsedSource = 292,
	TS_InputFiles = 293,
	TS_JSDocTypeExpression = 294,
	TS_JSDocAllType = 295,
	TS_JSDocUnknownType = 296,
	TS_JSDocNullableType = 297,
	TS_JSDocNonNullableType = 298,
	TS_JSDocOptionalType = 299,
	TS_JSDocFunctionType = 300,
	TS_JSDocVariadicType = 301,
	TS_JSDocNamepathType = 302,
	TS_JSDocComment = 303,
	TS_JSDocTypeLiteral = 304,
	TS_JSDocSignature = 305,
	TS_JSDocTag = 306,
	TS_JSDocAugmentsTag = 307,
	TS_JSDocImplementsTag = 308,
	TS_JSDocAuthorTag = 309,
	TS_JSDocClassTag = 310,
	TS_JSDocPublicTag = 311,
	TS_JSDocPrivateTag = 312,
	TS_JSDocProtectedTag = 313,
	TS_JSDocReadonlyTag = 314,
	TS_JSDocCallbackTag = 315,
	TS_JSDocEnumTag = 316,
	TS_JSDocParameterTag = 317,
	TS_JSDocReturnTag = 318,
	TS_JSDocThisTag = 319,
	TS_JSDocTypeTag = 320,
	TS_JSDocTemplateTag = 321,
	TS_JSDocTypedefTag = 322,
	TS_JSDocPropertyTag = 323,
	TS_SyntaxList = 324,
	TS_NotEmittedStatement = 325,
	TS_PartiallyEmittedExpression = 326,
	TS_CommaListExpression = 327,
	TS_MergeDeclarationMarker = 328,
	TS_EndOfDeclarationMarker = 329,
	TS_SyntheticReferenceExpression = 330,
	TS_Count = 331,
	TS_FirstAssignment = 62,
	TS_LastAssignment = 74,
	TS_FirstCompoundAssignment = 63,
	TS_LastCompoundAssignment = 74,
	TS_FirstReservedWord = 77,
	TS_LastReservedWord = 112,
	TS_FirstKeyword = 77,
	TS_LastKeyword = 152,
	TS_FirstFutureReservedWord = 113,
	TS_LastFutureReservedWord = 121,
	TS_FirstTypeNode = 168,
	TS_LastTypeNode = 188,
	TS_FirstPunctuation = 18,
	TS_LastPunctuation = 74,
	TS_FirstToken = 0,
	TS_LastToken = 152,
	TS_FirstTriviaToken = 2,
	TS_LastTriviaToken = 7,
	TS_FirstLiteralToken = 8,
	TS_LastLiteralToken = 14,
	TS_FirstTemplateToken = 14,
	TS_LastTemplateToken = 17,
	TS_FirstBinaryOperator = 29,
	TS_LastBinaryOperator = 74,
	TS_FirstStatement = 225,
	TS_LastStatement = 241,
	TS_FirstNode = 153,
	TS_FirstJSDocNode = 294,
	TS_LastJSDocNode = 323,
	TS_FirstJSDocTagNode = 306,
	TS_LastJSDocTagNode = 323,
} TsSyntaxKind;

/* ts.NodeFlags (the subset the printer and the parser use). */
enum {
	TS_NodeFlags_None = 0,
	TS_NodeFlags_Let = 1 << 0,
	TS_NodeFlags_Const = 1 << 1,
	TS_NodeFlags_NestedNamespace = 1 << 2,
	TS_NodeFlags_Synthesized = 1 << 3,
	TS_NodeFlags_Namespace = 1 << 4,
	TS_NodeFlags_OptionalChain = 1 << 5,
	TS_NodeFlags_GlobalAugmentation = 1 << 10,
	TS_NodeFlags_BlockScoped = TS_NodeFlags_Let | TS_NodeFlags_Const,
};

/* ts.EmitFlags (the subset the printer reads). */
enum {
	TS_EmitFlags_SingleLine = 1 << 0,
	TS_EmitFlags_NoLeadingComments = 1 << 9,
	TS_EmitFlags_NoTrailingComments = 1 << 10,
	TS_EmitFlags_NoNestedComments = 1 << 11,
	TS_EmitFlags_Indented = 1 << 16,
	TS_EmitFlags_NoIndentation = 1 << 17,
	TS_EmitFlags_NoAsciiEscaping = 1 << 24,
};

/* ts.TokenFlags of numeric literals. */
enum {
	TS_TokenFlags_Scientific = 1 << 4,
	TS_TokenFlags_Octal = 1 << 5,
	TS_TokenFlags_HexSpecifier = 1 << 6,
	TS_TokenFlags_BinarySpecifier = 1 << 7,
	TS_TokenFlags_OctalSpecifier = 1 << 8,
	TS_TokenFlags_ContainsSeparator = 1 << 9,
};

typedef struct TsNode TsNode;

/* ts.NodeArray: a list with its own text range and trailing-comma flag. A NULL TsNodeArray *
 * is `undefined`, which the printer distinguishes from an empty list. */
typedef struct {
	TsNode **v;
	int n, cap;
	int pos, end;
	bool has_trailing_comma;
} TsNodeArray;

/* ts.SynthesizedComment (kind is TS_SingleLineCommentTrivia or TS_MultiLineCommentTrivia). */
typedef struct {
	TsSyntaxKind kind;
	Str text;
	bool has_trailing_new_line;
} TsComment;

/* One struct for all node kinds; the fields are the TypeScript property names, each kind uses
 * those of its TypeScript interface (unused fields stay NULL / 0). */
struct TsNode {
	TsSyntaxKind kind;
	int pos, end;
	uint32_t flags;      /* NodeFlags */
	uint32_t emit_flags; /* EmitFlags */

	/* Synthetic comments (emitNode.leadingComments / trailingComments). */
	TsComment *leading_comments, *trailing_comments;
	int n_leading_comments, n_trailing_comments;

	/* Identifier (idText), literals (cooked text), regular expressions (source text). */
	Str text;
	Str raw_text;      /* template literal rawText; valid if has_raw_text */
	bool has_raw_text;
	bool single_quote; /* StringLiteral.singleQuote */
	bool multi_line;   /* Block, ObjectLiteralExpression, ArrayLiteralExpression */
	bool is_type_only; /* ImportClause */
	int numeric_literal_flags;
	TsSyntaxKind operator_; /* Prefix/PostfixUnaryExpression.operator, TypeOperatorNode.operator */
	TsSyntaxKind token;     /* HeritageClause.token */

	TsNode *name, *property_name, *expression, *type, *initializer, *body;
	TsNode *left, *right, *operator_token; /* QualifiedName, BinaryExpression */
	TsNode *question_token, *colon_token, *exclamation_token, *dot_dot_dot_token, *asterisk_token;
	TsNode *question_dot_token, *equals_greater_than_token, *await_modifier;
	TsNode *condition, *when_true, *when_false;
	TsNode *then_statement, *else_statement, *statement, *incrementor;
	TsNode *argument_expression, *operand, *head, *literal, *label;
	TsNode *constraint, *default_; /* TypeParameterDeclaration */
	TsNode *element_type, *object_type, *index_type, *type_name;
	TsNode *import_clause, *module_specifier, *named_bindings, *case_block, *declaration_list;

	TsNodeArray *decorators, *modifiers, *type_parameters, *parameters, *type_arguments, *arguments;
	TsNodeArray *members, *statements, *elements, *properties, *declarations, *types;
	TsNodeArray *heritage_clauses, *clauses, *template_spans;
};

/* ---- node arrays ---- */

/* createNodeArray(elements): a new synthesized list of the given nodes (at least one);
 * ts_list_new() is the empty list, distinct from `undefined` == NULL. */
TsNodeArray *ts_list_(TsNode *first, ...);
#define ts_list(...) ts_list_(__VA_ARGS__, (TsNode *)0)
TsNodeArray *ts_list_new(void);
void ts_list_push(TsNodeArray *a, TsNode *n);

/* ---- factory (factoryPublic.ts) ---- */

TsNode *ts_create_numeric_literal(const char *value);
TsNode *ts_create_string_literal(Str text);
TsNode *ts_create_string_literal_c(const char *text);
TsNode *ts_create_identifier(const char *text);
TsNode *ts_create_token(TsSyntaxKind kind);
#define ts_create_modifier(kind) ts_create_token(kind)
#define ts_create_keyword_type_node(kind) ts_create_token(kind)
TsNode *ts_create_this(void);
TsNode *ts_create_super(void);
TsNode *ts_create_null(void);
TsNode *ts_create_true(void);
TsNode *ts_create_false(void);
TsNode *ts_create_qualified_name(TsNode *left, TsNode *right);
TsNode *ts_create_type_parameter_declaration(TsNode *name, TsNode *constraint, TsNode *default_type);
TsNode *ts_create_parameter(TsNodeArray *decorators, TsNodeArray *modifiers, TsNode *dot_dot_dot_token,
				TsNode *name, TsNode *question_token, TsNode *type, TsNode *initializer);
TsNode *ts_create_property_signature(TsNodeArray *modifiers, TsNode *name, TsNode *question_token,
					 TsNode *type, TsNode *initializer);
TsNode *ts_create_property(TsNodeArray *decorators, TsNodeArray *modifiers, TsNode *name,
			       TsNode *question_or_exclamation_token, TsNode *type, TsNode *initializer);
TsNode *ts_create_method_signature(TsNodeArray *type_parameters, TsNodeArray *parameters, TsNode *type,
				       TsNode *name, TsNode *question_token);
TsNode *ts_create_method(TsNodeArray *decorators, TsNodeArray *modifiers, TsNode *asterisk_token,
			     TsNode *name, TsNode *question_token, TsNodeArray *type_parameters,
			     TsNodeArray *parameters, TsNode *type, TsNode *body);
TsNode *ts_create_constructor(TsNodeArray *decorators, TsNodeArray *modifiers, TsNodeArray *parameters,
				  TsNode *body);
TsNode *ts_create_index_signature(TsNodeArray *decorators, TsNodeArray *modifiers,
				      TsNodeArray *parameters, TsNode *type);
TsNode *ts_create_type_reference_node(TsNode *type_name, TsNodeArray *type_arguments);
TsNode *ts_create_function_type_node(TsNodeArray *type_parameters, TsNodeArray *parameters, TsNode *type);
TsNode *ts_create_type_literal_node(TsNodeArray *members);
TsNode *ts_create_array_type_node(TsNode *element_type);
TsNode *ts_create_union_type_node(TsNodeArray *types);
TsNode *ts_create_intersection_type_node(TsNodeArray *types);
TsNode *ts_create_parenthesized_type(TsNode *type);
/* createTypeOperatorNode(operator, type); operator 0 means the one-argument form (keyof). */
TsNode *ts_create_type_operator_node(TsSyntaxKind operator_, TsNode *type);
TsNode *ts_create_indexed_access_type_node(TsNode *object_type, TsNode *index_type);
TsNode *ts_create_literal_type_node(TsNode *literal);
TsNode *ts_create_array_binding_pattern(TsNodeArray *elements);
TsNode *ts_create_object_binding_pattern(TsNodeArray *elements);
TsNode *ts_create_binding_element(TsNode *dot_dot_dot_token, TsNode *property_name, TsNode *name,
				      TsNode *initializer);
TsNode *ts_create_array_literal(TsNodeArray *elements, bool multi_line);
TsNode *ts_create_object_literal(TsNodeArray *properties, bool multi_line);
TsNode *ts_create_property_access(TsNode *expression, TsNode *name);
TsNode *ts_create_element_access(TsNode *expression, TsNode *index);
TsNode *ts_create_call(TsNode *expression, TsNodeArray *type_arguments, TsNodeArray *arguments);
TsNode *ts_create_new(TsNode *expression, TsNodeArray *type_arguments, TsNodeArray *arguments);
TsNode *ts_create_paren(TsNode *expression);
TsNode *ts_create_arrow_function(TsNodeArray *modifiers, TsNodeArray *type_parameters,
				     TsNodeArray *parameters, TsNode *type, TsNode *equals_greater_than_token,
				     TsNode *body);
TsNode *ts_create_delete(TsNode *expression);
TsNode *ts_create_type_of(TsNode *expression);
TsNode *ts_create_prefix(TsSyntaxKind operator_, TsNode *operand);
TsNode *ts_create_postfix(TsNode *operand, TsSyntaxKind operator_);
/* createBinary(left, operator, right): operator is a token kind. */
TsNode *ts_create_binary(TsNode *left, TsSyntaxKind operator_, TsNode *right);
TsNode *ts_create_binary_token(TsNode *left, TsNode *operator_token, TsNode *right);
TsNode *ts_create_conditional(TsNode *condition, TsNode *when_true, TsNode *when_false);
TsNode *ts_create_template_expression(TsNode *head, TsNodeArray *template_spans);
/* createTemplateHead(text) etc.; createTemplateLiteralLikeNode(kind, text, rawText) with a raw text
 * equal to text (protobuf-ts passes the same string twice), which is then printed unescaped. */
TsNode *ts_create_template_literal_like_node(TsSyntaxKind kind, Str text, bool raw_is_text);
TsNode *ts_create_template_head(Str text);
TsNode *ts_create_template_middle(Str text);
TsNode *ts_create_template_tail(Str text);
TsNode *ts_create_no_substitution_template_literal(Str text);
TsNode *ts_create_spread(TsNode *expression);
TsNode *ts_create_omitted_expression(void);
TsNode *ts_create_as_expression(TsNode *expression, TsNode *type);
TsNode *ts_create_non_null_expression(TsNode *expression);
TsNode *ts_create_expression_with_type_arguments(TsNodeArray *type_arguments, TsNode *expression);
TsNode *ts_create_template_span(TsNode *expression, TsNode *literal);
TsNode *ts_create_block(TsNodeArray *statements, bool multi_line);
/* createVariableStatement(modifiers, declarationList) with a VariableDeclarationList node. */
TsNode *ts_create_variable_statement(TsNodeArray *modifiers, TsNode *declaration_list);
TsNode *ts_create_empty_statement(void);
TsNode *ts_create_expression_statement(TsNode *expression);
TsNode *ts_create_if(TsNode *expression, TsNode *then_statement, TsNode *else_statement);
TsNode *ts_create_while(TsNode *expression, TsNode *statement);
TsNode *ts_create_for(TsNode *initializer, TsNode *condition, TsNode *incrementor, TsNode *statement);
TsNode *ts_create_for_of(TsNode *await_modifier, TsNode *initializer, TsNode *expression,
			     TsNode *statement);
TsNode *ts_create_break(TsNode *label);
TsNode *ts_create_return(TsNode *expression);
TsNode *ts_create_switch(TsNode *expression, TsNode *case_block);
TsNode *ts_create_throw(TsNode *expression);
TsNode *ts_create_variable_declaration_list(TsNodeArray *declarations, uint32_t flags);
TsNode *ts_create_variable_declaration(TsNode *name, TsNode *type, TsNode *initializer);
TsNode *ts_create_function_declaration(TsNodeArray *decorators, TsNodeArray *modifiers,
					   TsNode *asterisk_token, TsNode *name, TsNodeArray *type_parameters,
					   TsNodeArray *parameters, TsNode *type, TsNode *body);
TsNode *ts_create_class_declaration(TsNodeArray *decorators, TsNodeArray *modifiers, TsNode *name,
					TsNodeArray *type_parameters, TsNodeArray *heritage_clauses,
					TsNodeArray *members);
TsNode *ts_create_interface_declaration(TsNodeArray *decorators, TsNodeArray *modifiers, TsNode *name,
					    TsNodeArray *type_parameters, TsNodeArray *heritage_clauses,
					    TsNodeArray *members);
TsNode *ts_create_enum_declaration(TsNodeArray *decorators, TsNodeArray *modifiers, TsNode *name,
				       TsNodeArray *members);
TsNode *ts_create_case_block(TsNodeArray *clauses);
TsNode *ts_create_import_declaration(TsNodeArray *decorators, TsNodeArray *modifiers,
					 TsNode *import_clause, TsNode *module_specifier);
TsNode *ts_create_import_clause(TsNode *name, TsNode *named_bindings, bool is_type_only);
TsNode *ts_create_namespace_import(TsNode *name);
TsNode *ts_create_named_imports(TsNodeArray *elements);
TsNode *ts_create_import_specifier(TsNode *property_name, TsNode *name);
TsNode *ts_create_case_clause(TsNode *expression, TsNodeArray *statements);
TsNode *ts_create_default_clause(TsNodeArray *statements);
TsNode *ts_create_heritage_clause(TsSyntaxKind token, TsNodeArray *types);
TsNode *ts_create_property_assignment(TsNode *name, TsNode *initializer);
TsNode *ts_create_shorthand_property_assignment(TsNode *name, TsNode *object_assignment_initializer);
TsNode *ts_create_spread_assignment(TsNode *expression);
TsNode *ts_create_enum_member(TsNode *name, TsNode *initializer);
TsNode *ts_create_computed_property_name(TsNode *expression);

/* ts.createSourceFile(fileName, "", Latest) followed by updateSourceFileNode(sf, statements):
 * a source file with empty text (the only kind protobuf-ts prints). */
TsNode *ts_create_source_file(TsNodeArray *statements);

/* addSyntheticLeadingComment / addSyntheticTrailingComment. */
void ts_add_synthetic_leading_comment(TsNode *node, TsSyntaxKind kind, Str text, bool has_trailing_new_line);
void ts_add_synthetic_trailing_comment(TsNode *node, TsSyntaxKind kind, Str text, bool has_trailing_new_line);
void ts_set_emit_flags(TsNode *node, uint32_t emit_flags);

/* ---- printer (emitter.ts) ---- */

/* ts.createPrinter({newLine: NewLineKind.LineFeed}).printFile(sourceFile), appended to *out. */
void ts_print_file(TsNode *source_file, Buf *out);
/* printer.printNode(EmitHint.Unspecified, node, sourceFile) with an empty source file. */
void ts_print_node(TsNode *node, Buf *out);

/* ---- parsed nodes (decode.c) ---- */

/* Decodes one node tree as ts.createSourceFile parses it (real positions, the flags the parser
 * sets), from the compact encoding described in decode.c, and advances *code past it. Each
 * "$$<n>$$" in a text of the string table is replaced with params[n]. This is how code that
 * upstream parses from constant text at run time builds its nodes. */
TsNode *ts_decode_parsed(const uint8_t **code, const char *const *strings, const char *const *params, int n_params);

/* ---- helpers ---- */

/* tokenToString(kind) or NULL. */
const char *ts_token_to_string(TsSyntaxKind kind);
/* escapeString / escapeNonAsciiString of utilities.ts (quote: '"', '\'' or '`'). */
char *ts_escape_string(Str s, char quote, bool non_ascii);

#endif
