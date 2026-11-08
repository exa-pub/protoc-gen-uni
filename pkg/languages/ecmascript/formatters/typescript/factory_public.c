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

/* Port of TypeScript 3.9.10 src/compiler/factoryPublic.ts: the `ts.createXxx` node factory
 * (the functions @protobuf-ts/plugin 2.11.1 calls, plus the node kinds the parser builds) and
 * addSyntheticLeadingComment / addSyntheticTrailingComment. Factory arguments that TypeScript
 * accepts as `string | Node` take nodes here (asName / asExpression are the callers' job). */

#include <stdarg.h>

/* ---- nodes and node arrays ---- */

static TsNode *new_node(TsSyntaxKind kind, int pos, int end)
{
	TsNode *n = xalloc(sizeof *n);
	n->kind = kind;
	n->pos = pos;
	n->end = end;
	return n;
}

/* createSynthesizedNode. */
static TsNode *create_synthesized_node(TsSyntaxKind kind)
{
	TsNode *n = new_node(kind, -1, -1);
	n->flags |= TS_NodeFlags_Synthesized;
	return n;
}

static TsNodeArray *node_array_with_range(int pos)
{
	TsNodeArray *a = xalloc(sizeof *a);
	a->pos = pos;
	a->end = pos;
	return a;
}

TsNodeArray *ts_list_new(void)
{
	return node_array_with_range(-1);
}

void ts_list_push(TsNodeArray *a, TsNode *n)
{
	if (a->n == a->cap) {
		int cap = a->cap ? 2 * a->cap : 4;
		a->v = xrealloc(a->v, (size_t)a->cap * sizeof *a->v, (size_t)cap * sizeof *a->v);
		a->cap = cap;
	}
	a->v[a->n++] = n;
}

TsNodeArray *ts_list_(TsNode *first, ...)
{
	TsNodeArray *a = ts_list_new();
	va_list ap;
	va_start(ap, first);
	for (TsNode *n = first; n; n = va_arg(ap, TsNode *))
		ts_list_push(a, n);
	va_end(ap);
	return a;
}

/* createNodeArray(elements): undefined becomes an empty list, a list is returned as is. */
static TsNodeArray *create_node_array(TsNodeArray *elements)
{
	return elements ? elements : ts_list_new();
}

/* asNodeArray. */
static TsNodeArray *as_node_array(TsNodeArray *array)
{
	return array ? create_node_array(array) : NULL;
}

static Str str_dup_c(const char *s)
{
	return (Str){xstrdup(s), strlen(s)};
}

/* ---- literals, identifiers, tokens ---- */

TsNode *ts_create_numeric_literal(const char *value)
{
	TsNode *n = create_synthesized_node(TS_NumericLiteral);
	n->text = str_dup_c(value);
	return n;
}

TsNode *ts_create_string_literal(Str text)
{
	TsNode *n = create_synthesized_node(TS_StringLiteral);
	n->text = (Str){xstrndup(text.p, text.n), text.n};
	return n;
}

TsNode *ts_create_string_literal_c(const char *text)
{
	return ts_create_string_literal(str_c(text));
}

/* createIdentifier: escapeLeadingUnderscores / idText round-trip, so the text is kept as is. */
TsNode *ts_create_identifier(const char *text)
{
	TsNode *n = create_synthesized_node(TS_Identifier);
	n->text = str_dup_c(text);
	return n;
}

TsNode *ts_create_token(TsSyntaxKind kind)
{
	return create_synthesized_node(kind);
}

TsNode *ts_create_this(void)
{
	return create_synthesized_node(TS_ThisKeyword);
}

TsNode *ts_create_super(void)
{
	return create_synthesized_node(TS_SuperKeyword);
}

TsNode *ts_create_null(void)
{
	return create_synthesized_node(TS_NullKeyword);
}

TsNode *ts_create_true(void)
{
	return create_synthesized_node(TS_TrueKeyword);
}

