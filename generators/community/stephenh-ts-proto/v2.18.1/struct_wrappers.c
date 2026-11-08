/* Port of ts-proto v2.18.1 src/generate-struct-wrappers.ts: the shallow `wrap`/`unwrap` methods of
 * google.protobuf.Struct, Value, ListValue and FieldMask, shallow and deep (for nestJs).
 *
 * ts-proto: Copyright (c) Stephen Haberman, Apache-2.0 License. */

typedef struct {
	const char *null_value, *number_value, *string_value, *bool_value, *struct_value, *list_value;
} StructFieldNames;

static const char *struct_field_name(const char *name)
{
	TpdField f = {0};
	f.name = name;
	f.json_name = snake_to_camel(name);
	return get_field_name(&f);
}

static StructFieldNames struct_field_names(void)
{
	StructFieldNames n = {struct_field_name("null_value"), struct_field_name("number_value"), struct_field_name("string_value"),
			      struct_field_name("bool_value"), struct_field_name("struct_value"), struct_field_name("list_value")};
	return n;
}

static bool is_wrapper_type(const char *n)
{
	return is_struct_type_name(n) || is_any_value_type_name(n) || is_list_value_type_name(n) || is_field_mask_type_name(n);
}

static Node *create_base_any(const char *type)
{
	Node *e = call(ID(xcat("createBase", wrap_type_name(type, false))), NULL);
	return O.use_readonly_types ? dts_as(e, KW("any")) : e;
}

static Node *typeof_is(const char *t) { return BIN(EQEQEQ, dts_unary(UNARY_TYPEOF, ID("value")), sq(t)); }

/* generateWrapShallow: method properties appended to l */
static void generate_wrap_shallow(TpCode *parent, const char *full_type_name, const StructFieldNames *fn, NodeList *l)
{
	if (is_struct_type_name(full_type_name)) {
		TpCode *c = tp_add(parent, tp_code(NULL));
		Node *set = O.use_map_type ? ES(mcall(MEM(ID("struct"), "fields"), "set", ID("key"), dts_index(ID("object"), ID("key")), NULL))
					   : ES(SET(dts_index(MEM(ID("struct"), "fields"), ID("key")), dts_index(ID("object"), ID("key"))));
		Node *loop = dts_for_of(dts_var_decl(VAR_CONST), mcall(MEM(tp_co(c, C.u->global_this), "Object"), "keys", ID("object"), NULL),
					block(set, NULL), false);
		dts_add(&loop->a->list, dts_declarator(dts_binding("key", NULL, false), NULL));
		NodeList body = L(const_("struct", NULL, call(ID(xcat("createBase", wrap_type_name("Struct", false))), NULL)), NULL);
		Node *ifs = dts_if(BIN(NOTEQEQ, ID("object"), UNDEF()), block(loop, NULL), NULL);
		if (O.uo_all) {
			/* struct.fields ??= {}; or new Map<string, any | undefined>(); */
			Node *init = dts_obj();
			if (O.use_map_type) {
				init = newx(ID("Map"), NULL);
				init->b = dts_targs(L(KW("string"), dts_tunion(KW("any"), KW("undefined"), NULL), NULL));
			}
			dts_add(&body, ES(dts_assign(TK_NULLISH_ASSIGN, MEM(ID("struct"), "fields"), init)));
			dts_add(&body, ifs);
		} else {
			dts_add(&body, BLANK(ifs));
		}
		dts_add(&body, RET(ID("struct")));
		Node *pt = dts_tunion(any_object_type(), KW("undefined"), NULL);
		dts_add(l, method("wrap", NULL, L(param("object", pt, false, NULL), NULL), tref(wrap_type_name("Struct", false), NULL), blockl(body)));
	}
	if (is_any_value_type_name(full_type_name)) {
		TpCode *c = tp_add(parent, tp_code(NULL));
		const char *names[6] = {fn->null_value, fn->bool_value, fn->number_value, fn->string_value, fn->list_value, fn->struct_value};
		Node *tests[6] = {BIN(EQEQEQ, ID("value"), dts_null()), typeof_is("boolean"), typeof_is("number"), typeof_is("string"),
				  call(MEM(MEM(tp_co(c, C.u->global_this), "Array"), "isArray"), ID("value"), NULL), typeof_is("object")};
		Node *chain = NULL, **slot = &chain;
		for (int k = 0; k < 6; k++) {
			Node *v = k == 0 ? MEM(ID(wrap_type_name("NullValue", true)), "NULL_VALUE") : ID("value");
			Node *asg;
			if (O.oneof == ONEOF_UNIONS) {
				asg = ES(SET(MEM(ID("result"), "kind"), obj(kv("$case", sq(names[k])), kv(names[k], v), NULL)));
			} else if (O.oneof == ONEOF_UNIONS_VALUE) {
				Node *o = k == 0 ? obj(kv("$case", sq(names[k])), kv("value", v), NULL) : obj(kv("$case", sq(names[k])), shorthand("value"), NULL);
				asg = ES(SET(MEM(ID("result"), "kind"), o));
			} else {
				asg = ES(SET(MEM(ID("result"), names[k]), v));
			}
			Node *ifs = dts_if(tests[k], block(asg, NULL), NULL);
			*slot = ifs;
			slot = &ifs->c;
		}
		Node *err = newx(MEM(tp_co(c, C.u->global_this), "Error"), BIN(ADD, SQ("Unsupported any value type: "), dts_unary(UNARY_TYPEOF, ID("value"))), NULL);
		*slot = dts_if(BIN(NOTEQEQ, dts_unary(UNARY_TYPEOF, ID("value")), SQ("undefined")), block(dts_throw(err), NULL), NULL);
		Node *body = block(const_("result", NULL, create_base_any("Value")), chain, RET(ID("result")), NULL);
		dts_add(l, method("wrap", NULL, L(param("value", KW("any"), false, NULL), NULL), tref(wrap_type_name("Value", false), NULL), body));
	}
	if (is_list_value_type_name(full_type_name)) {
		Node *arr = tref(O.use_readonly_types ? "ReadonlyArray" : "Array", KW("any"), NULL);
		Node *body = block(const_("result", NULL, create_base_any("ListValue")),
				   ES(SET(MEM(ID("result"), "values"), BIN(NULLISH, ID("array"), dts_arr(NULL)))), RET(ID("result")), NULL);
		dts_add(l, method("wrap", NULL, L(param("array", dts_tunion(arr, KW("undefined"), NULL), false, NULL), NULL),
				  tref(wrap_type_name("ListValue", false), NULL), body));
		tp_add(parent, tp_code(NULL));
	}
	if (is_field_mask_type_name(full_type_name)) {
		Node *a = dts_tarray(KW("string"));
		Node *body = block(const_("result", NULL, create_base_any("FieldMask")), ES(SET(MEM(ID("result"), "paths"), ID("paths"))),
				   RET(ID("result")), NULL);
		dts_add(l, method("wrap", NULL, L(param("paths", O.use_readonly_types ? dts_treadonly(a) : a, false, NULL), NULL),
				  tref(wrap_type_name("FieldMask", false), NULL), body));
		tp_add(parent, tp_code(NULL));
	}
}

