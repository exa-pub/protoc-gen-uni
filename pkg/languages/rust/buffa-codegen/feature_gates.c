/* Copyright Anthropic, PBC. Licensed under the Apache License, Version 2.0.
 *
 * Port of buffa-codegen 0.9.2 src/feature_gates.rs: the cfg helpers (cfg_block, cfg_block_any,
 * cfg_const_block, cfg_attr). FeatureGates::for_config and validate are in lib.c. */

static FeatureGates gates(void) { return feature_gates_for_config(CFG); }

static void prepend_attr(PpNode *node, PpNode *attr)
{
	PpNode *tmp = pp_node(PP_NONE);
	pp_push_attr(tmp, attr);
	if (node->attrs)
		for (int i = 0; i < node->attrs->n; i++)
			pp_push_attr(tmp, node->attrs->v[i]);
	node->attrs = tmp->attrs;
}

static PpNode *cfg_feature_attr(const char *feature)
{
	PpNode *a = pp_attr_list(pp_path("cfg"), PP_DELIM_PAREN, false);
	pp_tt_push_src(a, "feature =");
	pp_push(a, pp_tt_lit(pm2_string(str_c(feature))));
	return a;
}

/* cfg_block: `#[cfg(feature = "...")]` on the single item (or statement) of `items` */
static void cfg_block(Vec *out, Vec *items, const char *gate)
{
	if (gate && items->n) {
		PpNode *first = items->v[0];
		prepend_attr(first->kind == PP_S_ITEM ? first->a : first, cfg_feature_attr(gate));
	}
	vcat(out, items);
}

/* cfg_block_any over the json/text gates */
static void cfg_block_any(Vec *out, Vec *items, const char *const *gates_, int n)
{
	if (n == 1 || !items->n) {
		cfg_block(out, items, n ? gates_[0] : NULL);
		return;
	}
	if (n > 1) {
		PpNode *a = pp_attr_list(pp_path("cfg"), PP_DELIM_PAREN, false);
		PpNode *any = pp_tt_group(PP_DELIM_PAREN);
		for (int i = 0; i < n; i++) {
			if (i)
				pp_push(any, pp_tt_punct(',', false));
			pp_tt_push_src(any, "feature =");
			pp_push(any, pp_tt_lit(pm2_string(str_c(gates_[i]))));
		}
		pp_push(a, pp_tt_ident("any"));
		pp_push(a, any);
		PpNode *first = items->v[0];
		prepend_attr(first->kind == PP_S_ITEM ? first->a : first, a);
	}
	vcat(out, items);
}

/* cfg_const_block: `#[cfg(feature = "...")] const _: () = { items };` */
static void cfg_const_block(Vec *out, Vec *items, const char *gate)
{
	if (!gate || !items->n) {
		vcat(out, items);
		return;
	}
	PpNode *block = pp_block(NULL);
	for (int i = 0; i < items->n; i++)
		pp_push(block, pp_s_item(items->v[i]));
	PpNode *c = pp_item_const(0, "_", pp_ty_tuple(NULL), pp_e_block(block));
	pp_push_attr(c, cfg_feature_attr(gate));
	vec_push(out, c);
}

/* cfg_attr: `#[body]` or `#[cfg_attr(feature = "...", body)]` (body: constant tokens) */
static void cfg_attr(Vec *attrs, const char *body, const char *gate)
{
	if (!gate) {
		const char *paren = strchr(body, '(');
		if (!paren) {
			vec_push(attrs, attr_word(body));
			return;
		}
		char *path = xstrndup(body, (size_t)(paren - body));
		char *inner = xstrndup(paren + 1, strlen(paren + 1) - 1);
		vec_push(attrs, attr_src(path, inner));
		return;
	}
	PpNode *a = pp_attr_list(pp_path("cfg_attr"), PP_DELIM_PAREN, false);
	pp_tt_push_src(a, "feature =");
	pp_push(a, pp_tt_lit(pm2_string(str_c(gate))));
	pp_push(a, pp_tt_punct(',', false));
	pp_tt_push_src(a, body);
	vec_push(attrs, a);
}