TsNode *ts_create_false(void)
{
	return create_synthesized_node(TS_FalseKeyword);
}

/* ---- names ---- */

TsNode *ts_create_qualified_name(TsNode *left, TsNode *right)
{
	TsNode *n = create_synthesized_node(TS_QualifiedName);
	n->left = left;
	n->right = right;
	return n;
}

TsNode *ts_create_computed_property_name(TsNode *expression)
{
	TsNode *n = create_synthesized_node(TS_ComputedPropertyName);
	n->expression = is_comma_sequence(expression) ? ts_create_paren(expression) : expression;
	return n;
}

/* ---- signature elements ---- */

TsNode *ts_create_type_parameter_declaration(TsNode *name, TsNode *constraint, TsNode *default_type)
{
	TsNode *n = create_synthesized_node(TS_TypeParameter);
	n->name = name;
	n->constraint = constraint;
	n->default_ = default_type;
	return n;
}

TsNode *ts_create_parameter(TsNodeArray *decorators, TsNodeArray *modifiers, TsNode *dot_dot_dot_token,
				TsNode *name, TsNode *question_token, TsNode *type, TsNode *initializer)
{
	TsNode *n = create_synthesized_node(TS_Parameter);
	n->decorators = as_node_array(decorators);
	n->modifiers = as_node_array(modifiers);
	n->dot_dot_dot_token = dot_dot_dot_token;
	n->name = name;
	n->question_token = question_token;
	n->type = type;
	n->initializer = initializer ? parenthesize_expression_for_list(initializer) : NULL;
	return n;
}

/* ---- type members ---- */

TsNode *ts_create_property_signature(TsNodeArray *modifiers, TsNode *name, TsNode *question_token,
					 TsNode *type, TsNode *initializer)
{
	TsNode *n = create_synthesized_node(TS_PropertySignature);
	n->modifiers = as_node_array(modifiers);
	n->name = name;
	n->question_token = question_token;
	n->type = type;
	n->initializer = initializer;
	return n;
}

TsNode *ts_create_property(TsNodeArray *decorators, TsNodeArray *modifiers, TsNode *name,
			       TsNode *question_or_exclamation_token, TsNode *type, TsNode *initializer)
{
	TsNode *n = create_synthesized_node(TS_PropertyDeclaration);
	n->decorators = as_node_array(decorators);
	n->modifiers = as_node_array(modifiers);
	n->name = name;
	if (question_or_exclamation_token && question_or_exclamation_token->kind == TS_QuestionToken)
		n->question_token = question_or_exclamation_token;
	if (question_or_exclamation_token && question_or_exclamation_token->kind == TS_ExclamationToken)
		n->exclamation_token = question_or_exclamation_token;
	n->type = type;
	n->initializer = initializer;
	return n;
}

/* createSignatureDeclaration. */
static TsNode *create_signature_declaration(TsSyntaxKind kind, TsNodeArray *type_parameters,
					      TsNodeArray *parameters, TsNode *type)
{
	TsNode *n = create_synthesized_node(kind);
	n->type_parameters = as_node_array(type_parameters);
	n->parameters = as_node_array(parameters);
	n->type = type;
	return n;
}

TsNode *ts_create_method_signature(TsNodeArray *type_parameters, TsNodeArray *parameters, TsNode *type,
				       TsNode *name, TsNode *question_token)
{
	TsNode *n = create_signature_declaration(TS_MethodSignature, type_parameters, parameters, type);
	n->name = name;
	n->question_token = question_token;
	return n;
}

TsNode *ts_create_method(TsNodeArray *decorators, TsNodeArray *modifiers, TsNode *asterisk_token,
			     TsNode *name, TsNode *question_token, TsNodeArray *type_parameters,
			     TsNodeArray *parameters, TsNode *type, TsNode *body)
{
	TsNode *n = create_synthesized_node(TS_MethodDeclaration);
	n->decorators = as_node_array(decorators);
	n->modifiers = as_node_array(modifiers);
	n->asterisk_token = asterisk_token;
	n->name = name;
	n->question_token = question_token;
	n->type_parameters = as_node_array(type_parameters);
	n->parameters = create_node_array(parameters);
	n->type = type;
	n->body = body;
	return n;
}