static Node *any_unwrap_type(void)
{
	return dts_tunion(KW("string"), KW("number"), KW("boolean"), tref("Object", NULL), KW("null"), tref("Array", KW("any"), NULL), KW("undefined"),
			  NULL);
}

/* generateFieldMaskUnwrap */
static void generate_field_mask_unwrap(TpCode *parent, NodeList *l)
{
	tp_add(parent, tp_code(NULL));
	Node *rt = O.uo_all ? dts_tunion(dts_tarray(KW("string")), KW("undefined"), NULL) : dts_tarray(KW("string"));
	Node *pt = O.use_readonly_types ? KW("any") : tref(wrap_type_name("FieldMask", false), NULL);
	dts_add(l, method("unwrap", NULL, L(param("message", pt, false, NULL), NULL), rt,
			  block(RET(dts_opt_member(ID("message"), "paths", O.uo_all)), NULL)));
}

static void generate_unwrap_shallow(TpCode *parent, const char *full_type_name, const StructFieldNames *fn, NodeList *l)
{
	if (is_struct_type_name(full_type_name)) {
		TpCode *c = tp_add(parent, tp_code(NULL));
		Node *obj_t = any_object_type(); /* { [key: string]: any } */
		Node *loop;
		if (O.use_map_type) {
			Node *asg = ES(SET(dts_index(ID("object"), ID("key")), mcall(MEM(ID("message"), "fields"), "get", ID("key"), NULL)));
			loop = dts_for_of(dts_var_decl(VAR_CONST), mcall(MEM(ID("message"), "fields"), "keys", NULL), block(asg, NULL), false);
		} else {
			Node *asg = ES(SET(dts_index(ID("object"), ID("key")), dts_index(MEM(ID("message"), "fields"), ID("key"))));
			loop = dts_for_of(dts_var_decl(VAR_CONST), mcall(MEM(tp_co(c, C.u->global_this), "Object"), "keys", MEM(ID("message"), "fields"), NULL),
					  block(asg, NULL), false);
		}
		dts_add(&loop->a->list, dts_declarator(dts_binding("key", NULL, false), NULL));
		Node *body = block(const_("object", obj_t, dts_obj()), dts_if(MEM(ID("message"), "fields"), block(loop, NULL), NULL), RET(ID("object")), NULL);
		dts_add(l, method("unwrap", NULL, L(param("message", tref(wrap_type_name("Struct", false), NULL), false, NULL), NULL), any_object_type(), body));
	}
	if (is_any_value_type_name(full_type_name)) {
		tp_add(parent, tp_code(NULL));
		if (O.oneof == ONEOF_UNIONS) {
			const char *names[6] = {fn->null_value, fn->number_value, fn->string_value, fn->bool_value, fn->struct_value, fn->list_value};
			Node *chain = NULL, **slot = &chain;
			for (int k = 0; k < 6; k++) {
				Node *test = BIN(EQEQEQ, dts_opt_member(MEM(ID("message"), "kind"), "$case", true), sq(names[k]));
				Node *r = k == 0 ? RET(dts_null()) : RET(dts_opt_member(MEM(ID("message"), "kind"), names[k], true));
				Node *ifs = dts_if(test, block(r, NULL), NULL);
				*slot = ifs;
				slot = &ifs->c;
			}
			*slot = block(RET(UNDEF()), NULL);
			dts_add(l, method("unwrap", NULL, L(param("message", tref(wrap_type_name("Value", false), NULL), false, NULL), NULL), any_unwrap_type(),
					  block(chain, NULL)));
		} else if (O.oneof == ONEOF_UNIONS_VALUE) {
			Node *test = dts_paren(BIN(EQEQEQ, dts_opt_member(MEM(ID("message"), "kind"), "$case", true), sq(fn->null_value)));
			Node *r = RET(dts_cond(test, dts_null(), dts_opt_member(MEM(ID("message"), "kind"), "value", true)));
			dts_add(l, method("unwrap", NULL, L(param("message", tref(wrap_type_name("Value", false), NULL), false, NULL), NULL), any_unwrap_type(),
					  block(r, NULL)));
		} else {
			const char *names[6] = {fn->string_value, fn->number_value, fn->bool_value, fn->struct_value, fn->list_value, fn->null_value};
			Node *chain = NULL, **slot = &chain;
			for (int k = 0; k < 6; k++) {
				Node *msg = k == 0 ? MEM(ID("message"), names[k]) : dts_opt_member(ID("message"), names[k], true);
				Node *test = BIN(NOTEQEQ, msg, UNDEF());
				Node *rv = k == 5 ? dts_null() : MEM(ID("message"), names[k]);
				if (k == 3)
					rv = dts_as(rv, KW("any"));
				Node *ifs = dts_if(test, block(RET(rv), NULL), NULL);
				*slot = ifs;
				slot = &ifs->c;
			}
			dts_add(l, method("unwrap", NULL, L(param("message", KW("any"), false, NULL), NULL), any_unwrap_type(), block(chain, RET(UNDEF()), NULL)));
		}
	}
	if (is_list_value_type_name(full_type_name)) {
		TpCode *c = tp_add(parent, tp_code(NULL));
		Node *test = BIN(LOGICAL_AND, call(dts_opt_member(ID("message"), "hasOwnProperty", true), SQ("values"), NULL),
				 call(MEM(MEM(tp_co(c, C.u->global_this), "Array"), "isArray"), MEM(ID("message"), "values"), NULL));
		Node *ifs = dts_if(test, block(RET(MEM(ID("message"), "values")), NULL), block(RET(dts_as(ID("message"), KW("any"))), NULL));
		Node *pt = O.use_readonly_types ? KW("any") : tref(wrap_type_name("ListValue", false), NULL);
		dts_add(l, method("unwrap", NULL, L(param("message", pt, false, NULL), NULL), tref("Array", KW("any"), NULL), block(ifs, NULL)));
	}
	if (is_field_mask_type_name(full_type_name))
		generate_field_mask_unwrap(parent, l);
}

