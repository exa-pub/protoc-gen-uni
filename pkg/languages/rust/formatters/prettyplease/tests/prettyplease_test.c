/* Node fixtures of the prettyplease port: each test builds the syn tree of a source and checks the
 * output of prettyplease 0.2.37 (syn 2.0.114) for it, recorded with
 * `prettyplease::unparse(&syn::parse_file(src)?)`. The first cases come from the upstream
 * anthropics/buffa v0.9.2 output (already prettyplease output, so a fixed point). */
#include "prettyplease.h"
#include "test.h"

static PpNode *doc(const char *s)
{
	Buf b = {0};
	buf_addc(&b, '"');
	buf_adds(&b, s);
	buf_addc(&b, '"');
	return pp_attr_name_value(pp_path("doc"), pp_e_lit(pp_lit_str_repr(buf_str(&b), str_c(s))), false);
}

static PpNode *attr_tokens(const char *path, const char *idents)
{
	/* #[path(a, b::c, ...)] */
	PpNode *a = pp_attr_list(pp_path(path), PP_DELIM_PAREN, false);
	Vec parts = str_split(idents, ',');
	for (int i = 0; i < parts.n; i++) {
		if (i)
			pp_push(a, pp_tt_punct(',', false));
		const char *s = VEC_AT(parts, char, i);
		while (*s == ' ')
			s++;
		pp_tt_push_path(a, s);
	}
	return a;
}

static PpNode *lit_str(const char *s)
{
	return pp_e_lit(pp_lit_str_repr(xfmt("\"%s\"", s), str_c(s)));
}

static PpNode *self_field(const char *f) { return pp_e_field(pp_e("self"), f); }

static PpNode *ty_ref_mut(const char *path) { return pp_ty_ref(NULL, true, pp_ty(path)); }

static PpNode *arg(const char *name, PpNode *ty) { return pp_p_type(pp_p_ident(name, false, false), ty); }

static PpNode *allow_unused_use(const char *path)
{
	PpNode *u = pp_item_use(0, pp_use_tree(path), true);
	pp_push_attr(u, attr_tokens("allow", "unused_imports"));
	return pp_s_item(u);
}

