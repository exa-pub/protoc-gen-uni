/* Copyright 2022 Marcus Griep and the protoc-gen-prost contributors (protoc-gen-prost-serde).
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
 * limitations under the License.
 */

/* community/neoeinstein-prost-serde v0.4.0: a port of protoc-gen-prost-serde 0.4.0
 * (https://github.com/neoeinstein/protoc-gen-prost, buf.build/community/neoeinstein-prost-serde),
 * which generates serde Serialize/Deserialize implementations for prost types with pbjson-build
 * 0.8.0 (https://github.com/influxdata/pbjson), ported in pkg/languages/rust/pbjson-build. The
 * protoc-gen-prost core shared with the other plugins of the family lives in
 * pkg/languages/rust/protoc-gen-prost.
 *
 * Unity build: this file includes the other .c files of the version directory, and everything
 * but the generator below is static. */
#include "pbjson_build.h"
#include "prost.h"
#include "uni.h"

#include <stdint.h>
#include <string.h>

#include "unsupported.c"
#include "plugin.c"

/* main.rs: the response, or the error (upstream panics abort the process instead). */
static const char *generate(PbMsg *req, Str raw, Buf *out)
{
	Run run = {0};
	run.req = req;
	run.raw = raw;
	if (try_run(execute, &run)) {
		prost_write_error(out, fail_message());
		return NULL;
	}
	if (run.error) {
		prost_write_error(out, str_c(run.error));
		return NULL;
	}
	prost_write_response(out, &run.files);
	return NULL;
}

const UniGenerator gen_community_neoeinstein_prost_serde_v0_4_0 = {
	.name = "community/neoeinstein-prost-serde",
	.version = "v0.4.0",
	.generate = generate,
};