/* generateWrapDeep */
static void generate_wrap_deep(TpCode *parent, const char *full_type_name, const StructFieldNames *fn, NodeList *l)
{
	if (is_struct_type_name(full_type_name)) {
		TpCode *c = tp_add(parent, tp_code(NULL));
		Node *wrapped = mcall(ID(wrap_type_name("Value", false)), "wrap", dts_index(ID("object"), ID("key")), NULL);
		Node *set = O.use_map_type ? ES(mcall(MEM(ID("struct"), "fields"), "set", ID("key"), wrapped, NULL))
					   : ES(SET(dts_index(MEM(ID("struct"), "fields"), ID("key")), wrapped));
		Node *loop = dts_for_of(dts_var_decl(VAR_CONST), mcall(MEM(tp_co(c, C.u->global_this), "Object"), "keys", ID("object"), NULL),
					block(set, NULL), false);
		dts_add(&loop->a->list, dts_declarator(dts_binding("key", NULL, false), NULL));
		NodeList body = L(const_("struct", NULL, call(ID(xcat("createBase", wrap_type_name("Struct", false))), NULL)), NULL);
		Node *ifs = dts_if(BIN(NOTEQEQ, ID("object"), UNDEF()), block(loop, NULL), NULL);
		if (O.uo_all) {
			Node *init = dts_obj();
			if (O.use_map_type) {
				init = newx(ID("Map"), NULL);
				init->b = dts_targs(L(KW("string"), dts_tunion(KW("any"), KW("undefined"), NULL), NULL));
			}
			dts_add(&body, ES(dts_assign(TK_NULLISH_ASSIGN, MEM(ID("struct"), "fields"), init)));
			dts_add(&body, ifs);
		} else {
			dts_add(&body, BLANK(ifs));
		}
		dts_add(&body, RET(ID("struct")));
		Node *pt = dts_tunion(any_object_type(), KW("undefined"), NULL);
		dts_add(l, method("wrap", NULL, L(param("object", pt, false, NULL), NULL), tref(wrap_type_name("Struct", false), NULL), blockl(body)));
	}
	if (is_any_value_type_name(full_type_name)) {
		TpCode *c = tp_add(parent, tp_code(NULL));
		const char *names[6] = {fn->null_value, fn->bool_value, fn->number_value, fn->string_value, fn->list_value, fn->struct_value};
		Node *tests[6] = {BIN(EQEQEQ, ID("value"), dts_null()), typeof_is("boolean"), typeof_is("number"), typeof_is("string"),
				  call(MEM(MEM(tp_co(c, C.u->global_this), "Array"), "isArray"), ID("value"), NULL), typeof_is("object")};
		Node *chain = NULL, **slot = &chain;
		for (int k = 0; k < 6; k++) {
			Node *v = k == 0   ? MEM(ID(wrap_type_name("NullValue", true)), "NULL_VALUE")
				  : k == 4 ? mcall(ID(wrap_type_name("ListValue", false)), "wrap", ID("value"), NULL)
				  : k == 5 ? mcall(ID(wrap_type_name("Struct", false)), "wrap", ID("value"), NULL)
					   : ID("value");
			Node *ifs = dts_if(tests[k], block(ES(SET(MEM(ID("result"), names[k]), v)), NULL), NULL);
			*slot = ifs;
			slot = &ifs->c;
		}
		Node *err = newx(MEM(tp_co(c, C.u->global_this), "Error"), BIN(ADD, SQ("Unsupported any value type: "), dts_unary(UNARY_TYPEOF, ID("value"))), NULL);
		*slot = dts_if(BIN(NOTEQEQ, dts_unary(UNARY_TYPEOF, ID("value")), SQ("undefined")), block(dts_throw(err), NULL), NULL);
		Node *body = block(const_("result", NULL, dts_as(dts_obj(), KW("any"))), chain, RET(ID("result")), NULL);
		dts_add(l, method("wrap", NULL, L(param("value", KW("any"), false, NULL), NULL), tref(wrap_type_name("Value", false), NULL), body));
	}
	if (is_list_value_type_name(full_type_name)) {
		Node *arr = tref(O.use_readonly_types ? "ReadonlyArray" : "Array", KW("any"), NULL);
		Node *values = mcall(dts_paren(BIN(NULLISH, ID("array"), dts_arr(NULL))), "map", MEM(ID(wrap_type_name("Value", false)), "wrap"), NULL);
		Node *body = block(const_("result", NULL, create_base_any("ListValue")), ES(SET(MEM(ID("result"), "values"), values)), RET(ID("result")), NULL);
		dts_add(l, method("wrap", NULL, L(param("array", dts_tunion(arr, KW("undefined"), NULL), false, NULL), NULL),
				  tref(wrap_type_name("ListValue", false), NULL), body));
		tp_add(parent, tp_code(NULL));
	}
	if (is_field_mask_type_name(full_type_name)) {
		Node *a = dts_tarray(KW("string"));
		Node *body = block(const_("result", NULL, create_base_any("FieldMask")), ES(SET(MEM(ID("result"), "paths"), ID("paths"))),
				   RET(ID("result")), NULL);
		dts_add(l, method("wrap", NULL, L(param("paths", O.use_readonly_types ? dts_treadonly(a) : a, false, NULL), NULL),
				  tref(wrap_type_name("FieldMask", false), NULL), body));
		tp_add(parent, tp_code(NULL));
	}
}

