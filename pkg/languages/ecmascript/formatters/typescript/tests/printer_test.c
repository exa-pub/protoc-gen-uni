/* Unit tests of the TypeScript 3.9.10 printer port: trees built the way @protobuf-ts/plugin
 * 2.11.1 builds them, with the expected text taken from its upstream output
 * (testdata/gen/community/timostamm-protobuf-ts/v2.11.1, dedented), parsed nodes decoded with
 * ts_decode_parsed, and the factory's parenthesization and escaping rules (expected values follow
 * factory.ts / utilities.ts of TypeScript 3.9.10). */
#include "typescript.c"

#include "test.h"


static char *print_node(TsNode *n)
{
	Buf b = {0};
	ts_print_node(n, &b);
	return (char *)buf_str(&b);
}

static char *print_statements(TsNodeArray *statements)
{
	Buf b = {0};
	ts_print_file(ts_create_source_file(statements), &b);
	return (char *)buf_str(&b);
}

static TsNode *id(const char *s)
{
	return ts_create_identifier(s);
}

static TsNode *prop(TsNode *e, const char *name)
{
	return ts_create_property_access(e, id(name));
}

static TsNode *str(const char *s)
{
	return ts_create_string_literal_c(s);
}

static TsNode *num(const char *s)
{
	return ts_create_numeric_literal(s);
}

static TsNode *ref(const char *name)
{
	return ts_create_type_reference_node(id(name), NULL);
}

static TsNode *bin(TsNode *l, TsSyntaxKind op, TsNode *r)
{
	return ts_create_binary(l, op, r);
}

static TsNode *call(TsNode *e, TsNodeArray *args)
{
	return ts_create_call(e, NULL, args ? args : ts_list_new());
}

static TsNode *stmt(TsNode *e)
{
	return ts_create_expression_statement(e);
}

static TsNode *let1(const char *name, TsNode *init, uint32_t flags)
{
	return ts_create_variable_statement(
		NULL, ts_create_variable_declaration_list(
			      ts_list(ts_create_variable_declaration(id(name), NULL, init)), flags));
}

static TsNode *param(const char *name, bool optional, TsNode *type)
{
	return ts_create_parameter(NULL, NULL, NULL, id(name),
				     optional ? ts_create_token(TS_QuestionToken) : NULL, type, NULL);
}

/* message-type-extensions/create.ts, for google.protobuf.Any. */
static void test_create_method(void)
{
	TsNode *message_variable = let1(
		"message",
		call(prop(prop(id("globalThis"), "Object"), "create"),
		     ts_list(ts_create_non_null_expression(prop(ts_create_this(), "messagePrototype")))),
		TS_NodeFlags_Const);
	TsNode *merge_if = ts_create_if(
		bin(id("value"), TS_ExclamationEqualsEqualsToken, id("undefined")),
		stmt(ts_create_call(id("reflectionMergePartial"), ts_list(ref("Any")),
				      ts_list(ts_create_this(), id("message"), id("value")))),
		NULL);
	TsNodeArray *body = ts_list(
		message_variable, stmt(bin(prop(id("message"), "typeUrl"), TS_EqualsToken, str(""))),
		stmt(bin(prop(id("message"), "value"), TS_EqualsToken,
			 ts_create_new(id("Uint8Array"), NULL, ts_list(num("0"))))),
		merge_if, ts_create_return(id("message")));
	TsNode *m = ts_create_method(
		NULL, NULL, NULL, id("create"), NULL, NULL,
		ts_list(param("value", true,
				ts_create_type_reference_node(id("PartialMessage"), ts_list(ref("Any"))))),
		ref("Any"), ts_create_block(body, true));
	CHECK_STR(print_node(m), "create(value?: PartialMessage<Any>): Any {\n"
				 "    const message = globalThis.Object.create((this.messagePrototype!));\n"
				 "    message.typeUrl = \"\";\n"
				 "    message.value = new Uint8Array(0);\n"
				 "    if (value !== undefined)\n"
				 "        reflectionMergePartial<Any>(this, message, value);\n"
				 "    return message;\n"
				 "}");
}

