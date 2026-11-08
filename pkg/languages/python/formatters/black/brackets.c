/* black/brackets.py: BracketTracker and delimiter priorities. */

static int math_priority(int t)
{
	switch (t) {
	case BK_VBAR: return 9;
	case BK_CIRCUMFLEX: return 8;
	case BK_AMPER: return 7;
	case BK_LEFTSHIFT: case BK_RIGHTSHIFT: return 6;
	case BK_PLUS: case BK_MINUS: return 5;
	case BK_STAR: case BK_SLASH: case BK_DOUBLESLASH: case BK_PERCENT: case BK_AT: return 4;
	case BK_TILDE: return 3;
	case BK_DOUBLESTAR: return 2;
	}
	return 0;
}

static size_t ptr_hash(const void *p)
{
	uintptr_t x = (uintptr_t)p;
	x ^= x >> 17;
	x *= (uintptr_t)0x9E3779B97F4A7C15ull;
	return (size_t)(x ^ (x >> 29));
}

static void delim_set(BracketTracker *bt, BkNode *key, int prio)
{
	if ((bt->ndelim + 1) * 2 > bt->capslots) {
		int cap = bt->capslots ? bt->capslots * 2 : 16;
		DelimSlot *old = bt->slots;
		int oldcap = bt->capslots;
		bt->slots = xalloc(sizeof *bt->slots * (size_t)cap);
		bt->capslots = cap;
		bt->ndelim = 0;
		for (int i = 0; i < oldcap; i++)
			if (old[i].key)
				delim_set(bt, old[i].key, old[i].prio);
	}
	size_t m = (size_t)bt->capslots - 1;
	for (size_t i = ptr_hash(key) & m;; i = (i + 1) & m) {
		if (bt->slots[i].key == key) {
			bt->slots[i].prio = prio;
			return;
		}
		if (!bt->slots[i].key) {
			bt->slots[i].key = key;
			bt->slots[i].prio = prio;
			bt->ndelim++;
			return;
		}
	}
}

static int bt_get(const BracketTracker *bt, const BkNode *leaf)
{
	if (!bt->capslots)
		return 0;
	size_t m = (size_t)bt->capslots - 1;
	for (size_t i = ptr_hash(leaf) & m; bt->slots[i].key; i = (i + 1) & m)
		if (bt->slots[i].key == leaf)
			return bt->slots[i].prio;
	return 0;
}

static int bt_max_priority(const BracketTracker *bt, BkNode *const *exclude, int nexclude)
{
	int best = -1;
	for (int i = 0; i < bt->capslots; i++) {
		BkNode *k = bt->slots[i].key;
		if (!k)
			continue;
		bool ex = false;
		for (int j = 0; j < nexclude; j++)
			if (exclude[j] == k)
				ex = true;
		if (!ex && bt->slots[i].prio > best)
			best = bt->slots[i].prio;
	}
	return best;
}

static int bt_count_with_priority(const BracketTracker *bt, int prio)
{
	if (!bt->ndelim)
		return 0;
	if (!prio)
		prio = bt_max_priority(bt, NULL, 0);
	int n = 0;
	for (int i = 0; i < bt->capslots; i++)
		if (bt->slots[i].key && bt->slots[i].prio == prio)
			n++;
	return n;
}

static void push_int(int **v, int *n, int *cap, int x)
{
	if (*n == *cap) {
		int nc = *cap ? *cap * 2 : 4;
		*v = xrealloc(*v, sizeof **v * (size_t)*cap, sizeof **v * (size_t)nc);
		*cap = nc;
	}
	(*v)[(*n)++] = x;
}

static int find_match(const BracketTracker *bt, int depth, int type)
{
	for (int i = bt->nmatch - 1; i >= 0; i--)
		if (bt->match[i].depth == depth && bt->match[i].type == type)
			return i;
	return -1;
}