/* message?.hasOwnProperty('name') && message?.name !== undefined (the first without `?.` in the second) */
static Node *has_own(const char *name, bool opt2)
{
	return BIN(LOGICAL_AND, call(dts_opt_member(ID("message"), "hasOwnProperty", true), sq(name), NULL),
		   BIN(NOTEQEQ, opt2 ? dts_opt_member(ID("message"), name, true) : MEM(ID("message"), name), UNDEF()));
}

/* generateUnwrapDeep */
static void generate_unwrap_deep(TpCode *parent, const char *full_type_name, const StructFieldNames *fn, NodeList *l)
{
	if (is_struct_type_name(full_type_name)) {
		if (O.use_map_type)
			fail("unsupported by protoc-gen-uni: the ts-proto options nestJs and useMapType with google.protobuf.Struct "
			     "(upstream emits invalid TypeScript, unformatted)");
		TpCode *c = tp_add(parent, tp_code(NULL));
		Node *asg = ES(SET(dts_index(ID("object"), ID("key")), mcall(ID("Value"), "unwrap", dts_index(MEM(ID("message"), "fields"), ID("key")), NULL)));
		Node *loop = dts_for_of(dts_var_decl(VAR_CONST), mcall(MEM(tp_co(c, C.u->global_this), "Object"), "keys", MEM(ID("message"), "fields"), NULL),
					block(asg, NULL), false);
		dts_add(&loop->a->list, dts_declarator(dts_binding("key", NULL, false), NULL));
		Node *body = block(const_("object", any_object_type(), dts_obj()), dts_if(MEM(ID("message"), "fields"), block(loop, NULL), NULL), RET(ID("object")), NULL);
		dts_add(l, method("unwrap", NULL, L(param("message", tref(wrap_type_name("Struct", false), NULL), false, NULL), NULL), any_object_type(), body));
	}
	if (is_any_value_type_name(full_type_name)) {
		tp_add(parent, tp_code(NULL));
		const char *names[6] = {fn->string_value, fn->number_value, fn->bool_value, fn->struct_value, fn->list_value, fn->null_value};
		Node *chain = NULL, **slot = &chain;
		for (int k = 0; k < 6; k++) {
			Node *rv = k == 3   ? mcall(ID(wrap_type_name("Struct", false)), "unwrap", dts_as(MEM(ID("message"), names[k]), KW("any")), NULL)
				   : k == 4 ? mcall(ID(wrap_type_name("ListValue", false)), "unwrap", MEM(ID("message"), names[k]), NULL)
				   : k == 5 ? dts_null()
					    : MEM(ID("message"), names[k]);
			Node *ifs = dts_if(has_own(names[k], k > 0), block(RET(rv), NULL), NULL);
			*slot = ifs;
			slot = &ifs->c;
		}
		dts_add(l, method("unwrap", NULL, L(param("message", KW("any"), false, NULL), NULL), any_unwrap_type(), block(chain, RET(UNDEF()), NULL)));
	}
	if (is_list_value_type_name(full_type_name)) {
		TpCode *c = tp_add(parent, tp_code(NULL));
		Node *test = BIN(LOGICAL_AND, call(dts_opt_member(ID("message"), "hasOwnProperty", true), SQ("values"), NULL),
				 call(MEM(MEM(tp_co(c, C.u->global_this), "Array"), "isArray"), MEM(ID("message"), "values"), NULL));
		Node *ifs = dts_if(test, block(RET(mcall(MEM(ID("message"), "values"), "map", MEM(ID("Value"), "unwrap"), NULL)), NULL),
				   block(RET(dts_as(ID("message"), KW("any"))), NULL));
		Node *pt = O.use_readonly_types ? KW("any") : tref(wrap_type_name("ListValue", false), NULL);
		dts_add(l, method("unwrap", NULL, L(param("message", pt, false, NULL), NULL), tref("Array", KW("any"), NULL), block(ifs, NULL)));
	}
	if (is_field_mask_type_name(full_type_name))
		generate_field_mask_unwrap(parent, l);
}