static void test_message_struct(void)
{
	PpNode *file = pp_file();
	PpNode *st = pp_item_struct(PP_VIS_PUB, "Normal", PP_F_NAMED);
	pp_push_attr(st, attr_tokens("derive", "Clone, PartialEq, Default"));
	PpNode *f = pp_field(PP_VIS_PUB, "value", pp_ty("::buffa::alloc::string::String"));
	pp_push_attr(f, doc(" Field 1: `value`"));
	pp_push(st, f);
	f = pp_field(PP_VIS_PUB, "__buffa_unknown_fields", pp_ty("::buffa::UnknownFields"));
	pp_push_attr(f, attr_tokens("doc", "hidden"));
	pp_push(st, f);
	pp_push(file, st);

	/* impl ::core::fmt::Debug for Normal */
	PpNode *imp = pp_item_impl(pp_path("::core::fmt::Debug"), pp_ty("Normal"));
	PpNode *sig = pp_sig("fmt", pp_ty("::core::fmt::Result"));
	pp_push(sig, pp_receiver(true, false, NULL));
	pp_push(sig, arg("f", pp_ty_ref(NULL, true, pp_ty_gen("::core::fmt::Formatter", pp_lifetime("'_"), NULL))));
	PpNode *chain = pp_e_method(
		pp_e_method(pp_e_method(pp_e("f"), "debug_struct", lit_str("Normal"), NULL), "field", lit_str("value"),
			    pp_e_ref(false, self_field("value")), NULL),
		"finish", NULL);
	pp_push(imp, pp_item_fn(0, sig, pp_block(pp_s_expr(chain, false), NULL)));
	pp_push(file, imp);

	/* impl Normal { pub const TYPE_URL ... } */
	imp = pp_item_impl(NULL, pp_ty("Normal"));
	PpNode *c = pp_item_const(PP_VIS_PUB, "TYPE_URL", pp_ty_ref("'static", false, pp_ty("str")),
				  lit_str("type.googleapis.com/test.empty.Normal"));
	pp_push_attr(c, doc(" Protobuf type URL for this message, for use with `Any::pack` and"));
	pp_push_attr(c, doc(" `Any::unpack_if`."));
	pp_push_attr(c, doc(""));
	pp_push_attr(c, doc(" Format: `type.googleapis.com/<fully.qualified.TypeName>`"));
	pp_push(imp, c);
	pp_push(file, imp);

	PpNode *mac = pp_macro(pp_path("::buffa::impl_default_instance"), PP_DELIM_PAREN);
	pp_push(mac, pp_tt_ident("Normal"));
	pp_push(file, pp_item_macro(mac, NULL));

	/* impl ::buffa::Message for Normal { fn write_to ..., fn merge_field ... } */
	imp = pp_item_impl(pp_path("::buffa::Message"), pp_ty("Normal"));
	sig = pp_sig("write_to", NULL);
	pp_push(sig, pp_receiver(true, false, NULL));
	pp_push(sig, arg("_cache", ty_ref_mut("::buffa::SizeCache")));
	pp_push(sig, arg("buf", pp_ty_ref(NULL, true, pp_ty_impl(pp_bounds(pp_trait_bound(pp_path("::buffa::EncodeSink"), false), NULL)))));
	PpNode *body = pp_block(allow_unused_use("buffa::Enumeration as _"), NULL);
	PpNode *put = pp_e_call(pp_e("::buffa::types::put_string_field"), pp_e_lit_int("1u32"),
				pp_e_ref(false, self_field("value")), pp_e("buf"), NULL);
	pp_push(body, pp_s_expr(pp_e_if(pp_e_unary("!", pp_e_method(self_field("value"), "is_empty", NULL)),
					pp_block(pp_s_expr(put, true), NULL), NULL),
				false));
	pp_push(body, pp_s_expr(pp_e_method(self_field("__buffa_unknown_fields"), "write_to", pp_e("buf"), NULL), true));
	pp_push(imp, pp_item_fn(0, sig, body));

	sig = pp_sig("merge_field",
		     pp_ty_gen("::core::result::Result", pp_ty_tuple(NULL), pp_ty("::buffa::DecodeError"), NULL));
	pp_push(sig, pp_receiver(true, true, NULL));
	pp_push(sig, arg("tag", pp_ty("::buffa::encoding::Tag")));
	pp_push(sig, arg("buf", pp_ty_ref(NULL, true, pp_ty_impl(pp_bounds(pp_trait_bound(pp_path("::buffa::bytes::Buf"), false), NULL)))));
	pp_push(sig, arg("ctx", pp_ty_gen("::buffa::DecodeContext", pp_lifetime("'_"), NULL)));
	body = pp_block(allow_unused_use("buffa::bytes::Buf as _"), allow_unused_use("buffa::Enumeration as _"), NULL);
	PpNode *m = pp_e_match(pp_e_method(pp_e("tag"), "field_number", NULL));
	PpNode *arm1 = pp_block(
		pp_s_expr(pp_e_try(pp_e_call(pp_e("::buffa::encoding::check_wire_type"), pp_e("tag"),
					     pp_e("::buffa::encoding::WireType::LengthDelimited"), NULL)),
			  true),
		pp_s_expr(pp_e_try(pp_e_call(pp_e("::buffa::types::merge_string"), pp_e_ref(true, self_field("value")),
					     pp_e("buf"), NULL)),
			  true),
		NULL);
	pp_push(m, pp_arm(pp_p_lit(pp_e_lit_int("1u32")), NULL, pp_e_block(arm1)));
	PpNode *arm2 = pp_block(
		pp_s_expr(pp_e_method(self_field("__buffa_unknown_fields"), "push",
				      pp_e_try(pp_e_call(pp_e("::buffa::encoding::decode_unknown_field"), pp_e("tag"),
							 pp_e("buf"), pp_e("ctx"), NULL)),
				      NULL),
			  true),
		NULL);
	pp_push(m, pp_arm(pp_p_wild(), NULL, pp_e_block(arm2)));
	pp_push(body, pp_s_expr(m, false));
	pp_push(body, pp_s_expr(pp_e_call(pp_e("::core::result::Result::Ok"), pp_e_tuple(NULL), NULL), false));
	pp_push(imp, pp_item_fn(0, sig, body));
	pp_push(file, imp);

	CHECK_STR(pp_unparse(file),
		  "#[derive(Clone, PartialEq, Default)]\n"
		  "pub struct Normal {\n"
		  "    /// Field 1: `value`\n"
		  "    pub value: ::buffa::alloc::string::String,\n"
		  "    #[doc(hidden)]\n"
		  "    pub __buffa_unknown_fields: ::buffa::UnknownFields,\n"
		  "}\n"
		  "impl ::core::fmt::Debug for Normal {\n"
		  "    fn fmt(&self, f: &mut ::core::fmt::Formatter<'_>) -> ::core::fmt::Result {\n"
		  "        f.debug_struct(\"Normal\").field(\"value\", &self.value).finish()\n"
		  "    }\n"
		  "}\n"
		  "impl Normal {\n"
		  "    /// Protobuf type URL for this message, for use with `Any::pack` and\n"
		  "    /// `Any::unpack_if`.\n"
		  "    ///\n"
		  "    /// Format: `type.googleapis.com/<fully.qualified.TypeName>`\n"
		  "    pub const TYPE_URL: &'static str = \"type.googleapis.com/test.empty.Normal\";\n"
		  "}\n"
		  "::buffa::impl_default_instance!(Normal);\n"
		  "impl ::buffa::Message for Normal {\n"
		  "    fn write_to(\n"
		  "        &self,\n"
		  "        _cache: &mut ::buffa::SizeCache,\n"
		  "        buf: &mut impl ::buffa::EncodeSink,\n"
		  "    ) {\n"
		  "        #[allow(unused_imports)]\n"
		  "        use ::buffa::Enumeration as _;\n"
		  "        if !self.value.is_empty() {\n"
		  "            ::buffa::types::put_string_field(1u32, &self.value, buf);\n"
		  "        }\n"
		  "        self.__buffa_unknown_fields.write_to(buf);\n"
		  "    }\n"
		  "    fn merge_field(\n"
		  "        &mut self,\n"
		  "        tag: ::buffa::encoding::Tag,\n"
		  "        buf: &mut impl ::buffa::bytes::Buf,\n"
		  "        ctx: ::buffa::DecodeContext<'_>,\n"
		  "    ) -> ::core::result::Result<(), ::buffa::DecodeError> {\n"
		  "        #[allow(unused_imports)]\n"
		  "        use ::buffa::bytes::Buf as _;\n"
		  "        #[allow(unused_imports)]\n"
		  "        use ::buffa::Enumeration as _;\n"
		  "        match tag.field_number() {\n"
		  "            1u32 => {\n"
		  "                ::buffa::encoding::check_wire_type(\n"
		  "                    tag,\n"
		  "                    ::buffa::encoding::WireType::LengthDelimited,\n"
		  "                )?;\n"
		  "                ::buffa::types::merge_string(&mut self.value, buf)?;\n"
		  "            }\n"
		  "            _ => {\n"
		  "                self.__buffa_unknown_fields\n"
		  "                    .push(::buffa::encoding::decode_unknown_field(tag, buf, ctx)?);\n"
		  "            }\n"
		  "        }\n"
		  "        ::core::result::Result::Ok(())\n"
		  "    }\n"
		  "}\n");
}