TsNode *ts_create_constructor(TsNodeArray *decorators, TsNodeArray *modifiers, TsNodeArray *parameters,
				  TsNode *body)
{
	TsNode *n = create_synthesized_node(TS_Constructor);
	n->decorators = as_node_array(decorators);
	n->modifiers = as_node_array(modifiers);
	n->parameters = create_node_array(parameters);
	n->body = body;
	return n;
}

TsNode *ts_create_index_signature(TsNodeArray *decorators, TsNodeArray *modifiers,
				      TsNodeArray *parameters, TsNode *type)
{
	TsNode *n = create_synthesized_node(TS_IndexSignature);
	n->decorators = as_node_array(decorators);
	n->modifiers = as_node_array(modifiers);
	n->parameters = create_node_array(parameters);
	n->type = type;
	return n;
}

/* ---- types ---- */

TsNode *ts_create_type_reference_node(TsNode *type_name, TsNodeArray *type_arguments)
{
	TsNode *n = create_synthesized_node(TS_TypeReference);
	n->type_name = type_name;
	n->type_arguments = type_arguments ? parenthesize_type_parameters(type_arguments) : NULL;
	return n;
}

TsNode *ts_create_function_type_node(TsNodeArray *type_parameters, TsNodeArray *parameters, TsNode *type)
{
	return create_signature_declaration(TS_FunctionType, type_parameters, parameters, type);
}

TsNode *ts_create_type_literal_node(TsNodeArray *members)
{
	TsNode *n = create_synthesized_node(TS_TypeLiteral);
	n->members = create_node_array(members);
	return n;
}

TsNode *ts_create_array_type_node(TsNode *element_type)
{
	TsNode *n = create_synthesized_node(TS_ArrayType);
	n->element_type = parenthesize_array_type_member(element_type);
	return n;
}

static TsNode *create_union_or_intersection_type_node(TsSyntaxKind kind, TsNodeArray *types)
{
	TsNode *n = create_synthesized_node(kind);
	n->types = parenthesize_element_type_members(types);
	return n;
}

TsNode *ts_create_union_type_node(TsNodeArray *types)
{
	return create_union_or_intersection_type_node(TS_UnionType, types);
}

TsNode *ts_create_intersection_type_node(TsNodeArray *types)
{
	return create_union_or_intersection_type_node(TS_IntersectionType, types);
}

TsNode *ts_create_parenthesized_type(TsNode *type)
{
	TsNode *n = create_synthesized_node(TS_ParenthesizedType);
	n->type = type;
	return n;
}

TsNode *ts_create_type_operator_node(TsSyntaxKind operator_, TsNode *type)
{
	TsNode *n = create_synthesized_node(TS_TypeOperator);
	n->operator_ = operator_ ? operator_ : TS_KeyOfKeyword;
	n->type = parenthesize_element_type_member(type);
	return n;
}

TsNode *ts_create_indexed_access_type_node(TsNode *object_type, TsNode *index_type)
{
	TsNode *n = create_synthesized_node(TS_IndexedAccessType);
	n->object_type = parenthesize_element_type_member(object_type);
	n->index_type = index_type;
	return n;
}

TsNode *ts_create_literal_type_node(TsNode *literal)
{
	TsNode *n = create_synthesized_node(TS_LiteralType);
	n->literal = literal;
	return n;
}

/* ---- binding patterns ---- */

TsNode *ts_create_object_binding_pattern(TsNodeArray *elements)
{
	TsNode *n = create_synthesized_node(TS_ObjectBindingPattern);
	n->elements = create_node_array(elements);
	return n;
}

