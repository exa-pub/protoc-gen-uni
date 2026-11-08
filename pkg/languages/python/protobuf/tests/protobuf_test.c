/* Unit tests of pkg/languages/python/protobuf: text_format.Merge. The expected values were
 * printed by protobuf 7.36.2 (pure Python) under CPython 3.13. */
#include "python_protobuf.h"

#include "test.h"

#include <string.h>

static const PypbEnumValue colors[] = {{"RED", 0}, {"GREEN", 2}};
static const PypbEnum color = {"Color", 2, colors};
static const PypbField fields[] = {
	{"size", PT_INT32, false, NULL}, {"id", PT_UINT32, false, NULL}, {"on", PT_BOOL, false, NULL},
	{"name", PT_STRING, false, NULL}, {"color", PT_ENUM, false, &color}, {"tags", PT_STRING, true, NULL},
};
static const PypbMessage msg = {"Msg", 6, fields};

static Buf got;

static void set(void *ctx, int f, int64_t i, Str s)
{
	(void)ctx;
	buf_fmt(&got, "%s=", fields[f].name);
	if (fields[f].type == PT_STRING)
		buf_add_str(&got, s);
	else
		buf_fmt(&got, "%lld", (long long)i);
	buf_addc(&got, ';');
}

static const char *merge(const char *text)
{
	got = (Buf){0};
	PypbError err;
	if (!pypb_text_merge(&msg, str_c(text), set, NULL, &err))
		return err.exc;
	return buf_str(&got);
}

static void test_merge(void)
{
	CHECK_STR(merge("size:32"), "size=32;");
	CHECK_STR(merge("size: 0x10 id:017, on:t; color:GREEN color:0"), "size=16;id=15;on=1;color=2;color=0;");
	CHECK_STR(merge("name:\"a\\tb\" 'c' tags:[\"x\", 'y'] # comment\n tags:\"z\""), "name=a\tbc;tags=x;tags=y;tags=z;");
	CHECK_STR(merge("foo:1"), "google.protobuf.text_format.ParseError: 1:1 : Message type \"Msg\" has no field named \"foo\".\n");
	CHECK_STR(merge("size 5"), "google.protobuf.text_format.ParseError: 1:6 : 'size 5': Expected \":\".\n");
	CHECK_STR(merge("-5:3"),
		"google.protobuf.text_format.ParseError: 1:1 : '-5:3': Expected identifier or number, got -5.\n");
	CHECK_STR(merge("size:3000000000"),
		"google.protobuf.text_format.ParseError: 1:6 : 'size:3000000000': Value out of range: 3000000000\n");
	CHECK_STR(merge("on:maybe"), "google.protobuf.text_format.ParseError: 1:4 : 'on:maybe': Expected \"true\" or \"false\".\n");
	CHECK_STR(merge("color:BLUE"),
		"google.protobuf.text_format.ParseError: 1:7 : 'color:BLUE': Enum type \"Color\" has no value named BLUE.\n");
	CHECK_STR(merge("color:7"),
		"google.protobuf.text_format.ParseError: 1:7 : 'color:7': Enum type \"Color\" has no value with number 7.\n");
	CHECK_STR(merge("name:abc"), "google.protobuf.text_format.ParseError: 1:6 : 'name:abc': Expected string but found: 'abc'\n");
	CHECK_STR(merge("name:\"abc"),
		"google.protobuf.text_format.ParseError: 1:6 : 'name:\"abc': String missing ending quote: '\"abc'\n");
	CHECK_STR(merge("[foo]:1"), "google.protobuf.text_format.ParseError: 1:2 : Message type \"Msg\" does not have extensions.\n");
	CHECK_STR(merge("size:1.5"), "google.protobuf.text_format.ParseError: 1:6 : 'size:1.5': Couldn't parse integer: 1.5\n");
}

TESTS(TEST(test_merge))