static TsNode *case_clause(const char *no, const char *comment, TsNode *statement)
{
	TsNode *n = num(no);
	ts_add_synthetic_leading_comment(n, TS_MultiLineCommentTrivia, str_c(comment), false);
	return ts_create_case_clause(n, ts_list(statement, ts_create_break(NULL)));
}

/* message-type-extensions/internal-binary-read.ts, for google.protobuf.Any. */
static void test_internal_binary_read(void)
{
	TsNode *variables = ts_create_variable_statement(
		NULL,
		ts_create_variable_declaration_list(
			ts_list(ts_create_variable_declaration(
					  id("message"), NULL,
					  bin(id("target"), TS_QuestionQuestionToken, call(prop(ts_create_this(), "create"), NULL))),
				  ts_create_variable_declaration(id("end"), NULL,
								   bin(prop(id("reader"), "pos"), TS_PlusToken, id("length")))),
			TS_NodeFlags_Let));
	TsNode *template = ts_create_template_expression(
		ts_create_template_literal_like_node(TS_TemplateHead, STR_LIT("Unknown field "), true),
		ts_list(ts_create_template_span(
				  id("fieldNo"), ts_create_template_literal_like_node(TS_TemplateMiddle, STR_LIT(" (wire type "), true)),
			  ts_create_template_span(
				  id("wireType"), ts_create_template_literal_like_node(TS_TemplateMiddle, STR_LIT(") for "), true)),
			  ts_create_template_span(prop(ts_create_this(), "typeName"),
						    ts_create_template_literal_like_node(TS_TemplateTail, STR_LIT(""), true))));
	TsNode *default_clause = ts_create_default_clause(ts_list(
		let1("u", prop(id("options"), "readUnknownField"), TS_NodeFlags_Let),
		ts_create_if(bin(id("u"), TS_EqualsEqualsEqualsToken, str("throw")),
			       ts_create_throw(ts_create_new(prop(id("globalThis"), "Error"), NULL, ts_list(template))),
			       NULL),
		let1("d", call(prop(id("reader"), "skip"), ts_list(id("wireType"))), TS_NodeFlags_Let),
		ts_create_if(
			bin(id("u"), TS_ExclamationEqualsEqualsToken, ts_create_false()),
			stmt(call(ts_create_paren(ts_create_conditional(
					  bin(id("u"), TS_EqualsEqualsEqualsToken, ts_create_true()),
					  prop(id("UnknownFieldHandler"), "onRead"), id("u"))),
				  ts_list(prop(ts_create_this(), "typeName"), id("message"), id("fieldNo"),
					    id("wireType"), id("d")))),
			NULL)));
	TsNode *tag = ts_create_variable_statement(
		NULL, ts_create_variable_declaration_list(
			      ts_list(ts_create_variable_declaration(
				      ts_create_array_binding_pattern(
					      ts_list(ts_create_binding_element(NULL, NULL, id("fieldNo"), NULL),
							ts_create_binding_element(NULL, NULL, id("wireType"), NULL))),
				      NULL, call(prop(id("reader"), "tag"), NULL))),
			      TS_NodeFlags_Let));
	TsNode *loop = ts_create_while(
		bin(prop(id("reader"), "pos"), TS_LessThanToken, id("end")),
		ts_create_block(
			ts_list(tag, ts_create_switch(
					       id("fieldNo"),
					       ts_create_case_block(ts_list(
						       case_clause("1", " string type_url ",
								   stmt(bin(prop(id("message"), "typeUrl"), TS_EqualsToken,
									    call(prop(id("reader"), "string"), NULL)))),
						       case_clause("2", " bytes value ",
								   stmt(bin(prop(id("message"), "value"), TS_EqualsToken,
									    call(prop(id("reader"), "bytes"), NULL)))),
						       default_clause)))),
			true));
	TsNode *m = ts_create_method(
		NULL, NULL, NULL, id("internalBinaryRead"), NULL, NULL,
		ts_list(param("reader", false, ref("IBinaryReader")),
			  param("length", false, ts_create_keyword_type_node(TS_NumberKeyword)),
			  param("options", false, ref("BinaryReadOptions")), param("target", true, ref("Any"))),
		ref("Any"), ts_create_block(ts_list(variables, loop, ts_create_return(id("message"))), true));
	CHECK_STR(print_node(m),
		  "internalBinaryRead(reader: IBinaryReader, length: number, options: BinaryReadOptions, target?: Any): Any {\n"
		  "    let message = target ?? this.create(), end = reader.pos + length;\n"
		  "    while (reader.pos < end) {\n"
		  "        let [fieldNo, wireType] = reader.tag();\n"
		  "        switch (fieldNo) {\n"
		  "            case /* string type_url */ 1:\n"
		  "                message.typeUrl = reader.string();\n"
		  "                break;\n"
		  "            case /* bytes value */ 2:\n"
		  "                message.value = reader.bytes();\n"
		  "                break;\n"
		  "            default:\n"
		  "                let u = options.readUnknownField;\n"
		  "                if (u === \"throw\")\n"
		  "                    throw new globalThis.Error(`Unknown field ${fieldNo} (wire type ${wireType}) for ${this.typeName}`);\n"
		  "                let d = reader.skip(wireType);\n"
		  "                if (u !== false)\n"
		  "                    (u === true ? UnknownFieldHandler.onRead : u)(this.typeName, message, fieldNo, wireType, d);\n"
		  "        }\n"
		  "    }\n"
		  "    return message;\n"
		  "}");
}