TsNode *ts_create_array_binding_pattern(TsNodeArray *elements)
{
	TsNode *n = create_synthesized_node(TS_ArrayBindingPattern);
	n->elements = create_node_array(elements);
	return n;
}

TsNode *ts_create_binding_element(TsNode *dot_dot_dot_token, TsNode *property_name, TsNode *name,
				      TsNode *initializer)
{
	TsNode *n = create_synthesized_node(TS_BindingElement);
	n->dot_dot_dot_token = dot_dot_dot_token;
	n->property_name = property_name;
	n->name = name;
	n->initializer = initializer;
	return n;
}

/* ---- expressions ---- */

TsNode *ts_create_array_literal(TsNodeArray *elements, bool multi_line)
{
	TsNode *n = create_synthesized_node(TS_ArrayLiteralExpression);
	n->elements = parenthesize_list_elements(create_node_array(elements));
	if (multi_line)
		n->multi_line = true;
	return n;
}

TsNode *ts_create_object_literal(TsNodeArray *properties, bool multi_line)
{
	TsNode *n = create_synthesized_node(TS_ObjectLiteralExpression);
	n->properties = create_node_array(properties);
	if (multi_line)
		n->multi_line = true;
	return n;
}

TsNode *ts_create_property_access(TsNode *expression, TsNode *name)
{
	TsNode *n = create_synthesized_node(TS_PropertyAccessExpression);
	n->expression = parenthesize_for_access(expression);
	n->name = name;
	n->emit_flags |= TS_EmitFlags_NoIndentation;
	return n;
}

TsNode *ts_create_element_access(TsNode *expression, TsNode *index)
{
	TsNode *n = create_synthesized_node(TS_ElementAccessExpression);
	n->expression = parenthesize_for_access(expression);
	n->argument_expression = index;
	return n;
}

TsNode *ts_create_call(TsNode *expression, TsNodeArray *type_arguments, TsNodeArray *arguments)
{
	TsNode *n = create_synthesized_node(TS_CallExpression);
	n->expression = parenthesize_for_access(expression);
	n->type_arguments = as_node_array(type_arguments);
	n->arguments = parenthesize_list_elements(create_node_array(arguments));
	return n;
}

TsNode *ts_create_new(TsNode *expression, TsNodeArray *type_arguments, TsNodeArray *arguments)
{
	TsNode *n = create_synthesized_node(TS_NewExpression);
	n->expression = parenthesize_for_new(expression);
	n->type_arguments = as_node_array(type_arguments);
	n->arguments = arguments ? parenthesize_list_elements(create_node_array(arguments)) : NULL;
	return n;
}

TsNode *ts_create_paren(TsNode *expression)
{
	TsNode *n = create_synthesized_node(TS_ParenthesizedExpression);
	n->expression = expression;
	return n;
}

TsNode *ts_create_arrow_function(TsNodeArray *modifiers, TsNodeArray *type_parameters,
				     TsNodeArray *parameters, TsNode *type, TsNode *equals_greater_than_token,
				     TsNode *body)
{
	TsNode *n = create_synthesized_node(TS_ArrowFunction);
	n->modifiers = as_node_array(modifiers);
	n->type_parameters = as_node_array(type_parameters);
	n->parameters = create_node_array(parameters);
	n->type = type;
	n->equals_greater_than_token =
		equals_greater_than_token ? equals_greater_than_token : ts_create_token(TS_EqualsGreaterThanToken);
	n->body = parenthesize_concise_body(body);
	return n;
}

TsNode *ts_create_delete(TsNode *expression)
{
	TsNode *n = create_synthesized_node(TS_DeleteExpression);
	n->expression = parenthesize_prefix_operand(expression);
	return n;
}

TsNode *ts_create_type_of(TsNode *expression)
{
	TsNode *n = create_synthesized_node(TS_TypeOfExpression);
	n->expression = parenthesize_prefix_operand(expression);
	return n;
}

