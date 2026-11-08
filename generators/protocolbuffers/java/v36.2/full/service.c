// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* Port of src/google/protobuf/compiler/java/full/service.{h,cc} (protobuf v36.2). */

enum { REQUEST, RESPONSE };
enum { IS_ABSTRACT, IS_CONCRETE };
static void ImmutableServiceGenerator_Generate(JvServiceGen *g, LpPrinter *printer);
static void ImmutableServiceGenerator_GenerateGetDescriptorForType(JvServiceGen *g, LpPrinter *printer);
static void ImmutableServiceGenerator_GenerateInterface(JvServiceGen *g, LpPrinter *printer);
static void ImmutableServiceGenerator_GenerateNewReflectiveServiceMethod(JvServiceGen *g, LpPrinter *printer);
static void ImmutableServiceGenerator_GenerateNewReflectiveBlockingServiceMethod(JvServiceGen *g, LpPrinter *printer);
static void ImmutableServiceGenerator_GenerateAbstractMethods(JvServiceGen *g, LpPrinter *printer);
static char *ImmutableServiceGenerator_GetOutput(JvServiceGen *g, const LpMethod *method);
static void ImmutableServiceGenerator_GenerateCallMethod(JvServiceGen *g, LpPrinter *printer);
static void ImmutableServiceGenerator_GenerateCallBlockingMethod(JvServiceGen *g, LpPrinter *printer);
static void ImmutableServiceGenerator_GenerateGetPrototype(JvServiceGen *g, int which, LpPrinter *printer);
static void ImmutableServiceGenerator_GenerateStub(JvServiceGen *g, LpPrinter *printer);
static void ImmutableServiceGenerator_GenerateBlockingStub(JvServiceGen *g, LpPrinter *printer);
static void ImmutableServiceGenerator_GenerateMethodSignature(JvServiceGen *g, LpPrinter *printer,
							      const LpMethod *method, int is_abstract);
static void ImmutableServiceGenerator_GenerateBlockingMethodSignature(JvServiceGen *g, LpPrinter *printer,
								      const LpMethod *method);

/* ImmutableServiceGenerator::ImmutableServiceGenerator: generator_factory.c */

static void ImmutableServiceGenerator_Generate(JvServiceGen *g, LpPrinter *printer)
{
	bool is_own_file = jv_is_own_file_(g->descriptor, /* immutable = */ true);
	jv_write_service_doc_comment(printer, g->descriptor, &g->context->options);
	jv_maybe_print_generated_annotation_(g->context, printer, g->descriptor,
					     /* immutable = */ true);
	if (!JV_IS_OSS) {
		lp_print(printer, "@com.google.protobuf.Internal.ProtoNonnullApi\n", NULL);
	}

	lp_print(printer,
		 "public $static$ abstract class $classname$\n"
		 "    implements com.google.protobuf.Service {\n",
		 "static", is_own_file ? "" : "static", "classname", g->descriptor->name, NULL);
	lp_indent(printer);

	lp_print(printer, "protected $classname$() {}\n\n", "classname", g->descriptor->name, NULL);

	ImmutableServiceGenerator_GenerateInterface(g, printer);

	ImmutableServiceGenerator_GenerateNewReflectiveServiceMethod(g, printer);
	ImmutableServiceGenerator_GenerateNewReflectiveBlockingServiceMethod(g, printer);

	ImmutableServiceGenerator_GenerateAbstractMethods(g, printer);

	// Generate getDescriptor() and getDescriptorForType().
	lp_print(printer,
		 "public static final\n"
		 "    com.google.protobuf.Descriptors.ServiceDescriptor\n"
		 "    getDescriptor() {\n"
		 "  return $file$.getDescriptor().getService($index$);\n"
		 "}\n",
		 "file", jv_immutable_class_name(g->descriptor->file), "index", xcat(jv_i(g->descriptor->index)), NULL);
	ImmutableServiceGenerator_GenerateGetDescriptorForType(g, printer);

	// Generate more stuff.
	ImmutableServiceGenerator_GenerateCallMethod(g, printer);
	ImmutableServiceGenerator_GenerateGetPrototype(g, REQUEST, printer);
	ImmutableServiceGenerator_GenerateGetPrototype(g, RESPONSE, printer);
	ImmutableServiceGenerator_GenerateStub(g, printer);
	ImmutableServiceGenerator_GenerateBlockingStub(g, printer);

	// Add an insertion point.
	lp_print(printer,
		 "\n"
		 "// @@protoc_insertion_point(class_scope:$full_name$)\n",
		 "full_name", g->descriptor->full_name, NULL);

	lp_outdent(printer);
	lp_print(printer, "}\n\n", NULL);
}