static TsNode *field_info(const char *no, const char *name, const char *t, const char *comment)
{
	TsNode *tn = num(t);
	ts_add_synthetic_trailing_comment(tn, TS_MultiLineCommentTrivia, str_c(comment), false);
	return ts_create_object_literal(
		ts_list(ts_create_property_assignment(id("no"), num(no)),
			  ts_create_property_assignment(id("name"), str(name)),
			  ts_create_property_assignment(id("kind"), str("scalar")),
			  ts_create_property_assignment(id("T"), tn)),
		false);
}

/* A class with a heritage clause, a constructor calling super with a multi-line array and
 * trailing comments, a JSDoc comment; a file with leading `//` comment lines. */
static void test_class(void)
{
	TsNode *ctor = ts_create_constructor(
		NULL, NULL, ts_list_new(),
		ts_create_block(
			ts_list(stmt(call(ts_create_super(),
					    ts_list(str("google.protobuf.Any"),
						      ts_create_array_literal(
							      ts_list(field_info("1", "type_url", "9", "ScalarType.STRING"),
									field_info("2", "value", "12", "ScalarType.BYTES")),
							      true))))),
			true));
	TsNode *cls = ts_create_class_declaration(
		NULL, NULL, id("Any$Type"), NULL,
		ts_list(ts_create_heritage_clause(
			TS_ExtendsKeyword,
			ts_list(ts_create_expression_with_type_arguments(ts_list(ref("Any")), id("MessageType"))))),
		ts_list(ctor));
	TsNode *export_const = ts_create_variable_statement(
		ts_list(ts_create_modifier(TS_ExportKeyword)),
		ts_create_variable_declaration_list(
			ts_list(ts_create_variable_declaration(id("Any"), NULL, ts_create_new(id("Any$Type"), NULL, ts_list_new()))),
			TS_NodeFlags_Const));
	ts_add_synthetic_leading_comment(export_const, TS_MultiLineCommentTrivia,
					   STR_LIT("*\n * @generated MessageType for protobuf message google.protobuf.Any\n "), true);
	TsNode *import = ts_create_import_declaration(
		NULL, NULL,
		ts_create_import_clause(NULL, ts_create_named_imports(ts_list(ts_create_import_specifier(NULL, id("MessageType")))),
					  false),
		str("@protobuf-ts/runtime"));
	ts_add_synthetic_leading_comment(import, TS_SingleLineCommentTrivia, STR_LIT(" @generated by protobuf-ts 2.11.1"), true);
	ts_add_synthetic_leading_comment(import, TS_SingleLineCommentTrivia, STR_LIT(" detached\n\n"), false);
	CHECK_STR(print_statements(ts_list(import, cls, export_const)),
		  "// @generated by protobuf-ts 2.11.1\n"
		  "// detached\n"
		  "\n"
		  "import { MessageType } from \"@protobuf-ts/runtime\";\n"
		  "class Any$Type extends MessageType<Any> {\n"
		  "    constructor() {\n"
		  "        super(\"google.protobuf.Any\", [\n"
		  "            { no: 1, name: \"type_url\", kind: \"scalar\", T: 9 /*ScalarType.STRING*/ },\n"
		  "            { no: 2, name: \"value\", kind: \"scalar\", T: 12 /*ScalarType.BYTES*/ }\n"
		  "        ]);\n"
		  "    }\n"
		  "}\n"
		  "/**\n"
		  " * @generated MessageType for protobuf message google.protobuf.Any\n"
		  " */\n"
		  "export const Any = new Any$Type();\n");
}

