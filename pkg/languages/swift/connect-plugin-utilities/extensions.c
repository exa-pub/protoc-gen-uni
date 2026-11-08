/* Copyright 2022-2025 The Connect Authors
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

/* Port of Plugins/ConnectPluginUtilities/MethodDescriptor+Extensions.swift and
 * ServiceDescriptor+Extensions.swift (connect-swift 1.2.3). */
#include "connect_plugin_utilities.h"

/* ---- MethodDescriptor+Extensions.swift ---- */

char *cpu_method_path(const SwMethod *m) { return xcat("/", cpu_service_path(m->service), "/", m->name); }

const char *cpu_method_name(const SwMethod *m, const CpuOptions *o)
{
	return o->keep_method_casing ? m->name : sw_to_lower_camel(m->name);
}

/* Note that the method name is escaped to avoid using Swift keywords. */
char *cpu_callback_signature(const SwMethod *m, SwNamer *namer, bool include_defaults, const CpuOptions *o)
{
	const char *method_name = cpu_method_name(m, o);
	const char *input_name = sw_full_name_message(namer, m->input_type);
	const char *output_name = sw_full_name_message(namer, m->output_type);
	const char *defaults = include_defaults ? " = [:]" : "";
	const char *stream = NULL;
	if (m->client_streaming && m->server_streaming)
		stream = "BidirectionalStreamInterface";
	else if (m->server_streaming)
		stream = "ServerOnlyStreamInterface";
	else if (m->client_streaming)
		stream = "ClientOnlyStreamInterface";
	if (stream)
		return xfmt("func `%s`(headers: Connect.Headers%s, onResult: @escaping @Sendable "
			    "(Connect.StreamResult<%s>) -> Void) -> any Connect.%s<%s>",
			    method_name, defaults, output_name, stream, input_name);
	return xfmt("func `%s`(request: %s, headers: Connect.Headers%s, completion: @escaping @Sendable "
		    "(ResponseMessage<%s>) -> Void) -> Connect.Cancelable",
		    method_name, input_name, defaults, output_name);
}

char *cpu_async_await_signature(const SwMethod *m, SwNamer *namer, bool include_defaults, const CpuOptions *o)
{
	const char *method_name = cpu_method_name(m, o);
	const char *input_name = sw_full_name_message(namer, m->input_type);
	const char *output_name = sw_full_name_message(namer, m->output_type);
	const char *defaults = include_defaults ? " = [:]" : "";
	const char *stream = NULL;
	if (m->client_streaming && m->server_streaming)
		stream = "BidirectionalAsyncStreamInterface";
	else if (m->server_streaming)
		stream = "ServerOnlyAsyncStreamInterface";
	else if (m->client_streaming)
		stream = "ClientOnlyAsyncStreamInterface";
	if (stream)
		return xfmt("func `%s`(headers: Connect.Headers%s) -> any Connect.%s<%s, %s>", method_name, defaults,
			    stream, input_name, output_name);
	return xfmt("func `%s`(request: %s, headers: Connect.Headers%s) async -> ResponseMessage<%s>", method_name,
		    input_name, defaults, output_name);
}

/* ---- ServiceDescriptor+Extensions.swift ---- */

char *cpu_service_path(const SwService *s)
{
	if (!*s->file->package)
		return xstrdup(s->name);
	return xcat(s->file->package, ".", s->name);
}

char *cpu_implementation_name(const SwService *s, SwNamer *namer)
{
	char *upper_camel_name = xcat(sw_to_upper_camel(s->name), "Client");
	if (!*s->file->package)
		return upper_camel_name;
	return xcat(sw_namer_type_prefix(namer, s->file), upper_camel_name);
}

char *cpu_protocol_name(const SwService *s, SwNamer *namer) { return xcat(cpu_implementation_name(s, namer), "Interface"); }
