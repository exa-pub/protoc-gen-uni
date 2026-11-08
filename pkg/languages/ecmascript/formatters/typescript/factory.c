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

/* Port of TypeScript 3.9.10 src/compiler/factory.ts: the parenthesization rules the factory
 * applies to its operands. skipPartiallyEmittedExpressions is the identity (there are no
 * PartiallyEmittedExpressions outside transformations). setTextRange(createParen(e), e) keeps
 * the range of e; createParen of a parsed node therefore yields a parsed-looking node, as in
 * TypeScript. */

static TsNode *create_paren_with_range(TsNode *expression)
{
	TsNode *p = ts_create_paren(expression);
	p->pos = expression->pos;
	p->end = expression->end;
	return p;
}

static bool is_comma_sequence(const TsNode *node)
{
	return (node->kind == TS_BinaryExpression && node->operator_token->kind == TS_CommaToken) ||
	       node->kind == TS_CommaListExpression;
}

static bool operator_has_associative_property(int binary_operator)
{
	return binary_operator == TS_AsteriskToken || binary_operator == TS_BarToken ||
	       binary_operator == TS_AmpersandToken || binary_operator == TS_CaretToken;
}

/* getLiteralKindOfBinaryPlusOperand (without the cachedLiteralKind cache). */
static int get_literal_kind_of_binary_plus_operand(const TsNode *node)
{
	if (is_literal_kind(node->kind))
		return node->kind;
	if (node->kind == TS_BinaryExpression && node->operator_token->kind == TS_PlusToken) {
		int left_kind = get_literal_kind_of_binary_plus_operand(node->left);
		return is_literal_kind(left_kind) && left_kind == get_literal_kind_of_binary_plus_operand(node->right)
			       ? left_kind
			       : TS_Unknown;
	}
	return TS_Unknown;
}

static bool binary_operand_needs_parentheses(int binary_operator, const TsNode *operand,
					     bool is_left_side_of_binary, const TsNode *left_operand)
{
	int binary_operator_precedence = get_operator_precedence(TS_BinaryExpression, binary_operator, false);
	int binary_operator_associativity = get_operator_associativity(TS_BinaryExpression, binary_operator, false);
	const TsNode *emitted_operand = operand;
	if (!is_left_side_of_binary && operand->kind == TS_ArrowFunction && binary_operator_precedence > 3)
		return true;
	int operand_precedence = get_expression_precedence(emitted_operand);
	if (operand_precedence < binary_operator_precedence) {
		if (!is_left_side_of_binary && binary_operator_associativity == ASSOC_Right &&
		    operand->kind == TS_YieldExpression)
			return false;
		return true;
	}
	if (operand_precedence > binary_operator_precedence)
		return false;
	if (is_left_side_of_binary)
		return binary_operator_associativity == ASSOC_Right;
	if (emitted_operand->kind == TS_BinaryExpression &&
	    (int)emitted_operand->operator_token->kind == binary_operator) {
		if (operator_has_associative_property(binary_operator))
			return false;
		if (binary_operator == TS_PlusToken) {
			int left_kind = left_operand ? get_literal_kind_of_binary_plus_operand(left_operand) : TS_Unknown;
			if (is_literal_kind(left_kind) && left_kind == get_literal_kind_of_binary_plus_operand(emitted_operand))
				return false;
		}
	}
	return get_expression_associativity(emitted_operand) == ASSOC_Left;
}

static TsNode *parenthesize_binary_operand(int binary_operator, TsNode *operand, bool is_left_side_of_binary,
					     TsNode *left_operand)
{
	if (operand->kind == TS_ParenthesizedExpression)
		return operand;
	return binary_operand_needs_parentheses(binary_operator, operand, is_left_side_of_binary, left_operand)
		       ? ts_create_paren(operand)
		       : operand;
}

static TsNode *parenthesize_for_conditional_head(TsNode *condition)
{
	int conditional_precedence = get_operator_precedence(TS_ConditionalExpression, TS_QuestionToken, false);
	int condition_precedence = get_expression_precedence(condition);
	if (!(condition_precedence > conditional_precedence))
		return ts_create_paren(condition);
	return condition;
}

static TsNode *parenthesize_subexpression_of_conditional_expression(TsNode *e)
{
	return is_comma_sequence(e) ? ts_create_paren(e) : e;
}

/* getLeftmostExpression. */
static TsNode *get_leftmost_expression(TsNode *node, bool stop_at_call_expressions)
{
	for (;;) {
		switch (node->kind) {
		case TS_PostfixUnaryExpression:
			node = node->operand;
			continue;
		case TS_BinaryExpression:
			node = node->left;
			continue;
		case TS_ConditionalExpression:
			node = node->condition;
			continue;
		case TS_TaggedTemplateExpression:
			node = node->expression; /* .tag */
			continue;
		case TS_CallExpression:
			if (stop_at_call_expressions)
				return node;
			/* fallthrough */
		case TS_AsExpression:
		case TS_ElementAccessExpression:
		case TS_PropertyAccessExpression:
		case TS_NonNullExpression:
		case TS_PartiallyEmittedExpression:
			node = node->expression;
			continue;
		default:
			return node;
		}
	}
}

