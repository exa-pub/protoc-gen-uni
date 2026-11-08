/* Copyright (c) 2016 Timo Stamm
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License. */

/* Ported from packages/plugin/src/options.ts and framework/create-option-parser.ts. */

typedef struct {
	const char *key, *description, *excludes; /* excludes: ","-separated */
} OptionSpec;

static const OptionSpec option_specs[] = {
	{"long_type_string",
	 "Sets jstype = JS_STRING for message fields with 64 bit integral values. \nThe default behaviour is to use native `bigint`. \nOnly applies to fields that do *not* use the option `jstype`.",
	 "long_type_number,long_type_bigint"},
	{"long_type_number",
	 "Sets jstype = JS_NUMBER for message fields with 64 bit integral values. \nThe default behaviour is to use native `bigint`. \nOnly applies to fields that do *not* use the option `jstype`.",
	 "long_type_string,long_type_bigint"},
	{"long_type_bigint",
	 "Sets jstype = JS_NORMAL for message fields with 64 bit integral values. \nThis is the default behavior. \nOnly applies to fields that do *not* use the option `jstype`.",
	 "long_type_string,long_type_number"},
	{"generate_dependencies",
	 "By default, only the PROTO_FILES passed as input to protoc are generated, \nnot the files they import (with the exception of well-known types, which are \nalways generated when imported). \nSet this option to generate code for dependencies too.",
	 ""},
	{"force_exclude_all_options",
	 "By default, custom options are included in the metadata and can be blacklisted \nwith our option (ts.exclude_options). Set this option if you are certain you \ndo not want to include any options at all.",
	 ""},
	{"keep_enum_prefix",
	 "By default, if all enum values share a prefix that corresponds with the enum's \nname, the prefix is dropped from the value names. Set this option to disable \nthis behavior.",
	 ""},
	{"use_proto_field_name",
	 "By default interface fields use lowerCamelCase names by transforming proto field\nnames to follow common style convention for TypeScript. Set this option to preserve\noriginal proto field names in generated interfaces.",
	 ""},
	{"ts_nocheck",
	 "Generate a @ts-nocheck annotation at the top of each file. This will become the \ndefault behaviour in the next major release.",
	 "disable_ts_nocheck"},
	{"disable_ts_nocheck",
	 "Do not generate a @ts-nocheck annotation at the top of each file. Since this is \nthe default behaviour, this option has no effect.",
	 "ts_nocheck"},
	{"eslint_disable",
	 "Generate a eslint-disable comment at the top of each file. This will become the \ndefault behaviour in the next major release.",
	 "no_eslint_disable"},
	{"no_eslint_disable",
	 "Do not generate a eslint-disable comment at the top of each file. Since this is \nthe default behaviour, this option has no effect.",
	 "eslint_disable"},
	{"add_pb_suffix",
	 "Adds the suffix `_pb` to the names of all generated files. This will become the \ndefault behaviour in the next major release.",
	 ""},
	{"output_typescript",
	 "Output TypeScript files. This is the default behavior.",
	 "output_javascript,output_javascript_es2015,output_javascript_es2016,output_javascript_es2017,output_javascript_es2018,output_javascript_es2019,output_javascript_es2020"},
	{"output_javascript",
	 "Output JavaScript for the currently recommended target ES2020. The target may \nchange with a major release of protobuf-ts. \nAlong with JavaScript files, this always outputs TypeScript declaration files.",
	 "output_typescript,output_javascript_es2015,output_javascript_es2016,output_javascript_es2017,output_javascript_es2018,output_javascript_es2019,output_javascript_es2020"},
	{"output_javascript_es2015",
	 "Output JavaScript for the ES2015 target.",
	 "output_typescript,output_javascript_es2016,output_javascript_es2017,output_javascript_es2018,output_javascript_es2019,output_javascript_es2020"},
	{"output_javascript_es2016",
	 "Output JavaScript for the ES2016 target.",
	 "output_typescript,output_javascript_es2015,output_javascript_es2017,output_javascript_es2018,output_javascript_es2019,output_javascript_es2020"},
	{"output_javascript_es2017",
	 "Output JavaScript for the ES2017 target.",
	 "output_typescript,output_javascript_es2015,output_javascript_es2016,output_javascript_es2018,output_javascript_es2019,output_javascript_es2020"},
	{"output_javascript_es2018",
	 "Output JavaScript for the ES2018 target.",
	 "output_typescript,output_javascript_es2015,output_javascript_es2016,output_javascript_es2017,output_javascript_es2019,output_javascript_es2020"},
	{"output_javascript_es2019",
	 "Output JavaScript for the ES2019 target.",
	 "output_typescript,output_javascript_es2015,output_javascript_es2016,output_javascript_es2017,output_javascript_es2018,output_javascript_es2020"},
	{"output_javascript_es2020",
	 "Output JavaScript for the ES2020 target.",
	 "output_typescript,output_javascript_es2015,output_javascript_es2016,output_javascript_es2017,output_javascript_es2018,output_javascript_es2019"},
	{"output_legacy_commonjs",
	 "Use CommonJS instead of the default ECMAScript module system.",
	 "output_typescript"},
	{"client_none",
	 "Do not generate rpc clients. \nOnly applies to services that do *not* use the option `ts.client`. \nIf you do not want rpc clients at all, use `force_client_none`.",
	 "client_generic,client_grpc1"},
	{"client_generic",
	 "Only applies to services that do *not* use the option `ts.client`. \nSince GENERIC_CLIENT is the default, this option has no effect.",
	 "client_none,client_grpc1,force_client_none,force_disable_services"},
	{"client_grpc1",
	 "Generate a client using @grpc/grpc-js (major version 1). \nOnly applies to services that do *not* use the option `ts.client`.",
	 "client_none,client_generic,force_client_none,force_disable_services"},
	{"force_client_none",
	 "Do not generate rpc clients, ignore options in proto files.",
	 "client_none,client_generic,client_grpc1"},
	{"server_none",
	 "Do not generate rpc servers. \nThis is the default behaviour, but only applies to services that do \n*not* use the option `ts.server`. \nIf you do not want servers at all, use `force_server_none`.",
	 "server_grpc1"},
	{"server_generic",
	 "Generate a generic server interface. Adapters are used to serve the service, \nfor example @protobuf-ts/grpc-backend for gRPC. \nNote that this is an experimental feature and may change with a minor release. \nOnly applies to services that do *not* use the option `ts.server`.",
	 "server_none,force_server_none,force_disable_services"},
	{"server_grpc1",
	 "Generate a server interface and definition for use with @grpc/grpc-js \n(major version 1). \nOnly applies to services that do *not* use the option `ts.server`.",
	 "server_none,force_server_none,force_disable_services"},
	{"force_server_none",
	 "Do not generate rpc servers, ignore options in proto files.",
	 ""},
	{"force_disable_services",
	 "Do not generate anything for service definitions, and ignore options in proto \nfiles. This is the same as setting both options `force_server_none` and \n`force_client_none`, but also stops generating service metadata.",
	 "client_generic,client_grpc1,server_generic,server_grpc1"},
	{"optimize_speed",
	 "Sets optimize_for = SPEED for proto files that have no file option \n'option optimize_for'. Since SPEED is the default, this option has no effect.",
	 "force_optimize_speed"},
	{"optimize_code_size",
	 "Sets optimize_for = CODE_SIZE for proto files that have no file option \n'option optimize_for'.",
	 "force_optimize_speed"},
	{"force_optimize_code_size",
	 "Forces optimize_for = CODE_SIZE for all proto files, ignore file options.",
	 "optimize_code_size,force_optimize_speed"},
	{"force_optimize_speed",
	 "Forces optimize_for = SPEED for all proto files, ignore file options.",
	 "optimize_code_size,force_optimize_code_size"},
};
enum { N_OPTIONS = (int)(sizeof option_specs / sizeof *option_specs) };

