/* Copyright (c) 2014 - 2016 Apple Inc. and the project authors
 * Licensed under Apache License v2.0 with Runtime Library Exception
 *
 * See LICENSE.txt for license information:
 * https://github.com/apple/swift-protobuf/blob/main/LICENSE.txt */

/* Port of Sources/protoc-gen-swift/EnumGenerator.swift. */

#define UNRECOGNIZED_CASE_NAME "UNRECOGNIZED"

typedef struct {
	Gen *g;
	SwEnum *ed;
	ValueAliasInfo *alias_info;
	Vec main_sorted; /* mainEnumValueDescriptorsSorted */
	char *swift_relative_name, *swift_full_name;
} EnumGen;

static EnumGen *enum_gen(Gen *g, SwEnum *ed)
{
	EnumGen *e = xalloc(sizeof *e);
	e->g = g;
	e->ed = ed;
	e->alias_info = value_alias_info(ed);
	/* main values have distinct numbers: any sort gives the same order */
	for (int i = 0; i < e->alias_info->main_values.n; i++)
		vec_push(&e->main_sorted, e->alias_info->main_values.v[i]);
	for (int i = 1; i < e->main_sorted.n; i++) {
		SwEnumValue *x = e->main_sorted.v[i];
		int j = i;
		while (j > 0 && VEC_AT(e->main_sorted, SwEnumValue, j - 1)->number > x->number) {
			e->main_sorted.v[j] = e->main_sorted.v[j - 1];
			j--;
		}
		e->main_sorted.v[j] = x;
	}
	e->swift_relative_name = sw_relative_name_enum(g->namer, ed);
	e->swift_full_name = sw_full_name_enum(g->namer, ed);
	return e;
}

static void enum_generate_cases_or_aliases(SwPrinter *p, EnumGen *e)
{
	Gen *g = e->g;
	bool closed = sw_enum_is_closed(e->ed);
	Vec values = uniquely_named_values(g->namer, e->ed, e->alias_info);
	for (int i = 0; i < values.n; i++) {
		const SwEnumValue *v = values.v[i];
		Str comments = gen_comments_with_deprecation(g, SW_ENUM_VALUE, v);
		if (comments.n)
			sw_print_blank(p);
		const char *relative = sw_relative_name_enum_value(g->namer, v);
		const SwEnumValue *alias_of = e->alias_info->alias_of[v->index];
		if (alias_of)
			print_c(p, comments, "%sstatic let %s = %s", vis(g), relative, sw_relative_name_enum_value(g->namer, alias_of));
		else if (closed)
			print_c(p, comments, "case %s = %d", relative, (int)v->number);
		else
			print_c(p, comments, "case %s // = %d", relative, (int)v->number);
	}
	if (!closed)
		sw_print(p, "case " UNRECOGNIZED_CASE_NAME "(Int)");
}

static void enum_generate_init_raw_value(SwPrinter *p, EnumGen *e)
{
	Gen *g = e->g;
	sw_printf(p, "%sinit?(rawValue: Int) {", vis(g));
	sw_indent(p);
	sw_print(p, "switch rawValue {");
	for (int i = 0; i < e->main_sorted.n; i++) {
		const SwEnumValue *v = e->main_sorted.v[i];
		sw_printf(p, "case %d: self = %s", (int)v->number, sw_dotted_relative_name_enum_value(g->namer, v));
	}
	if (!sw_enum_is_closed(e->ed))
		sw_print(p, "default: self = ." UNRECOGNIZED_CASE_NAME "(rawValue)");
	else
		sw_print(p, "default: return nil");
	sw_print(p, "}");
	sw_outdent(p);
	sw_print(p, "}");
}

static void enum_generate_raw_value_property(SwPrinter *p, EnumGen *e)
{
	Gen *g = e->g;
	bool closed = sw_enum_is_closed(e->ed);
	int needed_cases = e->main_sorted.n + (closed ? 0 : 1);
	bool use_multiple_switches = needed_cases > MAX_CASES_IN_SWITCH;
	sw_printf(p, "%svar rawValue: Int {", vis(g));
	sw_indent(p);
	if (use_multiple_switches) {
		for (int i = 0; i < e->main_sorted.n; i++) {
			const SwEnumValue *v = e->main_sorted.v[i];
			if (i % MAX_CASES_IN_SWITCH == 0) {
				if (i > 0) {
					sw_print(p, "default: break");
					sw_print(p, "}");
				}
				sw_print(p, "switch self {");
			}
			sw_printf(p, "case %s: return %d", sw_dotted_relative_name_enum_value(g->namer, v), (int)v->number);
		}
		if (!closed)
			sw_print(p, "case ." UNRECOGNIZED_CASE_NAME "(let i): return i");
		sw_print(p, "default: break\n"
			    "}\n"
			    "\n"
			    "// Can't get here, all the cases are listed in the above switches.\n"
			    "// See https://github.com/apple/swift-protobuf/issues/904 for more details.\n"
			    "fatalError()");
	} else {
		sw_print(p, "switch self {");
		for (int i = 0; i < e->main_sorted.n; i++) {
			const SwEnumValue *v = e->main_sorted.v[i];
			sw_printf(p, "case %s: return %d", sw_dotted_relative_name_enum_value(g->namer, v), (int)v->number);
		}
		if (!closed)
			sw_print(p, "case ." UNRECOGNIZED_CASE_NAME "(let i): return i");
		sw_print(p, "}");
	}
	sw_outdent(p);
	sw_print(p, "}");
}