/* Doc strings that cannot be line comments, struct literals with the 34-column rule, closures,
 * arrays, a match with expression arms (output of prettyplease 0.2.37). */
static void test_shapes(void)
{
	PpNode *file = pp_file();
	PpNode *sig = pp_sig("f", pp_ty("u32"));
	PpNode *body = pp_block(NULL);
	PpNode *s = pp_e_struct(pp_path("Foo"), pp_e_call(pp_e("::core::default::Default::default"), NULL));
	pp_push(s, pp_field_value("a", pp_e_lit_int("1")));
	pp_push(body, pp_s_local(pp_p_ident("x", false, false), s));
	s = pp_e_struct(pp_path("Foo"), NULL);
	pp_push(s, pp_field_value("a", NULL));
	pp_push(body, pp_s_local(pp_p_ident("y", false, false), s));
	PpNode *clo = pp_e_closure(pp_e_binary(pp_e("v"), "+", pp_e_lit_int("1")), false);
	pp_push(clo, pp_p_ident("v", false, false));
	pp_push(body, pp_s_expr(pp_e_method(pp_e_method(pp_e("xs"), "iter", NULL), "map", clo, NULL), true));
	pp_push(body, pp_s_local(pp_p_ident("z", false, false),
				 pp_e_ref(false, pp_e_array(pp_e_lit_int("0u8"), pp_e_lit_int("171u8"), NULL))));
	PpNode *m = pp_e_match(pp_e("v"));
	pp_push(m, pp_arm(pp_p_lit(pp_e_lit_int("-3i32")), NULL,
			  pp_e_call(pp_e("::core::option::Option::Some"), pp_e("Self::STATUS_NEGATIVE"), NULL)));
	pp_push(m, pp_arm(pp_p_wild(), NULL, pp_e("::core::option::Option::None")));
	pp_push(body, pp_s_expr(m, false));
	PpNode *fn = pp_item_fn(0, sig, body);
	pp_push_attr(fn, pp_attr_name_value(pp_path("doc"),
					    pp_e_lit(pp_lit_str_repr("\"a\\nb */\"", str_c("a\nb */"))), false));
	pp_push_attr(fn, pp_attr_name_value(pp_path("doc"),
					    pp_e_lit(pp_lit_str_repr("\" x\\n y \"", str_c(" x\n y "))), false));
	pp_push(file, fn);
	CHECK_STR(pp_unparse(file),
		  "#[doc = \"a\\nb */\"]\n"
		  "/** x\n"
		  " y */\n"
		  "fn f() -> u32 {\n"
		  "    let x = Foo {\n"
		  "        a: 1,\n"
		  "        ..::core::default::Default::default()\n"
		  "    };\n"
		  "    let y = Foo { a };\n"
		  "    xs.iter().map(|v| v + 1);\n"
		  "    let z = &[0u8, 171u8];\n"
		  "    match v {\n"
		  "        -3i32 => ::core::option::Option::Some(Self::STATUS_NEGATIVE),\n"
		  "        _ => ::core::option::Option::None,\n"
		  "    }\n"
		  "}\n");
}