/* throwOptionError: a ParameterError with the list of options. */
NORETURN COLD static void throw_option_error(const char *error)
{
	Buf b = {0};
	buf_adds(&b, "ParameterError\n\n");
	buf_adds(&b, error);
	buf_adds(&b, "\n\nAvailable options:\n\n");
	for (int i = 0; i < N_OPTIONS; i++) {
		buf_fmt(&b, "- \"%s\"\n", option_specs[i].key);
		Vec lines = str_split(option_specs[i].description, '\n');
		for (int j = 0; j < lines.n; j++)
			buf_fmt(&b, "  %s\n", (const char *)lines.v[j]);
		buf_addc(&b, '\n');
	}
	fail_str(buf_view(&b));
}

static int option_index(Str key)
{
	for (int i = 0; i < N_OPTIONS; i++)
		if (str_eqc(key, option_specs[i].key))
			return i;
	return -1;
}

/* createOptionParser(...)(parameter): the flags that are set. */
COLD static void parse_parameter(Str parameter, bool *set)
{
	if (parameter.n) {
		Str *raw;
		int n = str_split_s(parameter, ',', &raw);
		for (int i = 0; i < n; i++) {
			const char *eq = memchr(raw[i].p, '=', raw[i].n);
			Str key = eq ? (Str){raw[i].p, (size_t)(eq - raw[i].p)} : raw[i];
			Str value = eq ? (Str){eq + 1, raw[i].n - (size_t)(eq - raw[i].p) - 1} : (Str){"", 0};
			int k = option_index(key);
			if (k < 0)
				throw_option_error(xcat("Option \"", xstrndup(key.p, key.n), "\" not recognized."));
			if (set[k])
				throw_option_error(xcat("Option \"", xstrndup(key.p, key.n), "\" cannot be given more than once."));
			set[k] = true;
			if (value.n)
				throw_option_error(xcat("Option \"", option_specs[k].key, "\" does not take a value."));
		}
	}
	/* validateOptions */
	for (int i = 0; i < N_OPTIONS; i++) {
		if (!set[i])
			continue;
		Vec ex = str_split(option_specs[i].excludes, ',');
		Vec denied = {0};
		for (int j = 0; j < ex.n; j++) {
			int k = option_index(str_c(ex.v[j]));
			if (k >= 0 && set[k])
				vec_push(&denied, ex.v[j]);
		}
		if (denied.n)
			throw_option_error(xcat("If option \"", option_specs[i].key, "\" is set, option \"",
						str_join(&denied, "\", \""), "\" cannot be set."));
	}
}