/* An interface with a JSDoc member, an index signature, a union of type literals. */
static void test_interface(void)
{
	TsNode *name = ts_create_property_signature(NULL, id("name"), NULL,
							ts_create_keyword_type_node(TS_StringKeyword), NULL);
	ts_add_synthetic_leading_comment(name, TS_MultiLineCommentTrivia,
					   STR_LIT("*\n * @generated from protobuf field: string name = 1\n "), true);
	ts_add_synthetic_trailing_comment(name, TS_SingleLineCommentTrivia, STR_LIT(" Trailing."), true);
	TsNode *map = ts_create_property_signature(
		NULL, id("children"), NULL,
		ts_create_type_literal_node(ts_list(ts_create_index_signature(
			NULL, NULL, ts_list(param("key", false, ts_create_keyword_type_node(TS_StringKeyword))),
			ref("Message")))),
		NULL);
	TsNode *choice = ts_create_property_signature(
		NULL, id("choice"), NULL,
		ts_create_union_type_node(ts_list(
			ts_create_type_literal_node(ts_list(
				ts_create_property_signature(NULL, id("oneofKind"), NULL,
							       ts_create_literal_type_node(str("a")), NULL),
				ts_create_property_signature(NULL, id("a"), NULL,
							       ts_create_keyword_type_node(TS_StringKeyword), NULL))),
			ts_create_type_literal_node(ts_list(ts_create_property_signature(
				NULL, id("oneofKind"), NULL, ts_create_keyword_type_node(TS_UndefinedKeyword), NULL))))),
		NULL);
	TsNode *iface = ts_create_interface_declaration(NULL, ts_list(ts_create_modifier(TS_ExportKeyword)),
							    id("Message"), NULL, NULL, ts_list(name, map, choice));
	CHECK_STR(print_node(iface), "export interface Message {\n"
				     "    /**\n"
				     "     * @generated from protobuf field: string name = 1\n"
				     "     */\n"
				     "    name: string; // Trailing.\n"
				     "    children: {\n"
				     "        [key: string]: Message;\n"
				     "    };\n"
				     "    choice: {\n"
				     "        oneofKind: \"a\";\n"
				     "        a: string;\n"
				     "    } | {\n"
				     "        oneofKind: undefined;\n"
				     "    };\n"
				     "}");
}