TsNode *ts_create_prefix(TsSyntaxKind operator_, TsNode *operand)
{
	TsNode *n = create_synthesized_node(TS_PrefixUnaryExpression);
	n->operator_ = operator_;
	n->operand = parenthesize_prefix_operand(operand);
	return n;
}

TsNode *ts_create_postfix(TsNode *operand, TsSyntaxKind operator_)
{
	TsNode *n = create_synthesized_node(TS_PostfixUnaryExpression);
	n->operand = parenthesize_postfix_operand(operand);
	n->operator_ = operator_;
	return n;
}

TsNode *ts_create_binary_token(TsNode *left, TsNode *operator_token, TsNode *right)
{
	TsNode *n = create_synthesized_node(TS_BinaryExpression);
	int operator_kind = operator_token->kind;
	n->left = parenthesize_binary_operand(operator_kind, left, true, NULL);
	n->operator_token = operator_token;
	n->right = parenthesize_binary_operand(operator_kind, right, false, n->left);
	return n;
}

TsNode *ts_create_binary(TsNode *left, TsSyntaxKind operator_, TsNode *right)
{
	return ts_create_binary_token(left, ts_create_token(operator_), right);
}

TsNode *ts_create_conditional(TsNode *condition, TsNode *when_true, TsNode *when_false)
{
	TsNode *n = create_synthesized_node(TS_ConditionalExpression);
	n->condition = parenthesize_for_conditional_head(condition);
	n->question_token = ts_create_token(TS_QuestionToken);
	n->when_true = parenthesize_subexpression_of_conditional_expression(when_true);
	n->colon_token = ts_create_token(TS_ColonToken);
	n->when_false = parenthesize_subexpression_of_conditional_expression(when_false);
	return n;
}

TsNode *ts_create_template_expression(TsNode *head, TsNodeArray *template_spans)
{
	TsNode *n = create_synthesized_node(TS_TemplateExpression);
	n->head = head;
	n->template_spans = create_node_array(template_spans);
	return n;
}

/* createTemplateLiteralLikeNode: a rawText different from text would have to be checked against
 * its cooked value (getCookedText); protobuf-ts passes none or the same string. */
TsNode *ts_create_template_literal_like_node(TsSyntaxKind kind, Str text, bool raw_is_text)
{
	TsNode *n = create_synthesized_node(kind);
	n->text = (Str){xstrndup(text.p, text.n), text.n};
	if (raw_is_text) {
		n->raw_text = n->text;
		n->has_raw_text = true;
	}
	return n;
}

static TsNode *create_template_literal_like_node(TsSyntaxKind kind, Str text)
{
	return ts_create_template_literal_like_node(kind, text, false);
}

TsNode *ts_create_template_head(Str text)
{
	return create_template_literal_like_node(TS_TemplateHead, text);
}

TsNode *ts_create_template_middle(Str text)
{
	return create_template_literal_like_node(TS_TemplateMiddle, text);
}

TsNode *ts_create_template_tail(Str text)
{
	return create_template_literal_like_node(TS_TemplateTail, text);
}

TsNode *ts_create_no_substitution_template_literal(Str text)
{
	return create_template_literal_like_node(TS_NoSubstitutionTemplateLiteral, text);
}

TsNode *ts_create_spread(TsNode *expression)
{
	TsNode *n = create_synthesized_node(TS_SpreadElement);
	n->expression = parenthesize_expression_for_list(expression);
	return n;
}

TsNode *ts_create_omitted_expression(void)
{
	return create_synthesized_node(TS_OmittedExpression);
}

TsNode *ts_create_as_expression(TsNode *expression, TsNode *type)
{
	TsNode *n = create_synthesized_node(TS_AsExpression);
	n->expression = expression;
	n->type = type;
	return n;
}

