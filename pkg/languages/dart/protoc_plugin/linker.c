/* Port of protoc_plugin 25.1.0 lib/src/linker.dart.
 *
 * Copyright (c) 2015, the Dart project authors. Please see the AUTHORS file for details. All
 * rights reserved. Use of this source code is governed by a BSD-style license that can be found
 * in the LICENSE file. */

static void ctx_registerFieldType(GenerationContext *ctx, Pc *type) { map_put(&ctx->typeRegistry, dottedName(type), type); }

static Pc *ctx_getFieldType(GenerationContext *ctx, const char *name) { return map_get(&ctx->typeRegistry, name); }

/* link: registers the targets of cross-references, then resolves files, then services. */
static void link_files(const Options *options, Vec *files)
{
	GenerationContext *ctx = xalloc(sizeof *ctx);
	ctx->options = options;
	for (int i = 0; i < files->n; i++) {
		FileGen *f = files->v[i];
		map_put(&ctx->files, pb_str(f->descriptor, FDP_NAME), f);
		for (int j = 0; j < f->messageGenerators.n; j++)
			msg_register(VEC_AT(f->messageGenerators, MsgGen, j), ctx);
		for (int j = 0; j < f->enumGenerators.n; j++)
			ctx_registerFieldType(ctx, &VEC_AT(f->enumGenerators, EnumGen, j)->pc);
	}
	for (int i = 0; i < files->n; i++)
		file_resolve(files->v[i], ctx);
	for (int i = 0; i < files->n; i++) {
		FileGen *f = files->v[i];
		for (int j = 0; j < f->serviceGenerators.n; j++)
			svc_resolve(VEC_AT(f->serviceGenerators, ServiceGen, j), ctx);
		for (int j = 0; j < f->grpcGenerators.n; j++)
			grpc_resolve(VEC_AT(f->grpcGenerators, ServiceGen, j), ctx);
	}
}
