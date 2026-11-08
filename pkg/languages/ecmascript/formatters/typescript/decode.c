/* Parsed nodes from a compact encoding (ts_decode_parsed in typescript.h).
 *
 * A node is: varint kind * 4 + 1 if it has a text + 2 if it has other properties (0: undefined),
 * the text, the varint property bits if any, the properties present, then child slots (varint
 * slot, value) up to slot 0. Property bits: 2 rawText, 4 singleQuote, 8 multiLine, 16 flags,
 * 32 numericLiteralFlags, 64 operator, 128 token, 256 the first parameter of an arrow function
 * starts where the arrow function starts (`x => ...`). Texts are varint indices into the string
 * table. A head of 1 refers back to an earlier node: a varint distance from the start of the
 * reference back to that node's encoding, which is decoded again (new nodes, new positions). Slots 1..N are the node fields of node_slots, N+1..N+M the list
 * fields of list_slots; a list is a varint (count * 2 + hasTrailingComma) and its nodes. */

static const size_t node_slots[] = {
	offsetof(TsNode, name), offsetof(TsNode, property_name), offsetof(TsNode, expression), offsetof(TsNode, type),
	offsetof(TsNode, initializer), offsetof(TsNode, body), offsetof(TsNode, left), offsetof(TsNode, right),
	offsetof(TsNode, operator_token), offsetof(TsNode, question_token), offsetof(TsNode, colon_token),
	offsetof(TsNode, exclamation_token), offsetof(TsNode, dot_dot_dot_token), offsetof(TsNode, asterisk_token),
	offsetof(TsNode, question_dot_token), offsetof(TsNode, equals_greater_than_token), offsetof(TsNode, await_modifier),
	offsetof(TsNode, condition), offsetof(TsNode, when_true), offsetof(TsNode, when_false),
	offsetof(TsNode, then_statement), offsetof(TsNode, else_statement), offsetof(TsNode, statement),
	offsetof(TsNode, incrementor), offsetof(TsNode, argument_expression), offsetof(TsNode, operand),
	offsetof(TsNode, head), offsetof(TsNode, literal), offsetof(TsNode, label), offsetof(TsNode, constraint),
	offsetof(TsNode, default_), offsetof(TsNode, element_type), offsetof(TsNode, object_type),
	offsetof(TsNode, index_type), offsetof(TsNode, type_name), offsetof(TsNode, import_clause),
	offsetof(TsNode, module_specifier), offsetof(TsNode, named_bindings), offsetof(TsNode, case_block),
	offsetof(TsNode, declaration_list),
};

static const size_t list_slots[] = {
	offsetof(TsNode, decorators), offsetof(TsNode, modifiers), offsetof(TsNode, type_parameters),
	offsetof(TsNode, parameters), offsetof(TsNode, type_arguments), offsetof(TsNode, arguments),
	offsetof(TsNode, members), offsetof(TsNode, statements), offsetof(TsNode, elements),
	offsetof(TsNode, properties), offsetof(TsNode, declarations), offsetof(TsNode, types),
	offsetof(TsNode, heritage_clauses), offsetof(TsNode, clauses), offsetof(TsNode, template_spans),
};

enum { N_NODE_SLOTS = sizeof node_slots / sizeof *node_slots, N_LIST_SLOTS = sizeof list_slots / sizeof *list_slots };

typedef struct {
	const uint8_t *p;
	const char *const *strings;
	const char *const *params;
	int n_params;
	int pos; /* positions of the decoded nodes, increasing */
} Decoder;

static uint32_t dec_varint(Decoder *d)
{
	uint32_t v = 0;
	for (int shift = 0;; shift += 7) {
		uint8_t b = *d->p++;
		v |= (uint32_t)(b & 0x7f) << shift;
		if (!(b & 0x80))
			return v;
	}
}

/* The string with each "$$<n>$$" replaced by params[n]. */
static Str dec_text(Decoder *d)
{
	const char *s = d->strings[dec_varint(d)];
	if (!strstr(s, "$$"))
		return str_c(s);
	Buf b = {0};
	for (const char *p = s; *p;) {
		if (p[0] == '$' && p[1] == '$' && p[2] >= '0' && p[2] <= '9') {
			const char *q = p + 2;
			int n = 0;
			while (*q >= '0' && *q <= '9')
				n = n * 10 + (*q++ - '0');
			if (q[0] == '$' && q[1] == '$' && n < d->n_params) {
				buf_adds(&b, d->params[n]);
				p = q + 2;
				continue;
			}
		}
		buf_addc(&b, *p++);
	}
	return buf_view(&b);
}

static TsNode *dec_node(Decoder *d)
{
	const uint8_t *start = d->p;
	uint32_t head = dec_varint(d);
	if (!head)
		return NULL;
	if (head == 1) {
		uint32_t dist = dec_varint(d);
		const uint8_t *resume = d->p;
		d->p = start - dist;
		TsNode *n = dec_node(d);
		d->p = resume;
		return n;
	}
	int pos = d->pos++;
	TsNode *n = new_node((TsSyntaxKind)(head >> 2), pos, 0);
	if (head & 1)
		n->text = dec_text(d);
	uint32_t props = head & 2 ? dec_varint(d) : 0;
	if (props & 2) {
		n->raw_text = dec_text(d);
		n->has_raw_text = true;
	}
	n->single_quote = (props & 4) != 0;
	n->multi_line = (props & 8) != 0;
	if (props & 16)
		n->flags = dec_varint(d);
	if (props & 32)
		n->numeric_literal_flags = (int)dec_varint(d);
	if (props & 64)
		n->operator_ = (TsSyntaxKind)dec_varint(d);
	if (props & 128)
		n->token = (TsSyntaxKind)dec_varint(d);
	for (uint32_t slot; (slot = dec_varint(d));) {
		if (slot <= N_NODE_SLOTS) {
			*(TsNode **)((char *)n + node_slots[slot - 1]) = dec_node(d);
			continue;
		}
		uint32_t c = dec_varint(d);
		TsNodeArray *a = node_array_with_range(pos);
		a->has_trailing_comma = c & 1;
		for (uint32_t i = 0; i < c / 2; i++)
			ts_list_push(a, dec_node(d));
		a->end = d->pos;
		*(TsNodeArray **)((char *)n + list_slots[slot - 1 - N_NODE_SLOTS]) = a;
	}
	if ((props & 256) && n->parameters && n->parameters->n)
		n->parameters->v[0]->pos = pos;
	n->end = d->pos++;
	return n;
}

TsNode *ts_decode_parsed(const uint8_t **code, const char *const *strings, const char *const *params, int n_params)
{
	Decoder d = {*code, strings, params, n_params, 0};
	TsNode *n = dec_node(&d);
	*code = d.p;
	return n;
}