static int is_split_after_delimiter(const BkNode *leaf) { return leaf->type == BK_COMMA ? COMMA_PRIORITY : 0; }

static int is_split_before_delimiter(const BkNode *leaf, const BkNode *previous)
{
	if (is_vararg(leaf, W_VARARGS | W_UNPACKING))
		return 0;
	int t = leaf->type;
	const BkNode *p = leaf->parent;
	if (t == BK_DOT && p && p->type != BK_import_from && p->type != BK_dotted_name &&
	    (!previous || is_closing(previous->type)))
		return DOT_PRIORITY;
	if (is_math_operator(t) && p && p->type != BK_factor && p->type != BK_star_expr)
		return math_priority(t);
	if (IN(t, BK_LESS, BK_GREATER, BK_EQEQUAL, BK_NOTEQUAL, BK_LESSEQUAL, BK_GREATEREQUAL))
		return COMPARATOR_PRIORITY;
	if (t == BK_STRING && previous && previous->type == BK_STRING)
		return STRING_PRIORITY;
	if (t != BK_NAME && t != BK_ASYNC)
		return 0;
	const char *v = leaf->value;
	if ((!strcmp(v, "for") && p && (p->type == BK_comp_for || p->type == BK_old_comp_for)) || t == BK_ASYNC) {
		BkNode *ps = prev_sibling(leaf);
		if (!ps || !IS_LEAF(ps) || strcmp(ps->value, "async"))
			return COMPREHENSION_PRIORITY;
	}
	if (!strcmp(v, "if") && p && (p->type == BK_comp_if || p->type == BK_old_comp_if))
		return COMPREHENSION_PRIORITY;
	if ((!strcmp(v, "if") || !strcmp(v, "else")) && p && p->type == BK_test)
		return TERNARY_PRIORITY;
	if (!strcmp(v, "is"))
		return COMPARATOR_PRIORITY;
	if (!strcmp(v, "in") && p && (p->type == BK_comp_op || p->type == BK_comparison) &&
	    !(previous && previous->type == BK_NAME && !strcmp(previous->value, "not")))
		return COMPARATOR_PRIORITY;
	if (!strcmp(v, "not") && p && p->type == BK_comp_op &&
	    !(previous && previous->type == BK_NAME && !strcmp(previous->value, "is")))
		return COMPARATOR_PRIORITY;
	if ((!strcmp(v, "and") || !strcmp(v, "or")) && p)
		return LOGIC_PRIORITY;
	return 0;
}