/* Width limits: an impl header broken before `for`, a compound assignment broken at its operators,
 * a call broken inside, a match arm wrapped in braces, a long string that cannot break. */
static void test_width(void)
{
	PpNode *file = pp_file();
	PpNode *imp = pp_item_impl(
		pp_path_args(pp_path("::core::convert::AsRef"),
			     pp_angle(pp_ty_gen("::buffa::OwnedView",
						pp_ty_gen("NormalViewWithAVeryLongName", pp_lifetime("'static"), NULL), NULL),
				      NULL)),
		pp_ty("NormalOwnedView"));
	PpNode *sig = pp_sig("from_proto_name", pp_ty_gen("::core::option::Option", pp_ty("Self"), NULL));
	pp_push(sig, arg("name", pp_ty_ref(NULL, false, pp_ty("str"))));
	PpNode *len = pp_e_call(pp_e("::buffa::types::string_encoded_len"),
				pp_e_ref(false, self_field("some_very_long_field_name_here_for_width")), NULL);
	PpNode *body = pp_block(
		pp_s_expr(pp_e_binary(pp_e("size"), "+=",
				      pp_e_binary(pp_e_lit_int("1u64"), "+", pp_e_cast(len, pp_ty("u64")))),
			  true),
		NULL);
	PpNode *m = pp_e_match(pp_e("name"));
	pp_push(m, pp_arm(pp_p_lit(lit_str("STATUS_UNSPECIFIED_WITH_LONGER_NAME")), NULL,
			  pp_e_call(pp_e("::core::option::Option::Some"), pp_e("Self::STATUS_UNSPECIFIED_WITH_LONGER_NAME"),
				    NULL)));
	pp_push(m, pp_arm(pp_p_wild(), NULL, pp_e("::core::option::Option::None")));
	pp_push(body, pp_s_expr(m, false));
	pp_push(imp, pp_item_fn(0, sig, body));
	pp_push(imp, pp_item_const(PP_VIS_PUB, "TYPE_URL", pp_ty_ref("'static", false, pp_ty("str")),
				   lit_str("type.googleapis.com/a.very.long.package.name.that.goes.on.and.on.Message")));
	pp_push(file, imp);
	CHECK_STR(pp_unparse(file),
		  "impl ::core::convert::AsRef<::buffa::OwnedView<NormalViewWithAVeryLongName<'static>>>\n"
		  "for NormalOwnedView {\n"
		  "    fn from_proto_name(name: &str) -> ::core::option::Option<Self> {\n"
		  "        size\n"
		  "            += 1u64\n"
		  "                + ::buffa::types::string_encoded_len(\n"
		  "                    &self.some_very_long_field_name_here_for_width,\n"
		  "                ) as u64;\n"
		  "        match name {\n"
		  "            \"STATUS_UNSPECIFIED_WITH_LONGER_NAME\" => {\n"
		  "                ::core::option::Option::Some(Self::STATUS_UNSPECIFIED_WITH_LONGER_NAME)\n"
		  "            }\n"
		  "            _ => ::core::option::Option::None,\n"
		  "        }\n"
		  "    }\n"
		  "    pub const TYPE_URL: &'static str = \"type.googleapis.com/a.very.long.package.name.that.goes.on.and.on.Message\";\n"
		  "}\n");
}

TESTS(TEST(test_message_struct), TEST(test_shapes), TEST(test_width))
