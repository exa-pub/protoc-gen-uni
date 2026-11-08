/* Copyright (c) 2014 - 2016 Apple Inc. and the project authors
 * Licensed under Apache License v2.0 with Runtime Library Exception
 *
 * See LICENSE.txt for license information:
 * https://github.com/apple/swift-protobuf/blob/main/LICENSE.txt */

/* apple/swift v1.39.0: a port of protoc-gen-swift from swift-protobuf 1.39.0
 * (https://github.com/apple/swift-protobuf, Sources/protoc-gen-swift) with buf's
 * extramoduleimports.patch (the ExtraModuleImports option of buf.build/apple/swift). The parts of
 * SwiftProtobufPluginLibrary it needs live in pkg/languages/swift/swift-protobuf.
 *
 * Unity build: this file includes the other .c files of the version directory, and everything
 * but the generator below is static. A new version is a copy of the directory with the exported
 * name changed (gen_apple_swift_v1_39_0), so versions never clash. */
#include "swift_protobuf.h"
#include "uni.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

typedef struct OneofGen OneofGen;
typedef struct MessageGen MessageGen;
typedef struct ExtensionSet ExtensionSet;

#include "options.c"

/* The state of one FileGenerator: options, the namer of the file and the errorString of
 * generateOutputFile (the last error set wins, like upstream). */
typedef struct {
	const Options *opts;
	SwFile *file;
	SwNamer *namer;
	const char *pfx; /* namer.swiftProtobufModulePrefix */
	const char *error;
} Gen;

#include "string_utils.c"
#include "bytecode.c"
#include "descriptor_ext.c"
#include "storage.c"
#include "field.c"
#include "oneof.c"
#include "enum.c"
#include "extension_set.c"
#include "message.c"
#include "file.c"
#include "plugin.c"

static const char *generate(PbMsg *req, Str raw, Buf *out)
{
	(void)raw;
	const char *err = sw_check_request(req, true);
	if (err)
		return xcat("protoc-gen-swift: Received an unparsable request from the compiler: ", err);
	storage_decision_reset();
	sw_generate_code(req, &swift_generator_plugin, out);
	return NULL;
}

const UniGenerator gen_apple_swift_v1_39_0 = {
	.name = "apple/swift",
	.version = "v1.39.0",
	.generate = generate,
};
