/* Port of protoc_plugin 25.1.0 lib/src/options.dart.
 *
 * Copyright (c) 2014, the Dart project authors. Please see the AUTHORS file for details. All
 * rights reserved. Use of this source code is governed by a BSD-style license that can be found
 * in the LICENSE file. */

/* parseGenerationOptions with genericOptionsParser and the grpc, generate_kythe_info and
 * disable_constructor_args parsers. Returns NULL and sets *error (errors joined with "\n") if an
 * option is wrong. */
static Options *parseGenerationOptions(Str parameter, const char **error)
{
	Options *o = xalloc(sizeof *o);
	Buf errors = {0};
	int nerrors = 0;
	Str trimmed = dart_trim(parameter);
	Str *options;
	int n = str_split_s(trimmed, ',', &options);
	for (int i = 0; i < n; i++) {
		Str option = dart_trim(options[i]);
		if (!option.n)
			continue;
		const char *opt = xstrndup(option.p, option.n);
		const char *details = NULL;
		Str *nameValue;
		int nv = str_split_s(option, '=', &nameValue);
		if (nv != 1 && nv != 2) {
			details = "Options should be a single token, or a name=value pair";
		} else {
			Str name = dart_trim(nameValue[0]);
			bool hasValue = nv > 1;
			if (str_eqc(name, "grpc")) {
				if (hasValue)
					details = "Invalid grpc option. No value expected.";
				else
					o->useGrpc = true;
			} else if (str_eqc(name, "generate_kythe_info")) {
				if (hasValue)
					details = "Invalid generate_kythe_info option. No value expected.";
				else
					o->generateMetadata = true;
			} else if (str_eqc(name, "disable_constructor_args")) {
				if (hasValue)
					details = "Invalid disable_constructor_args option. No value expected.";
				else
					o->disableConstructorArgs = true;
			} else {
				details = xfmt("Unknown option (%s).", xstrndup(name.p, name.n));
			}
		}
		if (details) {
			if (nerrors++)
				buf_addc(&errors, '\n');
			buf_fmt(&errors, "Error found trying to parse the option: %s.\n%s", opt, details);
		}
	}
	if (nerrors) {
		*error = buf_str(&errors);
		return NULL;
	}
	return o;
}
