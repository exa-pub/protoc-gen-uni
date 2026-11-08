/* MIT License
 *
 * Copyright (c) 2022 SpiralScout
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE. */

/* Ported from protoc_plugins/protoc-gen-php-grpc/php/generate.go (roadrunner-server/grpc v6.0.0).
 * Generate serializes the CodeGeneratorResponse as proto.Marshal does (fields in number order). */

/* The generated files may not exceed this size in total: a method's input and output types repeat
 * the php_namespace of their file, so the output grows with the product of the namespace length
 * and the number of methods (upstream would print it all). */
#define OUTPUT_LIMIT ((size_t)256 << 20)
#define UNSUPPORTED "unsupported by protoc-gen-uni: "

static size_t output_total;

static void output_check(size_t file_bytes)
{
	if (file_bytes > OUTPUT_LIMIT - output_total)
		fail(UNSUPPORTED "generated files larger than 256 MiB in total");
}

#include "template.c"

static void generate(const Request *req, const PbMsg *file, const PbMsg *service, Buf *resp)
{
	Str name = filename(file, opt_str(service, SDP_NAME));
	Str content = body(req, file, service);
	output_check(name.n + content.n);
	output_total += name.n + content.n;
	Buf f = {0};
	pb_put_bytes(&f, CGRESP_FILE_NAME, name);
	pb_put_bytes(&f, CGRESP_FILE_CONTENT, content);
	pb_put_bytes(resp, CGRESP_FILE, buf_view(&f));
}

/* Generate generates needed service classes */
static void Generate(const Request *req, Buf *resp)
{
	pb_put_tag(resp, CGRESP_SUPPORTED_FEATURES, WT_VARINT);
	pb_put_varint(resp, 1); /* FEATURE_PROTO3_OPTIONAL */
	output_total = 0;
	for (int i = 0; i < pb_count(req->req, CGR_PROTO_FILE); i++) {
		const PbMsg *file = pb_msg_at(req->req, CGR_PROTO_FILE, i);
		for (int j = 0; j < pb_count(file, FDP_SERVICE); j++)
			generate(req, file, pb_msg_at(file, FDP_SERVICE, j), resp);
	}
}
