/* Copyright (c) ScalaPB authors (Nadav Samet and contributors).
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

/* ScalaPB 0.11.21 compiler-plugin (scalapb.h). Unity build: this file includes the other .c files
 * of the directory, which are static but for the functions declared in scalapb.h, defined at the
 * end of this file. */
#include "scalapb.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "registry.c"
#include "implicits.c"
#include "printer.c"
#include "protobuf_generator.c"
#include "parse_from.c"
#include "sealed_oneofs.c"
#include "grpc.c"
#include "validation.c"

/* GeneratorParams.fromString: params.split(",").map(_.trim).filter(_.nonEmpty). params is the
 * decoded Java string, which may contain U+0000 (String.trim removes it at the ends). */
void scalapb_parse_parameters(Str params)
{
	static const char *const names[] = {"java_conversions", "flat_package", "grpc", "single_line_to_proto_string",
					    "ascii_format_to_string", "no_lenses", "retain_source_code_info", "scala3_sources"};
	memset(&P, 0, sizeof P);
	P.lenses = true;
	Buf unrecognized = {0};
	for (size_t start = 0; start <= params.n;) {
		const char *comma = memchr(params.p + start, ',', params.n - start);
		size_t end = comma ? (size_t)(comma - params.p) : params.n;
		/* String.trim: characters <= ' ' */
		const char *s = params.p + start;
		size_t n = end - start;
		while (n && (unsigned char)s[n - 1] <= ' ')
			n--;
		while (n && (unsigned char)*s <= ' ')
			s++, n--;
		start = end + 1;
		if (!n)
			continue;
		int k = 0;
		while (k < 8 && !(strlen(names[k]) == n && !memcmp(names[k], s, n)))
			k++;
		switch (k) {
		case 0: P.java_conversions = true; break;
		case 1: P.flat_package = true; break;
		case 2: P.grpc = true; break;
		case 3: P.single_line_to_proto_string = true; break;
		case 4: P.ascii_format_to_string = true; break;
		case 5: P.lenses = false; break;
		case 6: P.retain_source_code_info = true; break;
		case 7: P.scala3_sources = true; break;
		default:
			buf_adds(&unrecognized, unrecognized.n ? ", " : "Unrecognized parameters: ");
			buf_add(&unrecognized, s, n);
		}
	}
	if (unrecognized.n) {
		const char *msg = buf_str(&unrecognized);
		fail_str((Str){msg, unrecognized.n});
	}
}

void scalapb_descriptor_implicits(const CodeGenRequest *request)
{
	/* reset the state of a previous request (fuzzing runs many requests in one process) */
	all_files = request->all_protos;
	memset(&sealed_cache, 0, sizeof sealed_cache);
	file_opts_built = false;
}

typedef struct {
	const CodeGenRequest *request;
	CodeGenResponse *response;
} HandleCtx;

static void handle(void *arg)
{
	HandleCtx *c = arg;
	scalapb_descriptor_implicits(c->request);
	validate_files();
	for (int i = 0; i < c->request->files_to_generate.n; i++) {
		JFile *f = c->request->files_to_generate.v[i];
		if (file_disable_output(f))
			continue;
		if (jpb_bool(file_opts(f), SPO_SINGLE_FILE, false))
			generate_single_scala_file(f, &c->response->files);
		else
			generate_multiple_scala_files(f, &c->response->files);
	}
}

/* ProtobufGenerator.handleCodeGeneratorRequest: a GeneratorException becomes the error of the
 * response with its message; other exceptions propagate (to CodeGenApp). */
void scalapb_code_generator_process(const CodeGenRequest *request, CodeGenResponse *response, void *arg)
{
	(void)arg;
	scalapb_parse_parameters(request->parameter);
	HandleCtx c = {request, response};
	generator_exception_thrown = false;
	if (try_run(handle, &c)) {
		Str msg = fail_message();
		if (generator_exception_thrown) {
			msg.p += strlen(GENERATOR_EXCEPTION);
			msg.n -= strlen(GENERATOR_EXCEPTION);
		}
		fail_str(msg);
	}
	response->supported_features = 1; /* FEATURE_PROTO3_OPTIONAL */
}

/* ---- the API for other plugins (scalapb.h) ---- */

ScalaName *scalapb_scala_name_child(const ScalaName *n, const char *name) { return sn_child(n, name); }
const char *scalapb_scala_name_name(const ScalaName *n) { return sn_name(n); }
const char *scalapb_scala_name_full_name(const ScalaName *n) { return sn_full_name(n); }
const char *scalapb_snake_case_to_camel_case(const char *name, bool upper_initial) { return snake_case_to_camel_case(name, upper_initial); }
const char *scalapb_base_name(const char *file_name) { return base_name(file_name); }
ScalaName *scalapb_file_scala_package(JFile *f) { return file_scala_package(f); }
const char *scalapb_service_name(JService *s) { return service_name(s); }
ScalaName *scalapb_service_grpc_descriptor(JService *s) { return sn_child(service_companion_object(s), "SERVICE"); }
const char *scalapb_method_name(JMethod *m) { return method_name(m); }
ScalaName *scalapb_method_grpc_descriptor(JMethod *m) { return method_grpc_descriptor(m); }
int scalapb_method_stream_type(const JMethod *m) { return method_stream_type(m); }
const char *scalapb_method_type_scala_type(JMsg *type) { return method_type_scala(type); }

void scalapb_printer_add(FunctionalPrinter *p, const char *s) { p_add(p, s); }
void scalapb_printer_indent(FunctionalPrinter *p) { p_indent(p); }
void scalapb_printer_outdent(FunctionalPrinter *p) { p_outdent(p); }
const char *scalapb_printer_result(FunctionalPrinter *p) { return p_result(p); }