static void ImmutableServiceGenerator_GenerateGetDescriptorForType(JvServiceGen *g, LpPrinter *printer)
{
	lp_print(printer,
		 "public final com.google.protobuf.Descriptors.ServiceDescriptor\n"
		 "    getDescriptorForType() {\n"
		 "  return getDescriptor();\n"
		 "}\n",
		 NULL);
}

static void ImmutableServiceGenerator_GenerateInterface(JvServiceGen *g, LpPrinter *printer)
{
	lp_print(printer, "public interface Interface {\n", NULL);
	lp_indent(printer);
	ImmutableServiceGenerator_GenerateAbstractMethods(g, printer);
	lp_outdent(printer);
	lp_print(printer, "}\n\n", NULL);
}

static void ImmutableServiceGenerator_GenerateNewReflectiveServiceMethod(JvServiceGen *g, LpPrinter *printer)
{
	lp_print(printer,
		 "public static com.google.protobuf.Service newReflectiveService(\n"
		 "    final Interface impl) {\n"
		 "  return new $classname$() {\n",
		 "classname", g->descriptor->name, NULL);
	lp_indent(printer);
	lp_indent(printer);

	for (int i = 0; i < g->descriptor->n_methods; i++) {
		const LpMethod *method = (&g->descriptor->methods[i]);
		lp_print(printer, "@java.lang.Override\n", NULL);
		ImmutableServiceGenerator_GenerateMethodSignature(g, printer, method, IS_CONCRETE);
		lp_print(printer,
			 " {\n"
			 "  impl.$method$(controller, request, done);\n"
			 "}\n\n",
			 "method", jv_underscores_to_camel_case_method(method), NULL);
	}

	lp_outdent(printer);
	lp_print(printer, "};\n", NULL);
	lp_outdent(printer);
	lp_print(printer, "}\n\n", NULL);
}

static void ImmutableServiceGenerator_GenerateNewReflectiveBlockingServiceMethod(JvServiceGen *g, LpPrinter *printer)
{
	lp_print(printer,
		 "public static com.google.protobuf.BlockingService\n"
		 "    newReflectiveBlockingService(final BlockingInterface impl) {\n"
		 "  return new com.google.protobuf.BlockingService() {\n",
		 NULL);
	lp_indent(printer);
	lp_indent(printer);

	ImmutableServiceGenerator_GenerateGetDescriptorForType(g, printer);

	ImmutableServiceGenerator_GenerateCallBlockingMethod(g, printer);
	ImmutableServiceGenerator_GenerateGetPrototype(g, REQUEST, printer);
	ImmutableServiceGenerator_GenerateGetPrototype(g, RESPONSE, printer);

	lp_outdent(printer);
	lp_print(printer, "};\n", NULL);
	lp_outdent(printer);
	lp_print(printer, "}\n\n", NULL);
}

static void ImmutableServiceGenerator_GenerateAbstractMethods(JvServiceGen *g, LpPrinter *printer)
{
	for (int i = 0; i < g->descriptor->n_methods; i++) {
		const LpMethod *method = (&g->descriptor->methods[i]);
		jv_write_method_doc_comment(printer, method, &g->context->options);
		ImmutableServiceGenerator_GenerateMethodSignature(g, printer, method, IS_ABSTRACT);
		lp_print(printer, ";\n\n", NULL);
	}
}