TsNode *ts_create_non_null_expression(TsNode *expression)
{
	TsNode *n = create_synthesized_node(TS_NonNullExpression);
	n->expression = parenthesize_for_access(expression);
	return n;
}

TsNode *ts_create_expression_with_type_arguments(TsNodeArray *type_arguments, TsNode *expression)
{
	TsNode *n = create_synthesized_node(TS_ExpressionWithTypeArguments);
	n->expression = parenthesize_for_access(expression);
	n->type_arguments = as_node_array(type_arguments);
	return n;
}

/* ---- misc ---- */

TsNode *ts_create_template_span(TsNode *expression, TsNode *literal)
{
	TsNode *n = create_synthesized_node(TS_TemplateSpan);
	n->expression = expression;
	n->literal = literal;
	return n;
}

/* ---- statements ---- */

TsNode *ts_create_block(TsNodeArray *statements, bool multi_line)
{
	TsNode *n = create_synthesized_node(TS_Block);
	n->statements = create_node_array(statements);
	if (multi_line)
		n->multi_line = true;
	return n;
}

TsNode *ts_create_variable_statement(TsNodeArray *modifiers, TsNode *declaration_list)
{
	TsNode *n = create_synthesized_node(TS_VariableStatement);
	n->modifiers = as_node_array(modifiers);
	n->declaration_list = declaration_list;
	return n;
}

TsNode *ts_create_empty_statement(void)
{
	return create_synthesized_node(TS_EmptyStatement);
}

TsNode *ts_create_expression_statement(TsNode *expression)
{
	TsNode *n = create_synthesized_node(TS_ExpressionStatement);
	n->expression = parenthesize_expression_for_expression_statement(expression);
	return n;
}

/* asEmbeddedStatement is the identity: there are no NotEmittedStatements here. */
TsNode *ts_create_if(TsNode *expression, TsNode *then_statement, TsNode *else_statement)
{
	TsNode *n = create_synthesized_node(TS_IfStatement);
	n->expression = expression;
	n->then_statement = then_statement;
	n->else_statement = else_statement;
	return n;
}

TsNode *ts_create_while(TsNode *expression, TsNode *statement)
{
	TsNode *n = create_synthesized_node(TS_WhileStatement);
	n->expression = expression;
	n->statement = statement;
	return n;
}

TsNode *ts_create_for(TsNode *initializer, TsNode *condition, TsNode *incrementor, TsNode *statement)
{
	TsNode *n = create_synthesized_node(TS_ForStatement);
	n->initializer = initializer;
	n->condition = condition;
	n->incrementor = incrementor;
	n->statement = statement;
	return n;
}

TsNode *ts_create_for_of(TsNode *await_modifier, TsNode *initializer, TsNode *expression,
			     TsNode *statement)
{
	TsNode *n = create_synthesized_node(TS_ForOfStatement);
	n->await_modifier = await_modifier;
	n->initializer = initializer;
	n->expression = is_comma_sequence(expression) ? ts_create_paren(expression) : expression;
	n->statement = statement;
	return n;
}

TsNode *ts_create_break(TsNode *label)
{
	TsNode *n = create_synthesized_node(TS_BreakStatement);
	n->label = label;
	return n;
}

TsNode *ts_create_return(TsNode *expression)
{
	TsNode *n = create_synthesized_node(TS_ReturnStatement);
	n->expression = expression;
	return n;
}

TsNode *ts_create_switch(TsNode *expression, TsNode *case_block)
{
	TsNode *n = create_synthesized_node(TS_SwitchStatement);
	n->expression = parenthesize_expression_for_list(expression);
	n->case_block = case_block;
	return n;
}

TsNode *ts_create_throw(TsNode *expression)
{
	TsNode *n = create_synthesized_node(TS_ThrowStatement);
	n->expression = expression;
	return n;
}

