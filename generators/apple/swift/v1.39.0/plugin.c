/* Copyright (c) 2014 - 2016 Apple Inc. and the project authors
 * Licensed under Apache License v2.0 with Runtime Library Exception
 *
 * See LICENSE.txt for license information:
 * https://github.com/apple/swift-protobuf/blob/main/LICENSE.txt */

/* Port of Sources/protoc-gen-swift/SwiftGeneratorPlugin.swift. */

static void plugin_generate(SwFile **files, int n_files, Str parameter, SwOutputs *outputs)
{
	Options *o = parse_options(parameter);
	for (int i = 0; i < n_files; i++) {
		SwFile *file = files[i];
		Gen g = {o, file, sw_namer(file, o->proto_to_module_mappings), NULL, NULL};
		g.pfx = sw_namer_swift_protobuf_module_prefix(g.namer);
		SwPrinter p;
		sw_printer_init(&p, true);
		generate_output_file(&g, &p);
		if (g.error)
			fail("%s", n_files > 1 ? xcat(file->name, ": ", g.error) : g.error);
		sw_outputs_add(outputs, output_filename(o, file), buf_view(&p.content));
	}
}

static const SwCodeGenerator swift_generator_plugin = {
	.supported_features = 3, /* FEATURE_PROTO3_OPTIONAL | FEATURE_SUPPORTS_EDITIONS */
	.minimum_edition = SW_EDITION_PROTO2,
	.maximum_edition = SW_EDITION_2026,
	.generate = plugin_generate,
};