static char *ImmutableServiceGenerator_GetOutput(JvServiceGen *g, const LpMethod *method)
{
	return jv_immutable_class_name(method->output_type);
}

static void ImmutableServiceGenerator_GenerateCallMethod(JvServiceGen *g, LpPrinter *printer)
{
	lp_print(printer,
		 "\n"
		 "public final void callMethod(\n"
		 "    com.google.protobuf.Descriptors.MethodDescriptor method,\n"
		 "    com.google.protobuf.RpcController controller,\n"
		 "    com.google.protobuf.Message request,\n"
		 "    com.google.protobuf.RpcCallback<\n"
		 "      com.google.protobuf.Message> done) {\n"
		 "  if (method.getService() != getDescriptor()) {\n"
		 "    throw new java.lang.IllegalArgumentException(\n"
		 "      \"Service.callMethod() given method descriptor for wrong \" +\n"
		 "      \"service type.\");\n"
		 "  }\n"
		 "  switch(method.getIndex()) {\n",
		 NULL);
	lp_indent(printer);
	lp_indent(printer);

	for (int i = 0; i < g->descriptor->n_methods; i++) {
		const LpMethod *method = (&g->descriptor->methods[i]);
		LpVars vars = { 0 };
		lp_vars_set(&vars, "index", xcat(jv_i(i)));
		lp_vars_set(&vars, "method", jv_underscores_to_camel_case_method(method));
		lp_vars_set(&vars, "input", jv_immutable_class_name(method->input_type));
		lp_vars_set(&vars, "output", ImmutableServiceGenerator_GetOutput(g, method));
		lp_print_vars(printer, &vars,
			      "case $index$:\n"
			      "  this.$method$(controller, ($input$)request,\n"
			      "    com.google.protobuf.RpcUtil.<$output$>specializeCallback(\n"
			      "      done));\n"
			      "  return;\n");
	}

	lp_print(printer,
		 "default:\n"
		 "  throw new java.lang.AssertionError(\"Can't get here.\");\n",
		 NULL);

	lp_outdent(printer);
	lp_outdent(printer);

	lp_print(printer,
		 "  }\n"
		 "}\n"
		 "\n",
		 NULL);
}

static void ImmutableServiceGenerator_GenerateCallBlockingMethod(JvServiceGen *g, LpPrinter *printer)
{
	lp_print(printer,
		 "\n"
		 "public final com.google.protobuf.Message callBlockingMethod(\n"
		 "    com.google.protobuf.Descriptors.MethodDescriptor method,\n"
		 "    com.google.protobuf.RpcController controller,\n"
		 "    com.google.protobuf.Message request)\n"
		 "    throws com.google.protobuf.ServiceException {\n"
		 "  if (method.getService() != getDescriptor()) {\n"
		 "    throw new java.lang.IllegalArgumentException(\n"
		 "      \"Service.callBlockingMethod() given method descriptor for \" +\n"
		 "      \"wrong service type.\");\n"
		 "  }\n"
		 "  switch(method.getIndex()) {\n",
		 NULL);
	lp_indent(printer);
	lp_indent(printer);

	for (int i = 0; i < g->descriptor->n_methods; i++) {
		const LpMethod *method = (&g->descriptor->methods[i]);
		LpVars vars = { 0 };
		lp_vars_set(&vars, "index", xcat(jv_i(i)));
		lp_vars_set(&vars, "method", jv_underscores_to_camel_case_method(method));
		lp_vars_set(&vars, "input", jv_immutable_class_name(method->input_type));
		lp_vars_set(&vars, "output", ImmutableServiceGenerator_GetOutput(g, method));
		lp_print_vars(printer, &vars,
			      "case $index$:\n"
			      "  return impl.$method$(controller, ($input$)request);\n");
	}

	lp_print(printer,
		 "default:\n"
		 "  throw new java.lang.AssertionError(\"Can't get here.\");\n",
		 NULL);

	lp_outdent(printer);
	lp_outdent(printer);

	lp_print(printer,
		 "  }\n"
		 "}\n"
		 "\n",
		 NULL);
}