static void bt_mark(BracketTracker *bt, BkNode *leaf)
{
	if (leaf->type == BK_COMMENT)
		return;
	if (bt->depth == 0 && is_closing(leaf->type) && find_match(bt, bt->depth, leaf->type) < 0)
		return;
	/* maybe_decrement_after_for_loop_variable */
	if (bt->nfor && bt->for_depths[bt->nfor - 1] == bt->depth && is_name_value(leaf, "in")) {
		bt->depth--;
		bt->nfor--;
	}
	/* maybe_decrement_after_lambda_arguments */
	if (bt->nlambda && bt->lambda_depths[bt->nlambda - 1] == bt->depth && leaf->type == BK_COLON) {
		bt->depth--;
		bt->nlambda--;
	}
	if (is_closing(leaf->type)) {
		bt->depth--;
		int i = find_match(bt, bt->depth, leaf->type);
		if (i < 0)
			fail("black: BracketMatchError: Unable to match a closing bracket");
		BkNode *opening = bt->match[i].leaf;
		memmove(bt->match + i, bt->match + i + 1, sizeof *bt->match * (size_t)(bt->nmatch - i - 1));
		bt->nmatch--;
		leaf->opening_bracket = opening;
		if (!*leaf->value) {
			if (bt->ninv == bt->capinv) {
				int nc = bt->capinv ? bt->capinv * 2 : 4;
				bt->invisible = xrealloc(bt->invisible, sizeof *bt->invisible * (size_t)bt->capinv,
							 sizeof *bt->invisible * (size_t)nc);
				bt->capinv = nc;
			}
			bt->invisible[bt->ninv++] = leaf;
		}
	}
	leaf->bracket_depth = bt->depth;
	if (bt->depth == 0) {
		int delim = is_split_before_delimiter(leaf, bt->previous);
		if (delim && bt->previous) {
			delim_set(bt, bt->previous, delim);
		} else {
			delim = is_split_after_delimiter(leaf);
			if (delim)
				delim_set(bt, leaf, delim);
		}
	}
	if (is_opening(leaf->type)) {
		if (bt->nmatch == bt->capmatch) {
			int nc = bt->capmatch ? bt->capmatch * 2 : 4;
			bt->match = xrealloc(bt->match, sizeof *bt->match * (size_t)bt->capmatch,
					     sizeof *bt->match * (size_t)nc);
			bt->capmatch = nc;
		}
		bt->match[bt->nmatch++] = (BracketMatch){bt->depth, bracket_of(leaf->type), leaf};
		bt->depth++;
		if (!*leaf->value) {
			if (bt->ninv == bt->capinv) {
				int nc = bt->capinv ? bt->capinv * 2 : 4;
				bt->invisible = xrealloc(bt->invisible, sizeof *bt->invisible * (size_t)bt->capinv,
							 sizeof *bt->invisible * (size_t)nc);
				bt->capinv = nc;
			}
			bt->invisible[bt->ninv++] = leaf;
		}
	}
	bt->previous = leaf;
	/* maybe_increment_lambda_arguments, maybe_increment_for_loop_variable */
	if (is_name_value(leaf, "lambda")) {
		bt->depth++;
		push_int(&bt->lambda_depths, &bt->nlambda, &bt->caplambda, bt->depth);
	}
	if (is_name_value(leaf, "for")) {
		bt->depth++;
		push_int(&bt->for_depths, &bt->nfor, &bt->capfor, bt->depth);
	}
}

static bool bt_any_open_brackets(const BracketTracker *bt) { return bt->nmatch > 0; }
static bool bt_any_open_for_or_lambda(const BracketTracker *bt) { return bt->nfor || bt->nlambda; }

static BkNode *bt_get_open_lsqb(const BracketTracker *bt)
{
	int i = find_match(bt, bt->depth - 1, BK_RSQB);
	return i < 0 ? NULL : bt->match[i].leaf;
}

static int max_delimiter_priority_in_atom(BkNode *n)
{
	if (n->type != BK_atom)
		return 0;
	if (!(n->kids[0]->type == BK_LPAR && n->kids[n->n - 1]->type == BK_RPAR))
		return 0;
	BracketTracker bt = {0};
	for (int i = 1; i < n->n - 1; i++) {
		BkNode **ls;
		int nl = leaves_of(n->kids[i], &ls);
		for (int j = 0; j < nl; j++)
			bt_mark(&bt, ls[j]);
	}
	int p = bt_max_priority(&bt, NULL, 0);
	return p < 0 ? 0 : p;
}

static void leaves_inside_matching_brackets(BkNode **leaves, int n, bool *in)
{
	memset(in, 0, (size_t)n);
	int start = 0;
	while (start < n && !is_opening(leaves[start]->type))
		start++;
	if (start == n)
		return;
	int *stack_type = xalloc(sizeof(int) * (size_t)n), *stack_i = xalloc(sizeof(int) * (size_t)n), sp = 0;
	for (int i = start; i < n; i++) {
		BkNode *l = leaves[i];
		if (is_opening(l->type)) {
			stack_type[sp] = bracket_of(l->type);
			stack_i[sp++] = i;
		}
		if (is_closing(l->type)) {
			if (sp && l->type == stack_type[sp - 1]) {
				int s = stack_i[--sp];
				for (int j = s; j <= i; j++)
					in[j] = true;
			} else {
				break;
			}
		}
	}
}