static TsNode *parenthesize_for_new(TsNode *expression)
{
	TsNode *leftmost = get_leftmost_expression(expression, true);
	switch (leftmost->kind) {
	case TS_CallExpression:
		return ts_create_paren(expression);
	case TS_NewExpression:
		return !leftmost->arguments ? ts_create_paren(expression) : expression;
	default:
		return parenthesize_for_access(expression);
	}
}

static TsNode *parenthesize_for_access(TsNode *expression)
{
	if (is_left_hand_side_expression_kind(expression->kind) &&
	    (expression->kind != TS_NewExpression || expression->arguments))
		return expression;
	return create_paren_with_range(expression);
}

static TsNode *parenthesize_postfix_operand(TsNode *operand)
{
	return is_left_hand_side_expression_kind(operand->kind) ? operand : create_paren_with_range(operand);
}

static TsNode *parenthesize_prefix_operand(TsNode *operand)
{
	return is_unary_expression_kind(operand->kind) ? operand : create_paren_with_range(operand);
}

static TsNodeArray *parenthesize_list_elements(TsNodeArray *elements)
{
	TsNodeArray *result = NULL;
	for (int i = 0; i < elements->n; i++) {
		TsNode *element = parenthesize_expression_for_list(elements->v[i]);
		if (result || element != elements->v[i]) {
			if (!result) {
				result = node_array_with_range(elements->pos);
				result->end = elements->end;
				result->has_trailing_comma = elements->has_trailing_comma;
				for (int j = 0; j < i; j++)
					ts_list_push(result, elements->v[j]);
			}
			ts_list_push(result, element);
		}
	}
	return result ? result : elements;
}

static TsNode *parenthesize_expression_for_list(TsNode *expression)
{
	int expression_precedence = get_expression_precedence(expression);
	int comma_precedence = get_operator_precedence(TS_BinaryExpression, TS_CommaToken, false);
	return expression_precedence > comma_precedence ? expression : create_paren_with_range(expression);
}

static TsNode *parenthesize_expression_for_expression_statement(TsNode *expression)
{
	if (expression->kind == TS_CallExpression) {
		TsNode *callee = expression->expression;
		if (callee->kind == TS_FunctionExpression || callee->kind == TS_ArrowFunction) {
			/* getMutableClone(call) with a parenthesized callee */
			TsNode *call = xalloc(sizeof *call);
			*call = *expression;
			call->expression = create_paren_with_range(callee);
			return call;
		}
	}
	int leftmost_kind = get_leftmost_expression(expression, false)->kind;
	if (leftmost_kind == TS_ObjectLiteralExpression || leftmost_kind == TS_FunctionExpression)
		return create_paren_with_range(expression);
	return expression;
}

static TsNode *parenthesize_conditional_type_member(TsNode *member)
{
	return member->kind == TS_ConditionalType ? ts_create_parenthesized_type(member) : member;
}

static TsNode *parenthesize_element_type_member(TsNode *member)
{
	switch (member->kind) {
	case TS_UnionType:
	case TS_IntersectionType:
	case TS_FunctionType:
	case TS_ConstructorType:
		return ts_create_parenthesized_type(member);
	default:
		return parenthesize_conditional_type_member(member);
	}
}

static TsNode *parenthesize_array_type_member(TsNode *member)
{
	switch (member->kind) {
	case TS_TypeQuery:
	case TS_TypeOperator:
	case TS_InferType:
		return ts_create_parenthesized_type(member);
	default:
		return parenthesize_element_type_member(member);
	}
}

/* createNodeArray(sameMap(members, parenthesizeElementTypeMember)): a new list unless members
 * already is one (sameMap returns the input array when nothing changed). */
static TsNodeArray *parenthesize_element_type_members(TsNodeArray *members)
{
	TsNodeArray *result = NULL;
	for (int i = 0; i < members->n; i++) {
		TsNode *m = parenthesize_element_type_member(members->v[i]);
		if (result || m != members->v[i]) {
			if (!result) {
				result = ts_list_new();
				for (int j = 0; j < i; j++)
					ts_list_push(result, members->v[j]);
			}
			ts_list_push(result, m);
		}
	}
	return result ? result : members;
}

static TsNodeArray *parenthesize_type_parameters(TsNodeArray *type_parameters)
{
	if (!type_parameters || !type_parameters->n)
		return NULL;
	TsNodeArray *params = ts_list_new();
	for (int i = 0; i < type_parameters->n; i++) {
		TsNode *entry = type_parameters->v[i];
		bool fn = entry->kind == TS_FunctionType || entry->kind == TS_ConstructorType;
		ts_list_push(params, i == 0 && fn && entry->type_parameters ? ts_create_parenthesized_type(entry) : entry);
	}
	return params;
}

static TsNode *parenthesize_concise_body(TsNode *body)
{
	if (body->kind != TS_Block &&
	    (is_comma_sequence(body) || get_leftmost_expression(body, false)->kind == TS_ObjectLiteralExpression))
		return create_paren_with_range(body);
	return body;
}