static void ImmutableServiceGenerator_GenerateGetPrototype(JvServiceGen *g, int which, LpPrinter *printer)
{
	/*
   * TODO: The exception message says "Service.foo" when it may be
   * "BlockingService.foo."  Consider fixing.
   */
	lp_print(printer,
		 "public final com.google.protobuf.Message\n"
		 "    get$request_or_response$Prototype(\n"
		 "    com.google.protobuf.Descriptors.MethodDescriptor method) {\n"
		 "  if (method.getService() != getDescriptor()) {\n"
		 "    throw new java.lang.IllegalArgumentException(\n"
		 "      \"Service.get$request_or_response$Prototype() given method \" +\n"
		 "      \"descriptor for wrong service type.\");\n"
		 "  }\n"
		 "  switch(method.getIndex()) {\n",
		 "request_or_response", (which == REQUEST) ? "Request" : "Response", NULL);
	lp_indent(printer);
	lp_indent(printer);

	for (int i = 0; i < g->descriptor->n_methods; i++) {
		const LpMethod *method = (&g->descriptor->methods[i]);
		LpVars vars = { 0 };
		lp_vars_set(&vars, "index", xcat(jv_i(i)));
		lp_vars_set(&vars, "type",
			    (which == REQUEST) ? jv_immutable_class_name(method->input_type)
					       : ImmutableServiceGenerator_GetOutput(g, method));
		lp_print_vars(printer, &vars,
			      "case $index$:\n"
			      "  return $type$.getDefaultInstance();\n");
	}

	lp_print(printer,
		 "default:\n"
		 "  throw new java.lang.AssertionError(\"Can't get here.\");\n",
		 NULL);

	lp_outdent(printer);
	lp_outdent(printer);

	lp_print(printer,
		 "  }\n"
		 "}\n"
		 "\n",
		 NULL);
}

static void ImmutableServiceGenerator_GenerateStub(JvServiceGen *g, LpPrinter *printer)
{
	lp_print(printer,
		 "public static Stub newStub(\n"
		 "    com.google.protobuf.RpcChannel channel) {\n"
		 "  return new Stub(channel);\n"
		 "}\n"
		 "\n"
		 "public static final class Stub extends $classname$ implements Interface "
		 "{"
		 "\n",
		 "classname", jv_immutable_class_name(g->descriptor), NULL);
	lp_indent(printer);

	lp_print(printer,
		 "private Stub(com.google.protobuf.RpcChannel channel) {\n"
		 "  this.channel = channel;\n"
		 "}\n"
		 "\n"
		 "private final com.google.protobuf.RpcChannel channel;\n"
		 "\n"
		 "public com.google.protobuf.RpcChannel getChannel() {\n"
		 "  return channel;\n"
		 "}\n",
		 NULL);

	for (int i = 0; i < g->descriptor->n_methods; i++) {
		const LpMethod *method = (&g->descriptor->methods[i]);
		lp_print(printer, "\n", NULL);
		ImmutableServiceGenerator_GenerateMethodSignature(g, printer, method, IS_CONCRETE);
		lp_print(printer, " {\n", NULL);
		lp_indent(printer);

		LpVars vars = { 0 };
		lp_vars_set(&vars, "index", xcat(jv_i(i)));
		lp_vars_set(&vars, "output", ImmutableServiceGenerator_GetOutput(g, method));
		lp_print_vars(printer, &vars,
			      "channel.callMethod(\n"
			      "  getDescriptor().getMethod($index$),\n"
			      "  controller,\n"
			      "  request,\n"
			      "  $output$.getDefaultInstance(),\n"
			      "  com.google.protobuf.RpcUtil.generalizeCallback(\n"
			      "    done,\n"
			      "    $output$.class,\n"
			      "    $output$.getDefaultInstance()));\n");

		lp_outdent(printer);
		lp_print(printer, "}\n", NULL);
	}

	lp_outdent(printer);
	lp_print(printer,
		 "}\n"
		 "\n",
		 NULL);
}

