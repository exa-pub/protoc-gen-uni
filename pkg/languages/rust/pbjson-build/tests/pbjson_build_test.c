/* The unit tests of pbjson-build 0.8.0 (src/descriptor.rs, src/resolver.rs), plus the keyword
 * escaping of src/escape.rs and descriptor_set_find. Includes the unity build to reach its static
 * functions. */
#include "../pbjson_build.c"
#include "test.h"

static TypePath *path(const char *package, const char *a, const char *b)
{
	TypePath *t = package_new(package);
	if (a)
		t = type_path_child(t, a);
	if (b)
		t = type_path_child(t, b);
	return t;
}

static void test_prefix_match(void)
{
	TypePath *t = path("foo.bar", "Baz", "Bar");
	CHECK(prefix_match(t, ".") == 0);
	CHECK(prefix_match(t, "..") == -1);
	CHECK(prefix_match(t, ".foo") == 1);
	CHECK(prefix_match(t, ".foo.") == -1);
	CHECK(prefix_match(t, ".foo.bar") == 2);
	CHECK(prefix_match(t, ".foo.bar.Bar") == -1);
	CHECK(prefix_match(t, ".foo.bar.Baz") == 3);
	CHECK(prefix_match(t, ".foo.bar.Baza") == -1);
	CHECK(prefix_match(t, ".foo.bar.Baz.Bar") == 4);
	CHECK(prefix_match(t, ".foo.bar.Baz.Bar.Boo") == -1);
}

static void test_package_display(void)
{
	/* test_handle_camel_case_in_package, escape_keywords */
	CHECK_STR(package_display(package_new("fooBar.baz.boo")), "foo_bar.baz.boo");
	CHECK_STR(package_display(package_new("type.abstract")), "r#type.r#abstract");
	CHECK_STR(package_display(package_new("self.Super.gen.extern")), "self_.super_.gen.extern_");
	CHECK_STR(type_path_display(path("test.empty", "Normal", "Inner")), "test.empty.Normal.Inner");
	CHECK_STR(upper_camel_type("self"), "Self_");
}

static void test_resolver(void)
{
	TypePath *package = package_new("test.syntax3");
	Vec extern_types = {0};
	const char *pairs[] = {".test.external", "foo::common", ".test.external.Boo", "foo::common",
			       ".test.external.Fiz.Buz", "foo::bar::Buz"};
	for (size_t i = 0; i < sizeof pairs / sizeof *pairs; i++)
		vec_push(&extern_types, (void *)pairs[i]);
	Resolver r = {&extern_types, package, false, NULL};

	/* A type in the same package */
	CHECK_STR(rust_type(&r, path("test.syntax3", "Foo", NULL)), "Foo");
	CHECK_STR(rust_type(&r, path("test.syntax3", "Foo", "Bar")), "foo::Bar");
	/* A type in another package */
	CHECK_STR(rust_type(&r, path("test.common", "Bar", NULL)), "super::common::Bar");
	CHECK_STR(rust_type(&r, path("test.common", "Foo", "Bar")), "super::common::foo::Bar");
	/* An external type */
	CHECK_STR(rust_type(&r, path("test.external", "Fiz", NULL)), "foo::common::Fiz");
	CHECK_STR(rust_type(&r, path("test.external", "Fiz", "Bar")), "foo::common::fiz::Bar");
	/* An external type within an external type */
	CHECK_STR(rust_type(&r, path("test.external", "Fiz", "Buz")), "foo::bar::Buz");
	/* An external type with a different rust path length */
	CHECK_STR(rust_type(&r, path("test.external", "Boo", "Bar")), "foo::common::Bar");

	/* test_resolver_shared_prefix_false_match (pbjson issue 48) */
	Vec none = {0};
	Resolver r2 = {&none, package_new("test.api.v1"), false, NULL};
	CHECK_STR(rust_type(&r2, path("test.domain.v1", "Foo", "Bar")), "super::super::domain::v1::foo::Bar");
}