TsNode *ts_create_variable_declaration_list(TsNodeArray *declarations, uint32_t flags)
{
	TsNode *n = create_synthesized_node(TS_VariableDeclarationList);
	n->flags |= flags & TS_NodeFlags_BlockScoped;
	n->declarations = create_node_array(declarations);
	return n;
}

TsNode *ts_create_variable_declaration(TsNode *name, TsNode *type, TsNode *initializer)
{
	TsNode *n = create_synthesized_node(TS_VariableDeclaration);
	n->name = name;
	n->type = type;
	n->initializer = initializer ? parenthesize_expression_for_list(initializer) : NULL;
	return n;
}

TsNode *ts_create_function_declaration(TsNodeArray *decorators, TsNodeArray *modifiers,
					   TsNode *asterisk_token, TsNode *name, TsNodeArray *type_parameters,
					   TsNodeArray *parameters, TsNode *type, TsNode *body)
{
	TsNode *n = create_synthesized_node(TS_FunctionDeclaration);
	n->decorators = as_node_array(decorators);
	n->modifiers = as_node_array(modifiers);
	n->asterisk_token = asterisk_token;
	n->name = name;
	n->type_parameters = as_node_array(type_parameters);
	n->parameters = create_node_array(parameters);
	n->type = type;
	n->body = body;
	return n;
}

TsNode *ts_create_class_declaration(TsNodeArray *decorators, TsNodeArray *modifiers, TsNode *name,
					TsNodeArray *type_parameters, TsNodeArray *heritage_clauses,
					TsNodeArray *members)
{
	TsNode *n = create_synthesized_node(TS_ClassDeclaration);
	n->decorators = as_node_array(decorators);
	n->modifiers = as_node_array(modifiers);
	n->name = name;
	n->type_parameters = as_node_array(type_parameters);
	n->heritage_clauses = as_node_array(heritage_clauses);
	n->members = create_node_array(members);
	return n;
}

TsNode *ts_create_interface_declaration(TsNodeArray *decorators, TsNodeArray *modifiers, TsNode *name,
					    TsNodeArray *type_parameters, TsNodeArray *heritage_clauses,
					    TsNodeArray *members)
{
	TsNode *n = create_synthesized_node(TS_InterfaceDeclaration);
	n->decorators = as_node_array(decorators);
	n->modifiers = as_node_array(modifiers);
	n->name = name;
	n->type_parameters = as_node_array(type_parameters);
	n->heritage_clauses = as_node_array(heritage_clauses);
	n->members = create_node_array(members);
	return n;
}

TsNode *ts_create_enum_declaration(TsNodeArray *decorators, TsNodeArray *modifiers, TsNode *name,
				       TsNodeArray *members)
{
	TsNode *n = create_synthesized_node(TS_EnumDeclaration);
	n->decorators = as_node_array(decorators);
	n->modifiers = as_node_array(modifiers);
	n->name = name;
	n->members = create_node_array(members);
	return n;
}

TsNode *ts_create_case_block(TsNodeArray *clauses)
{
	TsNode *n = create_synthesized_node(TS_CaseBlock);
	n->clauses = create_node_array(clauses);
	return n;
}

TsNode *ts_create_import_declaration(TsNodeArray *decorators, TsNodeArray *modifiers,
					 TsNode *import_clause, TsNode *module_specifier)
{
	TsNode *n = create_synthesized_node(TS_ImportDeclaration);
	n->decorators = as_node_array(decorators);
	n->modifiers = as_node_array(modifiers);
	n->import_clause = import_clause;
	n->module_specifier = module_specifier;
	return n;
}

TsNode *ts_create_import_clause(TsNode *name, TsNode *named_bindings, bool is_type_only)
{
	TsNode *n = create_synthesized_node(TS_ImportClause);
	n->name = name;
	n->named_bindings = named_bindings;
	n->is_type_only = is_type_only;
	return n;
}

TsNode *ts_create_namespace_import(TsNode *name)
{
	TsNode *n = create_synthesized_node(TS_NamespaceImport);
	n->name = name;
	return n;
}