enum { OPTIMIZE_SPEED = 1, OPTIMIZE_CODE_SIZE = 2 };
enum { NO_CLIENT = 0, GENERIC_CLIENT = 1, GRPC1_CLIENT = 4 };
enum { NO_SERVER = 0, GENERIC_SERVER = 1, GRPC1_SERVER = 2 };

/* The internal settings for the file generation (interface Options). */
typedef struct {
	bool generate_dependencies;
	const char *plugin_credit;
	int normal_long_type;
	int normal_optimize_mode, forced_optimize_mode; /* -1: undefined */
	int normal_server_style, forced_server_style;
	int normal_client_style, forced_client_style;
	const char *synthesize_enum_zero_value;
	const char *oneof_kind_discriminator;
	const char *runtime_rpc_import_path, *runtime_import_path;
	bool force_exclude_all_options, keep_enum_prefix, use_proto_field_name, ts_nocheck, es_lint_disable;
	const char *transpile_target; /* the output_javascript* option, NULL: TypeScript */
	bool force_disable_services, add_pb_suffix;
} Options;

static bool opt_set(const bool *set, const char *key) { return set[option_index(str_c(key))]; }

COLD static Options *parse_options(Str parameter, const char *plugin_credit)
{
	bool set[N_OPTIONS] = {0};
	parse_parameter(parameter, set);
	Options *o = xalloc(sizeof *o);
	o->normal_long_type = RT_LONG_BIGINT;
	o->normal_optimize_mode = OPTIMIZE_SPEED;
	o->forced_optimize_mode = -1;
	o->normal_client_style = GENERIC_CLIENT;
	o->forced_client_style = -1;
	o->normal_server_style = NO_SERVER;
	o->forced_server_style = -1;
	o->synthesize_enum_zero_value = "UNSPECIFIED$";
	o->oneof_kind_discriminator = "oneofKind";
	o->runtime_rpc_import_path = "@protobuf-ts/runtime-rpc";
	o->runtime_import_path = "@protobuf-ts/runtime";
	o->plugin_credit = plugin_credit;
	o->generate_dependencies = opt_set(set, "generate_dependencies");
	o->force_exclude_all_options = opt_set(set, "force_exclude_all_options");
	o->keep_enum_prefix = opt_set(set, "keep_enum_prefix");
	o->use_proto_field_name = opt_set(set, "use_proto_field_name");
	o->ts_nocheck = opt_set(set, "ts_nocheck");
	o->es_lint_disable = opt_set(set, "eslint_disable");
	if (opt_set(set, "long_type_string"))
		o->normal_long_type = RT_LONG_STRING;
	if (opt_set(set, "long_type_number"))
		o->normal_long_type = RT_LONG_NUMBER;
	if (opt_set(set, "optimize_code_size"))
		o->normal_optimize_mode = OPTIMIZE_CODE_SIZE;
	if (opt_set(set, "force_optimize_speed"))
		o->forced_optimize_mode = OPTIMIZE_SPEED;
	if (opt_set(set, "force_optimize_code_size"))
		o->forced_optimize_mode = OPTIMIZE_CODE_SIZE;
	if (opt_set(set, "client_none"))
		o->normal_client_style = NO_CLIENT;
	if (opt_set(set, "client_grpc1"))
		o->normal_client_style = GRPC1_CLIENT;
	if (opt_set(set, "force_client_none"))
		o->forced_client_style = NO_CLIENT;
	if (opt_set(set, "server_generic"))
		o->normal_server_style = GENERIC_SERVER;
	if (opt_set(set, "server_grpc1"))
		o->normal_server_style = GRPC1_SERVER;
	if (opt_set(set, "force_server_none"))
		o->forced_server_style = NO_SERVER;
	o->add_pb_suffix = opt_set(set, "add_pb_suffix");
	o->force_disable_services = opt_set(set, "force_disable_services");
	static const char *const js[] = {"output_javascript", "output_javascript_es2015", "output_javascript_es2016",
					 "output_javascript_es2017", "output_javascript_es2018",
					 "output_javascript_es2019", "output_javascript_es2020"};
	for (int i = 0; i < 7; i++)
		if (opt_set(set, js[i]))
			o->transpile_target = js[i];
	return o;
}

/* getOptimizeMode: FileOptions.optimize_for reads as its default SPEED when the file has options
 * without it, so the normal mode only applies to files without options. */
static int get_optimize_mode(const Options *o, const DescFile *file)
{
	if (o->forced_optimize_mode >= 0)
		return o->forced_optimize_mode;
	const PbMsg *fo = pb_msg(file->h.proto, FDP_OPTIONS);
	if (fo)
		return (int)pb_int(fo, 9, OPTIMIZE_SPEED);
	return o->normal_optimize_mode;
}