static void ImmutableServiceGenerator_GenerateBlockingStub(JvServiceGen *g, LpPrinter *printer)
{
	lp_print(printer,
		 "public static BlockingInterface newBlockingStub(\n"
		 "    com.google.protobuf.BlockingRpcChannel channel) {\n"
		 "  return new BlockingStub(channel);\n"
		 "}\n"
		 "\n",
		 NULL);

	lp_print(printer, "public interface BlockingInterface {", NULL);
	lp_indent(printer);

	for (int i = 0; i < g->descriptor->n_methods; i++) {
		const LpMethod *method = (&g->descriptor->methods[i]);
		ImmutableServiceGenerator_GenerateBlockingMethodSignature(g, printer, method);
		lp_print(printer, ";\n", NULL);
	}

	lp_outdent(printer);
	lp_print(printer,
		 "}\n"
		 "\n",
		 NULL);

	lp_print(printer,
		 "private static final class BlockingStub implements BlockingInterface "
		 "{\n",
		 NULL);
	lp_indent(printer);

	lp_print(printer,
		 "private BlockingStub(com.google.protobuf.BlockingRpcChannel channel) {\n"
		 "  this.channel = channel;\n"
		 "}\n"
		 "\n"
		 "private final com.google.protobuf.BlockingRpcChannel channel;\n",
		 NULL);

	for (int i = 0; i < g->descriptor->n_methods; i++) {
		const LpMethod *method = (&g->descriptor->methods[i]);
		ImmutableServiceGenerator_GenerateBlockingMethodSignature(g, printer, method);
		lp_print(printer, " {\n", NULL);
		lp_indent(printer);

		LpVars vars = { 0 };
		lp_vars_set(&vars, "index", xcat(jv_i(i)));
		lp_vars_set(&vars, "output", ImmutableServiceGenerator_GetOutput(g, method));
		lp_print_vars(printer, &vars,
			      "return ($output$) channel.callBlockingMethod(\n"
			      "  getDescriptor().getMethod($index$),\n"
			      "  controller,\n"
			      "  request,\n"
			      "  $output$.getDefaultInstance());\n");

		lp_outdent(printer);
		lp_print(printer,
			 "}\n"
			 "\n",
			 NULL);
	}

	lp_outdent(printer);
	lp_print(printer, "}\n", NULL);
}

static void ImmutableServiceGenerator_GenerateMethodSignature(JvServiceGen *g, LpPrinter *printer,
							      const LpMethod *method, int is_abstract)
{
	LpVars vars = { 0 };
	lp_vars_set(&vars, "name", jv_underscores_to_camel_case_method(method));
	lp_vars_set(&vars, "input", jv_immutable_class_name(method->input_type));
	lp_vars_set(&vars, "output", ImmutableServiceGenerator_GetOutput(g, method));
	lp_vars_set(&vars, "abstract", (is_abstract == IS_ABSTRACT) ? "abstract" : "");
	lp_print_vars(printer, &vars,
		      "public $abstract$ void $name$(\n"
		      "    com.google.protobuf.RpcController controller,\n"
		      "    $input$ request,\n"
		      "    com.google.protobuf.RpcCallback<$output$> done)");
}

static void ImmutableServiceGenerator_GenerateBlockingMethodSignature(JvServiceGen *g, LpPrinter *printer,
								      const LpMethod *method)
{
	LpVars vars = { 0 };
	lp_vars_set(&vars, "method", jv_underscores_to_camel_case_method(method));
	lp_vars_set(&vars, "input", jv_immutable_class_name(method->input_type));
	lp_vars_set(&vars, "output", ImmutableServiceGenerator_GetOutput(g, method));
	lp_print_vars(printer, &vars,
		      "\n"
		      "public $output$ $method$(\n"
		      "    com.google.protobuf.RpcController controller,\n"
		      "    $input$ request)\n"
		      "    throws com.google.protobuf.ServiceException");
}