TsNode *ts_create_named_imports(TsNodeArray *elements)
{
	TsNode *n = create_synthesized_node(TS_NamedImports);
	n->elements = create_node_array(elements);
	return n;
}

TsNode *ts_create_import_specifier(TsNode *property_name, TsNode *name)
{
	TsNode *n = create_synthesized_node(TS_ImportSpecifier);
	n->property_name = property_name;
	n->name = name;
	return n;
}

/* ---- clauses ---- */

TsNode *ts_create_case_clause(TsNode *expression, TsNodeArray *statements)
{
	TsNode *n = create_synthesized_node(TS_CaseClause);
	n->expression = parenthesize_expression_for_list(expression);
	n->statements = create_node_array(statements);
	return n;
}

TsNode *ts_create_default_clause(TsNodeArray *statements)
{
	TsNode *n = create_synthesized_node(TS_DefaultClause);
	n->statements = create_node_array(statements);
	return n;
}

TsNode *ts_create_heritage_clause(TsSyntaxKind token, TsNodeArray *types)
{
	TsNode *n = create_synthesized_node(TS_HeritageClause);
	n->token = token;
	n->types = create_node_array(types);
	return n;
}

/* ---- property assignments ---- */

TsNode *ts_create_property_assignment(TsNode *name, TsNode *initializer)
{
	TsNode *n = create_synthesized_node(TS_PropertyAssignment);
	n->name = name;
	n->initializer = parenthesize_expression_for_list(initializer);
	return n;
}

TsNode *ts_create_shorthand_property_assignment(TsNode *name, TsNode *object_assignment_initializer)
{
	TsNode *n = create_synthesized_node(TS_ShorthandPropertyAssignment);
	n->name = name;
	n->initializer =
		object_assignment_initializer ? parenthesize_expression_for_list(object_assignment_initializer) : NULL;
	return n;
}

TsNode *ts_create_spread_assignment(TsNode *expression)
{
	TsNode *n = create_synthesized_node(TS_SpreadAssignment);
	n->expression = expression ? parenthesize_expression_for_list(expression) : NULL;
	return n;
}

TsNode *ts_create_enum_member(TsNode *name, TsNode *initializer)
{
	TsNode *n = create_synthesized_node(TS_EnumMember);
	n->name = name;
	n->initializer = initializer ? parenthesize_expression_for_list(initializer) : NULL;
	return n;
}

/* ---- top-level ---- */

/* updateSourceFileNode(createSourceFile(name, "", ScriptTarget.Latest), statements): a
 * synthesized SourceFile whose text is "" (the printer never reads anything else of it). */
TsNode *ts_create_source_file(TsNodeArray *statements)
{
	TsNode *n = create_synthesized_node(TS_SourceFile);
	n->statements = create_node_array(statements);
	return n;
}

/* ---- synthetic comments, emit flags ---- */

static void add_comment(TsComment **v, int *n, TsSyntaxKind kind, Str text, bool has_trailing_new_line)
{
	*v = xrealloc(*v, (size_t)*n * sizeof **v, (size_t)(*n + 1) * sizeof **v);
	(*v)[*n] = (TsComment){kind, {xstrndup(text.p, text.n), text.n}, has_trailing_new_line};
	(*n)++;
}

void ts_add_synthetic_leading_comment(TsNode *node, TsSyntaxKind kind, Str text, bool has_trailing_new_line)
{
	add_comment(&node->leading_comments, &node->n_leading_comments, kind, text, has_trailing_new_line);
}

void ts_add_synthetic_trailing_comment(TsNode *node, TsSyntaxKind kind, Str text, bool has_trailing_new_line)
{
	add_comment(&node->trailing_comments, &node->n_trailing_comments, kind, text, has_trailing_new_line);
}

void ts_set_emit_flags(TsNode *node, uint32_t emit_flags)
{
	node->emit_flags = emit_flags;
}