/* Enums and the empty forms of the lists. */
static void test_enum_and_empty(void)
{
	TsNode *e = ts_create_enum_declaration(
		NULL, ts_list(ts_create_modifier(TS_ExportKeyword)), id("Status"),
		ts_list(ts_create_enum_member(id("UNSPECIFIED"), num("0")),
			  ts_create_enum_member(id("NEGATIVE"), ts_create_prefix(TS_MinusToken, num("1")))));
	CHECK_STR(print_node(e), "export enum Status {\n    UNSPECIFIED = 0,\n    NEGATIVE = -1\n}");
	CHECK_STR(print_node(ts_create_class_declaration(NULL, NULL, id("A"), NULL, NULL, ts_list_new())),
		  "class A {\n}");
	CHECK_STR(print_node(ts_create_interface_declaration(NULL, NULL, id("I"), NULL, NULL, ts_list_new())),
		  "interface I {\n}");
	CHECK_STR(print_node(ts_create_block(ts_list_new(), false)), "{ }");
	CHECK_STR(print_node(ts_create_block(ts_list_new(), true)), "{\n}");
	CHECK_STR(print_node(ts_create_object_literal(ts_list_new(), false)), "{}");
	CHECK_STR(print_node(ts_create_object_literal(ts_list_new(), true)), "{}");
	CHECK_STR(print_node(ts_create_array_literal(ts_list_new(), true)), "[]");
	CHECK_STR(print_node(ts_create_type_literal_node(ts_list_new())), "{}");
	CHECK_STR(print_node(ts_create_new(id("A"), NULL, NULL)), "new A");
	CHECK_STR(print_node(call(id("f"), NULL)), "f()");
	/* a synthesized function body without multiLine stays on one line */
	CHECK_STR(print_node(ts_create_method(NULL, NULL, NULL, id("m"), NULL, NULL, ts_list_new(), NULL,
						ts_create_block(ts_list(ts_create_return(num("1"))), false))),
		  "m() { return 1; }");
	CHECK_STR(print_node(ts_create_method(NULL, NULL, NULL, id("m"), NULL, NULL, ts_list_new(), NULL,
						ts_create_block(ts_list_new(), false))),
		  "m() { }");
	CHECK_STR(print_node(ts_create_object_literal(
			  ts_list(ts_create_property_assignment(id("a"), num("1")),
				    ts_create_property_assignment(str("b-c"), num("2"))),
			  true)),
		  "{\n    a: 1,\n    \"b-c\": 2\n}");
	CHECK_STR(print_statements(ts_list_new()), "");
}

