/* Unit tests of pkg/languages/go/grpc-gateway. The expected values come from upstream
 * (grpc-gateway v2.33.0): internal/casing/camel_test.go, internal/httprule/compile_test.go,
 * utilities/trie_test.go, and the errors of protoc-gen-grpc-gateway for these templates. */
#include "grpcgateway.h"
#include "test.h"

static void test_casing(void)
{
	CHECK_STR(gw_casing_CamelIdentifier("snake_case"), "SnakeCase");
	CHECK_STR(gw_casing_CamelIdentifier("snake_case_0_enum"), "SnakeCase_0Enum");
	CHECK_STR(gw_casing_CamelIdentifier("pathenum.snake_case"), "pathenum.SnakeCase");
	CHECK_STR(gw_casing_CamelIdentifier("path.pathenum.snake_case_0_enum"), "path.pathenum.SnakeCase_0Enum");
	CHECK_STR(gw_casing_Camel("_my_field_name_2"), "XMyFieldName_2");
	CHECK_STR(gw_casing_Camel(""), "");
	CHECK_STR(gw_casing_JSONCamelCase("foo_bar_baz"), "fooBarBaz");
}

/* "ops|pool|fields|verb" of a compiled template */
static const char *compiled(const char *tmpl)
{
	Str err;
	GwHttpruleTemplate *t = gw_httprule_Parse(str_c(tmpl), &err);
	if (!t)
		return xcat("error: ", xstrndup(err.p, err.n));
	GwTemplate c = gw_httprule_Compile(t);
	Buf b = {0};
	for (int i = 0; i < c.nOpCodes; i++)
		buf_fmt(&b, i ? ",%d" : "%d", c.OpCodes[i]);
	buf_addc(&b, '|');
	for (int i = 0; i < c.nPool; i++) {
		if (i)
			buf_addc(&b, ',');
		buf_add_str(&b, c.Pool[i]);
	}
	buf_addc(&b, '|');
	for (int i = 0; i < c.nFields; i++) {
		if (i)
			buf_addc(&b, ',');
		buf_add_str(&b, c.Fields[i]);
	}
	buf_addc(&b, '|');
	buf_add_str(&b, c.Verb);
	return buf_str(&b);
}

static void test_httprule_compile(void)
{
	CHECK_STR(compiled("/"), "2,0|||");
	CHECK_STR(compiled("/*"), "1,0|||");
	CHECK_STR(compiled("/**"), "3,0|||");
	CHECK_STR(compiled("/v1:LOCK"), "2,0|v1||LOCK");
	CHECK_STR(compiled("/{name.nested}"), "1,0,4,1,5,0|name.nested|name.nested|");
	CHECK_STR(compiled("/obj/{name.nested=a/*/b}/{obj=**}"),
		  "2,0,2,1,1,0,2,2,4,3,5,3,3,0,4,1,5,0|obj,a,b,name.nested|name.nested,obj|");
	/* the parser accepts "/" for any punctuation: the second "/" of "//" is a wildcard */
	CHECK_STR(compiled("/a//"), "2,0,1,0|a||");
	CHECK_STR(compiled("/a//b"), "error: unexpected token \"b\" after segments \"a//\": /a//b");
	/* the verb after a variable is after its first colon */
	CHECK_STR(compiled("/v1/{id}:x:y"), "2,0,1,0,4,1,5,1|v1,id|id|x:y");
}

static void test_httprule_errors(void)
{
	CHECK_STR(compiled("/v1/{id"), "error: segment neither wildcards, literal or variable: unterminated variable segment: id: /v1/{id");
	CHECK_STR(compiled("v1/x"), "error: no leading /: v1/x");
	CHECK_STR(compiled("/v1/%zz"), "error: segment neither wildcards, literal or variable: expected \"{\" but got \"%zz\": /v1/%zz");
	CHECK_STR(compiled("/v1/{1x}"), "error: segment neither wildcards, literal or variable: identifier starting with digit: 1x: /v1/{1x}");
	CHECK_STR(compiled("/v1/{id=**}/x/{sub.}"),
		  "error: segment neither wildcards, literal or variable: invalid field path component: invalid character '}'(U+007D) "
		  "in identifier: }: /v1/{id=**}/x/{sub.}");
	CHECK_STR(compiled("/v1/\xc3\xa9"),
		  "error: segment neither wildcards, literal or variable: expected \"{\" but got \"\xc3\xa9\": /v1/\xc3\xa9");
	CHECK_STR(compiled("/v1/{i\xd0\xb4}"), "error: segment neither wildcards, literal or variable: invalid character "
					       "'\xd0\xb4'(U+0434) in identifier: i\xd0\xb4: /v1/{i\xd0\xb4}");
	CHECK_STR(compiled("/v1/x:b%g"), "error: invalid verb: invalid hexdigit: g(U+0067): /v1/x:b%g");
	CHECK_STR(compiled("/v1//{id}/{sub.name=a/*}}"), "error: unexpected token \"{\" after segments \"v1//\": /v1//{id}/{sub.name=a/*}}");
	CHECK_STR(compiled("/{id=}"), "error: segment neither wildcards, literal or variable: invalid segment in variable \"id\": "
				      "segment neither wildcards, literal or variable: expected \"{\" but got \"}\": /{id=}");
}

/* "Encoding|Base|Check" of NewDoubleArray */
static const char *trie(const char *const *const *seqs, const int *lens, int n)
{
	GwDoubleArray *da = gw_utilities_NewDoubleArray(seqs, lens, n);
	Buf b = {0};
	for (int i = 0; i < da->nEncoding; i++)
		buf_fmt(&b, i ? ",%s" : "%s", da->Encoding[i]);
	buf_addc(&b, '|');
	for (int a = 0; a < 2; a++) {
		if (da->nil)
			buf_adds(&b, "nil");
		for (int i = 0; i < da->n; i++)
			buf_fmt(&b, i ? ",%d" : "%d", (a ? da->Check : da->Base)[i]);
		if (!a)
			buf_addc(&b, '|');
	}
	return buf_str(&b);
}

static void test_trie(void)
{
	static const char *const foo[] = {"foo"}, *const foobar[] = {"foo", "bar"}, *const bar[] = {"bar"};
	CHECK_STR(trie(NULL, NULL, 0), "|nil|nil");
	{
		const char *const *seqs[] = {foo};
		int lens[] = {1};
		CHECK_STR(trie(seqs, lens, 1), "foo|1,1,0|0,1,2");
	}
	{
		const char *const *seqs[] = {foo, foobar, bar};
		int lens[] = {1, 2, 1};
		CHECK_STR(trie(seqs, lens, 3), "foo,bar|1,2,4,3,0,0,0|0,1,1,2,2,4,3");
	}
	{
		static const char *const abc[] = {"a", "b", "c"}, *const ab[] = {"a", "b"}, *const x[] = {"x"},
				  *const ay[] = {"a", "y"};
		const char *const *seqs[] = {abc, ab, x, ay};
		int lens[] = {3, 2, 1, 2};
		CHECK_STR(trie(seqs, lens, 4), "a,b,c,x,y|1,1,1,2,4,3,0,0,0,0|0,1,2,3,1,2,3,4,6,5");
	}
}

TESTS(TEST(test_casing), TEST(test_httprule_compile), TEST(test_httprule_errors), TEST(test_trie))