static void test_variant(void)
{
	Vec none = {0};
	TypePath *package = package_new("test.syntax3");
	Resolver r = {&none, package, false, NULL};
	static const char *const cases[][3] = {
		{"MyEnum", "MyEnumFoo", "Foo"},
		{"MyEnum", "MyEnumfoo", "MyEnumfoo"},
		{"MyEnum", "MY_ENUM_foo", "Foo"},
	};
	for (size_t i = 0; i < sizeof cases / sizeof *cases; i++)
		CHECK_STR(rust_variant(&r, path("test.syntax3", cases[i][0], NULL), cases[i][1]), cases[i][2]);
	r.retain_enum_prefix = true;
	CHECK_STR(rust_variant(&r, path("test.syntax3", "MyEnum", NULL), "MY_ENUM_foo"), "MyEnumFoo");
}

static void test_escape(void)
{
	CHECK_STR(escape_ident("type"), "r#type");
	CHECK_STR(escape_ident("try"), "r#try");
	CHECK_STR(escape_ident("gen"), "gen");
	CHECK_STR(escape_ident("crate"), "crate_");
	CHECK_STR(escape_ident("extern"), "extern_");
	CHECK_STR(escape_ident("Self"), "Self");
	CHECK_STR(escape_type("Self"), "Self_");
	CHECK_STR(escape_type("self"), "self");
}

/* A descriptor of package pkg with the dot-separated type path names (no proto). */
static void add(DescriptorSet *set, const char *pkg, const char *names)
{
	TypePath *t = intern_package(set, pkg);
	Vec parts = str_split(names, '.');
	for (int i = 0; i < parts.n; i++)
		t = type_path_child(t, parts.v[i]);
	Descriptor *d = xalloc(sizeof *d);
	d->path = t;
	register_descriptor(set, d);
}

static void run_duplicate(void *arg) { add(arg, "a.b", "C"); }

/* descriptor_set_find against upstream's linear scan over the sorted set. */
static void test_find(void)
{
	DescriptorSet set = {0};
	static const char *const descs[][2] = {
		{"a", "b"}, {"a", "b.C"}, {"a.b", "C"}, {"a.b", "A"}, {"a.b.C", "D"}, {"a.b.C.D", "E"}, {"", "X"},
		{"", "a"}, {"a.c", "B"}, {"a.b", "C.D"}, {"z", "Y.Z"}, {"a.", "b"}, {"a..b", "C"}, {"b", "a"},
	};
	for (size_t i = 0; i < sizeof descs / sizeof *descs; i++)
		add(&set, descs[i][0], descs[i][1]);
	CHECK(try_run(run_duplicate, &set) != NULL);
	CHECK_STR(fail_message().p, "descriptor already registered for a.b.C");
	descriptor_set_sort(&set);
	for (int i = 1; i < set.v.n; i++)
		CHECK(type_path_cmp(VEC_AT(set.v, Descriptor, i - 1)->path, VEC_AT(set.v, Descriptor, i)->path) < 0);
	static const char *const names[] = {
		".", "..", ".a", ".a.b", ".a.b.C", ".a.b.C.D", ".a.b.C.D.E", ".a.b.C.D.E.F", ".a.b.A", ".a.c", ".a.c.B",
		".X", ".a.", ".a..b", ".a..b.C", ".z", ".z.Y.Z", ".b", ".b.a", ".q", ".a.b.D", ".z.Y", "...",
	};
	for (size_t i = 0; i < sizeof names / sizeof *names; i++) {
		const Descriptor *want = NULL;
		for (int j = 0; j < set.v.n && !want; j++)
			if (prefix_match(VEC_AT(set.v, Descriptor, j)->path, names[i]) >= 0)
				want = set.v.v[j];
		CHECK(descriptor_set_find(&set, names[i]) == want);
		CHECK(descriptor_set_find(&set, names[i]) == want); /* cached */
	}
}

/* Panics of upstream become errors. */
static void run_prefix_match(void *arg) { prefix_match(arg, "foo"); }
static void run_package_new(void *arg) { package_new(arg); }

static void test_panics(void)
{
	CHECK(try_run(run_prefix_match, path("foo", "Bar", NULL)) != NULL);
	CHECK(try_run(run_package_new, ".foo") != NULL);
}

TESTS(TEST(test_prefix_match), TEST(test_package_display), TEST(test_resolver), TEST(test_variant), TEST(test_escape),
      TEST(test_find), TEST(test_panics))