/* Parenthesization of factory.ts. */
static void test_parenthesize(void)
{
	TsNode *a = id("a"), *b = id("b"), *c = id("c");
	CHECK_STR(print_node(bin(a, TS_AsteriskToken, bin(b, TS_PlusToken, c))), "a * (b + c)");
	CHECK_STR(print_node(bin(bin(a, TS_PlusToken, b), TS_AsteriskToken, c)), "(a + b) * c");
	CHECK_STR(print_node(bin(a, TS_MinusToken, bin(b, TS_MinusToken, c))), "a - (b - c)");
	CHECK_STR(print_node(bin(a, TS_AsteriskToken, bin(b, TS_AsteriskToken, c))), "a * b * c");
	CHECK_STR(print_node(bin(str("x"), TS_PlusToken, bin(str("y"), TS_PlusToken, str("z")))), "\"x\" + \"y\" + \"z\"");
	CHECK_STR(print_node(bin(a, TS_PlusToken, bin(b, TS_PlusToken, c))), "a + (b + c)");
	CHECK_STR(print_node(bin(a, TS_EqualsToken, bin(b, TS_EqualsToken, c))), "a = b = c");
	CHECK_STR(print_node(bin(a, TS_AmpersandAmpersandToken,
				 ts_create_arrow_function(NULL, NULL, ts_list_new(), NULL, NULL, b))),
		  "a && (() => b)");
	CHECK_STR(print_node(prop(bin(a, TS_PlusToken, b), "x")), "(a + b).x");
	CHECK_STR(print_node(prop(ts_create_new(id("A"), NULL, NULL), "x")), "(new A).x");
	CHECK_STR(print_node(prop(ts_create_new(id("A"), NULL, ts_list_new()), "x")), "new A().x");
	CHECK_STR(print_node(ts_create_new(call(id("f"), NULL), NULL, ts_list_new())), "new (f())()");
	CHECK_STR(print_node(ts_create_prefix(TS_MinusToken, ts_create_prefix(TS_MinusToken, a))), "- -a");
	CHECK_STR(print_node(ts_create_prefix(TS_ExclamationToken, bin(a, TS_BarBarToken, b))), "!(a || b)");
	CHECK_STR(print_node(ts_create_postfix(bin(a, TS_PlusToken, b), TS_PlusPlusToken)), "(a + b)++");
	CHECK_STR(print_node(ts_create_type_of(a)), "typeof a");
	CHECK_STR(print_node(ts_create_conditional(bin(a, TS_EqualsToken, b), c, a)), "(a = b) ? c : a");
	CHECK_STR(print_node(ts_create_conditional(a, bin(b, TS_CommaToken, c), a)), "a ? (b, c) : a");
	CHECK_STR(print_node(call(id("f"), ts_list(ts_create_as_expression(a, ref("T")), bin(a, TS_CommaToken, b)))),
		  "f((a as T), (a, b))");
	CHECK_STR(print_node(prop(num("1"), "toString")), "1..toString");
	CHECK_STR(print_node(prop(num("1.5"), "toString")), "1.5.toString");
	CHECK_STR(print_node(stmt(call(ts_create_arrow_function(NULL, NULL, ts_list_new(), NULL, NULL,
								  ts_create_block(ts_list_new(), false)),
				       NULL))),
		  "(() => { })();");
	CHECK_STR(print_node(stmt(ts_create_object_literal(ts_list_new(), false))), "({});");
	CHECK_STR(print_node(ts_create_arrow_function(NULL, NULL, ts_list(param("x", false, NULL)), NULL, NULL,
							 ts_create_object_literal(ts_list_new(), false))),
		  "x => ({})");
	CHECK_STR(print_node(ts_create_arrow_function(NULL, NULL, ts_list(param("x", false, ref("T"))), NULL, NULL, a)),
		  "(x: T) => a");
	CHECK_STR(print_node(ts_create_array_type_node(ts_create_union_type_node(ts_list(ref("A"), ref("B"))))),
		  "(A | B)[]");
	CHECK_STR(print_node(ts_create_array_type_node(ts_create_type_operator_node(0, ref("A")))), "(keyof A)[]");
	CHECK_STR(print_node(ts_create_union_type_node(ts_list(
			  ts_create_function_type_node(NULL, ts_list_new(), ref("A")), ref("B")))),
		  "(() => A) | B");
	CHECK_STR(print_node(ts_create_indexed_access_type_node(
			  ts_create_union_type_node(ts_list(ref("A"), ref("B"))), ts_create_literal_type_node(str("k")))),
		  "(A | B)[\"k\"]");
	CHECK_STR(print_node(ts_create_element_access(ts_create_non_null_expression(a), num("0"))), "a![0]");
	CHECK_STR(print_node(ts_create_for_of(NULL,
						ts_create_variable_declaration_list(
							ts_list(ts_create_variable_declaration(id("x"), NULL, NULL)), TS_NodeFlags_Const),
						a, ts_create_block(ts_list_new(), true))),
		  "for (const x of a) {\n}");
	CHECK_STR(print_node(ts_create_for(let1("i", num("0"), TS_NodeFlags_Let)->declaration_list,
					     bin(id("i"), TS_LessThanToken, num("2")),
					     ts_create_postfix(id("i"), TS_PlusPlusToken), stmt(call(id("f"), NULL)))),
		  "for (let i = 0; i < 2; i++)\n    f();");
	CHECK_STR(print_node(ts_create_if(a, ts_create_block(ts_list(stmt(b)), true),
					    ts_create_if(b, stmt(c), stmt(a)))),
		  "if (a) {\n    b;\n}\nelse if (b)\n    c;\nelse\n    a;");
}