static void enum_maybe_generate_case_iterable(SwPrinter *p, EnumGen *e)
{
	Gen *g = e->g;
	if (sw_enum_is_closed(e->ed))
		return;
	sw_print_blank(p);
	sw_print(p, "// The compiler won't synthesize support with the " UNRECOGNIZED_CASE_NAME " case.");
	sw_printf(p, "%sstatic let allCases: [%s] = [", vis(g), e->swift_full_name);
	sw_indent(p);
	for (int i = 0; i < e->alias_info->main_values.n; i++)
		sw_printf(p, "%s,", sw_dotted_relative_name_enum_value(g->namer, e->alias_info->main_values.v[i]));
	sw_outdent(p);
	sw_print(p, "]");
}

static void enum_generate_main_enum(SwPrinter *p, EnumGen *e)
{
	Gen *g = e->g;
	bool closed = sw_enum_is_closed(e->ed);
	Str comments = gen_comments_with_deprecation(g, SW_ENUM, e->ed);
	char *enum_decl = xfmt("%snonisolated enum %s: %s%sEnum, Swift.CaseIterable {", vis(g), e->swift_relative_name,
			       closed ? "Int, " : "", g->pfx);
	sw_print_blank(p);
	if (g->opts->enum_generation != ENUM_GEN_NONE && !closed)
		print_c(p, comments, "%s%s", enum_annotation(g->opts), enum_decl);
	else
		print_c(p, comments, "%s", enum_decl);
	sw_indent(p);
	if (!closed)
		sw_printf(p, "%stypealias RawValue = Int", vis(g));
	enum_generate_cases_or_aliases(p, e);
	sw_print_blank(p);
	sw_printf(p, "%sinit() {", vis(g));
	sw_indent(p);
	sw_printf(p, "self = %s", sw_dotted_relative_name_enum_value(g->namer, e->ed->values.v[0]));
	sw_outdent(p);
	sw_print(p, "}");
	if (!closed) {
		sw_print_blank(p);
		enum_generate_init_raw_value(p, e);
		sw_print_blank(p);
		enum_generate_raw_value_property(p, e);
	}
	enum_maybe_generate_case_iterable(p, e);
	sw_outdent(p);
	sw_print_blank(p);
	sw_print(p, "}");
}

static void enum_generate_proto_name_providing(SwPrinter *p, EnumGen *e)
{
	Gen *g = e->g;
	if (g->opts->experimental_hidden_names & HIDDEN_ENUM_VALUES) {
		sw_printf(p, "%sstatic let _protobuf_nameMap = %s_NameMap()", vis(g), g->pfx);
		return;
	}
	NameWriter w;
	name_writer_init(&w);
	for (int i = 0; i < e->main_sorted.n; i++) {
		const SwEnumValue *v = e->main_sorted.v[i];
		const Vec *aliases = e->alias_info->aliases[v->index];
		if (v->has_json) {
			if (aliases)
				nw_write_aliased(&w, v, aliases);
			nw_write_unique(&w, v->number, v->name, v->json);
		} else if (aliases) {
			nw_write_aliased(&w, v, aliases);
		} else {
			nw_write_same(&w, v->number, v->name);
		}
	}
	sw_printf(p, "%sstatic let _protobuf_nameMap = %s_NameMap(bytecode: %s)", vis(g), g->pfx, name_writer_literal(&w));
}

static void enum_generate_runtime_support(SwPrinter *p, EnumGen *e)
{
	sw_print_blank(p);
	sw_printf(p, "nonisolated extension %s: %s_ProtoNameProviding {", e->swift_full_name, e->g->pfx);
	sw_indent(p);
	enum_generate_proto_name_providing(p, e);
	sw_outdent(p);
	sw_print(p, "}");
}