/* escapeString / escapeNonAsciiString (getReplacement, encodeUtf16EscapeSequence). */
static void test_escape(void)
{
	CHECK_STR(print_node(str("a\"b'c\\d")), "\"a\\\"b'c\\\\d\"");
	CHECK_STR(print_node(ts_create_string_literal(STR_LIT("\x00" "1\x00x"))), "\"\\x001\\0x\"");
	CHECK_STR(print_node(str("\t\v\f\b\r\n\x01\x1f\x7f")), "\"\\t\\v\\f\\b\\r\\n\\u0001\\u001F\x7f\"");
	CHECK_STR(print_node(str("caf\xc3\xa9 \xe2\x80\xa8\xe2\x80\xa9\xc2\x85")), "\"caf\\u00E9 \\u2028\\u2029\\u0085\"");
	CHECK_STR(print_node(str("\xf0\x9f\x98\x80")), "\"\\uD83D\\uDE00\"");
	TsNode *raw = str("caf\xc3\xa9\n");
	ts_set_emit_flags(raw, TS_EmitFlags_NoAsciiEscaping);
	CHECK_STR(print_node(raw), "\"caf\xc3\xa9\\n\"");
	TsNode *single = str("it's \"x\"");
	single->single_quote = true;
	CHECK_STR(print_node(single), "'it\\'s \"x\"'");
	CHECK_STR(print_node(ts_create_no_substitution_template_literal(STR_LIT("a`b\\c${d}\n\xc3\xa9"))),
		  "`a\\`b\\\\c\\${d}\n\\u00E9`");
	CHECK_STR(print_node(ts_create_template_expression(
			  ts_create_template_head(STR_LIT("x$")),
			  ts_list(ts_create_template_span(id("a"), ts_create_template_tail(STR_LIT("{")))))),
		  "`x$${a}{`");
	CHECK_STR(ts_escape_string(STR_LIT("\xe6\x97\xa5"), '"', true), "\\u65E5");
	CHECK_STR(ts_escape_string(STR_LIT("\xe6\x97\xa5"), '"', false), "\xe6\x97\xa5");
}

/* writeCommentRange: re-indentation of multi-line comments, trimming, empty lines. */
static void test_comment_range(void)
{
	TsNode *n = ts_create_property_signature(NULL, id("x"), NULL, NULL, NULL);
	ts_add_synthetic_leading_comment(n, TS_MultiLineCommentTrivia, STR_LIT("*\n *   a  \n\n\t* b\n "), true);
	TsNode *iface = ts_create_interface_declaration(NULL, NULL, id("I"), NULL, NULL, ts_list(n));
	CHECK_STR(print_node(iface), "interface I {\n"
				     "    /**\n"
				     "     *   a\n"
				     "    \n"
				     "        * b\n"
				     "     */\n"
				     "    x;\n"
				     "}");
	TsNode *e = id("e");
	ts_add_synthetic_leading_comment(e, TS_MultiLineCommentTrivia, STR_LIT(" first\nsecond "), false);
	CHECK_STR(print_node(stmt(e)), "/* first\nsecond */ e;");
	TsNode *t = stmt(id("t"));
	ts_add_synthetic_trailing_comment(t, TS_SingleLineCommentTrivia, STR_LIT(" trailing"), false);
	CHECK_STR(print_node(ts_create_block(ts_list(t, stmt(id("u"))), true)),
		  "{\n    t; // trailing\n    u;\n}");
	/* on an identifier, a trailing comment comes before the statement's semicolon */
	TsNode *v = id("v");
	ts_add_synthetic_trailing_comment(v, TS_MultiLineCommentTrivia, STR_LIT("c"), false);
	CHECK_STR(print_node(stmt(v)), "v /*c*/;");
}

/* ts_decode_parsed: parsed nodes print like the parser's (positions, multiLine, trailing commas),
 * back references and parameters. */
static Buf enc;

static void ev(uint32_t x)
{
	while (x >= 0x80) {
		buf_addc(&enc, (char)(x | 0x80));
		x >>= 7;
	}
	buf_addc(&enc, (char)x);
}

static void en(TsSyntaxKind k, int text, uint32_t props)
{
	ev((uint32_t)k << 2 | (text >= 0 ? 1 : 0) | (props ? 2 : 0));
	if (text >= 0)
		ev((uint32_t)text);
	if (props)
		ev(props);
}

static uint32_t node_slot(size_t off)
{
	for (int i = 0; i < N_NODE_SLOTS; i++)
		if (node_slots[i] == off)
			return (uint32_t)i + 1;
	return 0;
}

static uint32_t list_slot(size_t off)
{
	for (int i = 0; i < N_LIST_SLOTS; i++)
		if (list_slots[i] == off)
			return (uint32_t)(N_NODE_SLOTS + i + 1);
	return 0;
}

#define NODE_SLOT(f) node_slot(offsetof(TsNode, f))
#define LIST_SLOT(f) list_slot(offsetof(TsNode, f))

static const char *const dec_strings[] = {"t", "x", "a", "1", "$$0$$.b", "$$1$$"};

/* t => t.x, with or without the position of a simple arrow head */
static void enc_arrow(bool simple)
{
	en(TS_ArrowFunction, -1, simple ? 256 : 0);
	ev(NODE_SLOT(equals_greater_than_token));
	en(TS_EqualsGreaterThanToken, -1, 0);
	ev(0);
	ev(NODE_SLOT(body));
	en(TS_PropertyAccessExpression, -1, 0);
	ev(NODE_SLOT(name));
	en(TS_Identifier, 1, 0);
	ev(0);
	ev(NODE_SLOT(expression));
	en(TS_Identifier, 0, 0);
	ev(0);
	ev(0);
	ev(LIST_SLOT(parameters));
	ev(2);
	en(TS_Parameter, -1, 0);
	ev(NODE_SLOT(name));
	en(TS_Identifier, 0, 0);
	ev(0);
	ev(0);
	ev(0);
}

static char *decode_print(const char *const *params, int n)
{
	const uint8_t *p = (const uint8_t *)enc.p;
	TsNode *node = ts_decode_parsed(&p, dec_strings, params, n);
	CHECK(p == (const uint8_t *)enc.p + enc.n);
	return print_node(node);
}

static void test_decode(void)
{
	enc = (Buf){0};
	enc_arrow(true);
	CHECK_STR(decode_print(NULL, 0), "t => t.x");
	enc = (Buf){0};
	enc_arrow(false);
	CHECK_STR(decode_print(NULL, 0), "(t) => t.x");
	/* {\n a: 1, a: <back reference to "1">, \n} with a trailing comma: multiLine puts the properties
	 * on a new line, the parsed positions keep them on one line */
	enc = (Buf){0};
	en(TS_ObjectLiteralExpression, -1, 8);
	ev(LIST_SLOT(properties));
	ev(2 * 2 + 1);
	en(TS_PropertyAssignment, -1, 0);
	ev(NODE_SLOT(name));
	en(TS_Identifier, 2, 0);
	ev(0);
	ev(NODE_SLOT(initializer));
	size_t one = enc.n;
	en(TS_NumericLiteral, 3, 0);
	ev(0);
	ev(0);
	en(TS_PropertyAssignment, -1, 0);
	ev(NODE_SLOT(name));
	en(TS_Identifier, 2, 0);
	ev(0);
	ev(NODE_SLOT(initializer));
	size_t here = enc.n;
	ev(1);
	ev((uint32_t)(here - one));
	ev(0);
	ev(0);
	CHECK_STR(decode_print(NULL, 0), "{\n    a: 1, a: 1,\n}");
	/* parameters in texts */
	static const char *const params[] = {"Foo$", "bar"};
	enc = (Buf){0};
	en(TS_Identifier, 4, 0);
	ev(0);
	CHECK_STR(decode_print(params, 2), "Foo$.b");
	enc = (Buf){0};
	en(TS_StringLiteral, 5, 0);
	ev(0);
	CHECK_STR(decode_print(params, 2), "\"bar\"");
}

TESTS(TEST(test_create_method), TEST(test_internal_binary_read), TEST(test_class), TEST(test_interface),
      TEST(test_enum_and_empty), TEST(test_parenthesize), TEST(test_escape), TEST(test_comment_range),
      TEST(test_decode))
