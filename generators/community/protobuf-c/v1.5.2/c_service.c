// Copyright 2008 Google Inc.  All rights reserved.
// Copyright (c) 2008-2025, Dave Benson and the protobuf-c authors.  All rights reserved.
// Use of this source code is governed by the BSD-style licenses reproduced in generator.c.

/* Port of protoc-gen-c/c_service.{h,cc} (protobuf-c v1.5.2): ServiceGenerator. Its variables
 * (vars_) are kept between the methods, like upstream's member map. */

static void service_generator_init(ServiceGenerator *g, const LpService *descriptor, const char *dllexport_decl)
{
	g->descriptor = descriptor;
	LpVars3 *v = &g->vars;
	lp3_vars_set(v, "name", descriptor->name);
	lp3_vars_set(v, "fullname", descriptor->full_name);
	lp3_vars_set(v, "cname", full_name_to_c(descriptor->full_name, descriptor->file));
	char *lcfullname = full_name_to_lower(descriptor->full_name, descriptor->file);
	lp3_vars_set(v, "lcfullname", lcfullname);
	lp3_vars_set(v, "ucfullname", full_name_to_upper(descriptor->full_name, descriptor->file));
	lp3_vars_set(v, "lcfullpadd", convert_to_spaces(lcfullname));
	lp3_vars_set(v, "package", descriptor->file->package);
	lp3_vars_set(v, "dllexport", *dllexport_decl ? xcat(dllexport_decl, " ") : "");
}

/* The variables of a method: method, metpad, input_typename, output_typename */
static void service_method_vars(ServiceGenerator *g, const LpMethod *method)
{
	char *lcname = camel_to_lower(method->name);
	lp3_vars_set(&g->vars, "method", lcname);
	lp3_vars_set(&g->vars, "metpad", convert_to_spaces(lcname));
	lp3_vars_set(&g->vars, "input_typename", full_name_to_c(method->input_type->full_name, method->input_type->file));
	lp3_vars_set(&g->vars, "output_typename",
		     full_name_to_c(method->output_type->full_name, method->output_type->file));
}

/* GenerateVfuncs */
static void service_generate_vfuncs(ServiceGenerator *g, LpPrinter3 *printer)
{
	const LpService *d = g->descriptor;
	lp3_print_vars(printer, &g->vars,
		       "typedef struct $cname$_Service $cname$_Service;\n"
		       "struct $cname$_Service\n"
		       "{\n"
		       "  ProtobufCService base;\n");
	for (int i = 0; i < d->n_methods; i++) {
		service_method_vars(g, &d->methods[i]);
		lp3_print_vars(printer, &g->vars,
			       "  void (*$method$)($cname$_Service *service,\n"
			       "         $metpad$  const $input_typename$ *input,\n"
			       "         $metpad$  $output_typename$_Closure closure,\n"
			       "         $metpad$  void *closure_data);\n");
	}
	lp3_print_vars(printer, &g->vars, "};\n");
	lp3_print_vars(printer, &g->vars,
		       "typedef void (*$cname$_ServiceDestroy)($cname$_Service *);\n"
		       "void $lcfullname$__init ($cname$_Service *service,\n"
		       "     $lcfullpadd$        $cname$_ServiceDestroy destroy);\n");
}

/* GenerateInitMacros */
static void service_generate_init_macros(ServiceGenerator *g, LpPrinter3 *printer)
{
	const LpService *d = g->descriptor;
	lp3_print_vars(printer, &g->vars,
		       "#define $ucfullname$__BASE_INIT \\\n"
		       "    { &$lcfullname$__descriptor, protobuf_c_service_invoke_internal, NULL }\n"
		       "#define $ucfullname$__INIT(function_prefix__) \\\n"
		       "    { $ucfullname$__BASE_INIT");
	for (int i = 0; i < d->n_methods; i++) {
		char *lcname = camel_to_lower(d->methods[i].name);
		lp3_vars_set(&g->vars, "method", lcname);
		lp3_vars_set(&g->vars, "metpad", convert_to_spaces(lcname));
		lp3_print_vars(printer, &g->vars, ",\\\n      function_prefix__ ## $method$");
	}
	lp3_print_vars(printer, &g->vars, "  }\n");
}

/* The variables of a caller: those of the method, padddddddddddddddddd */
static void service_caller_vars(ServiceGenerator *g, const LpMethod *method)
{
	const LpService *d = g->descriptor;
	char *lcname = camel_to_lower(method->name);
	char *lcfullname = full_name_to_lower(d->full_name, d->file);
	service_method_vars(g, method);
	lp3_vars_set(&g->vars, "padddddddddddddddddd", convert_to_spaces(xcat(lcfullname, "__", lcname)));
}

/* GenerateCallersDeclarations */
static void service_generate_callers_declarations(ServiceGenerator *g, LpPrinter3 *printer)
{
	const LpService *d = g->descriptor;
	for (int i = 0; i < d->n_methods; i++) {
		service_caller_vars(g, &d->methods[i]);
		lp3_print_vars(printer, &g->vars,
			       "void $lcfullname$__$method$(ProtobufCService *service,\n"
			       "     $padddddddddddddddddd$ const $input_typename$ *input,\n"
			       "     $padddddddddddddddddd$ $output_typename$_Closure closure,\n"
			       "     $padddddddddddddddddd$ void *closure_data);\n");
	}
}

/* GenerateMainHFile */
static void service_generate_main_h_file(ServiceGenerator *g, LpPrinter3 *printer)
{
	service_generate_vfuncs(g, printer);
	service_generate_init_macros(g, printer);
	service_generate_callers_declarations(g, printer);
}

static void service_generate_descriptor_declarations(ServiceGenerator *g, LpPrinter3 *printer)
{
	lp3_print_vars(printer, &g->vars, "extern const ProtobufCServiceDescriptor $lcfullname$__descriptor;\n");
}

/* GenerateInit */
static void service_generate_init(ServiceGenerator *g, LpPrinter3 *printer)
{
	lp3_print_vars(printer, &g->vars,
		       "void $lcfullname$__init ($cname$_Service *service,\n"
		       "     $lcfullpadd$        $cname$_ServiceDestroy destroy)\n"
		       "{\n"
		       "  protobuf_c_service_generated_init (&service->base,\n"
		       "                                     &$lcfullname$__descriptor,\n"
		       "                                     (ProtobufCServiceDestroy) destroy);\n"
		       "}\n");
}

typedef struct {
	unsigned i;
	const char *name;
} MethodIndexAndName;

/* compare_method_index_and_name_by_name (std::string::compare) */
static int compare_method_index_and_name_by_name(const void *a, const void *b)
{
	return strcmp(((const MethodIndexAndName *)a)->name, ((const MethodIndexAndName *)b)->name);
}

/* GenerateServiceDescriptor */
static void service_generate_service_descriptor(ServiceGenerator *g, LpPrinter3 *printer)
{
	const LpService *d = g->descriptor;
	int n_methods = d->n_methods;
	MethodIndexAndName *mi_array = xalloc(sizeof(MethodIndexAndName) * (size_t)(n_methods + 1));
	bool code_size = optimize_code_size(d->file);

	lp3_vars_set(&g->vars, "n_methods", simple_itoa(n_methods));
	lp3_print_vars(printer, &g->vars,
		       "static const ProtobufCMethodDescriptor $lcfullname$__method_descriptors[$n_methods$] =\n"
		       "{\n");
	for (int i = 0; i < n_methods; i++) {
		const LpMethod *method = &d->methods[i];
		lp3_vars_set(&g->vars, "method", method->name);
		lp3_vars_set(&g->vars, "input_descriptor",
			     xcat("&", full_name_to_lower(method->input_type->full_name, method->input_type->file),
				  "__descriptor"));
		lp3_vars_set(&g->vars, "output_descriptor",
			     xcat("&", full_name_to_lower(method->output_type->full_name, method->output_type->file),
				  "__descriptor"));
		if (code_size)
			lp3_print_vars(printer, &g->vars,
				       "  { NULL, $input_descriptor$, $output_descriptor$ }, /* CODE_SIZE */\n");
		else
			lp3_print_vars(printer, &g->vars, "  { \"$method$\", $input_descriptor$, $output_descriptor$ },\n");
		mi_array[i] = (MethodIndexAndName){(unsigned)i, method->name};
	}
	lp3_print_vars(printer, &g->vars, "};\n");

	if (!code_size) {
		qsort(mi_array, (size_t)n_methods, sizeof(MethodIndexAndName), compare_method_index_and_name_by_name);
		lp3_print_vars(printer, &g->vars, "const unsigned $lcfullname$__method_indices_by_name[] = {\n");
		for (int i = 0; i < n_methods; i++) {
			lp3_vars_set(&g->vars, "i", simple_utoa(mi_array[i].i));
			lp3_vars_set(&g->vars, "name", mi_array[i].name);
			lp3_vars_set(&g->vars, "comma", (i + 1 < n_methods) ? "," : " ");
			lp3_print_vars(printer, &g->vars, "  $i$$comma$        /* $name$ */\n");
		}
		lp3_print_vars(printer, &g->vars, "};\n");
		lp3_vars_set(&g->vars, "name", d->name);
	}

	if (code_size)
		lp3_print_vars(printer, &g->vars,
			       "const ProtobufCServiceDescriptor $lcfullname$__descriptor =\n"
			       "{\n"
			       "  PROTOBUF_C__SERVICE_DESCRIPTOR_MAGIC,\n"
			       "  NULL,NULL,NULL,NULL, /* CODE_SIZE */\n"
			       "  $n_methods$,\n"
			       "  $lcfullname$__method_descriptors,\n"
			       "  NULL /* CODE_SIZE */\n"
			       "};\n");
	else
		lp3_print_vars(printer, &g->vars,
			       "const ProtobufCServiceDescriptor $lcfullname$__descriptor =\n"
			       "{\n"
			       "  PROTOBUF_C__SERVICE_DESCRIPTOR_MAGIC,\n"
			       "  \"$fullname$\",\n"
			       "  \"$name$\",\n"
			       "  \"$cname$\",\n"
			       "  \"$package$\",\n"
			       "  $n_methods$,\n"
			       "  $lcfullname$__method_descriptors,\n"
			       "  $lcfullname$__method_indices_by_name\n"
			       "};\n");
}

/* GenerateCallersImplementations */
static void service_generate_callers_implementations(ServiceGenerator *g, LpPrinter3 *printer)
{
	const LpService *d = g->descriptor;
	for (int i = 0; i < d->n_methods; i++) {
		service_caller_vars(g, &d->methods[i]);
		lp3_vars_set(&g->vars, "index", simple_itoa(i));
		lp3_print_vars(printer, &g->vars,
			       "void $lcfullname$__$method$(ProtobufCService *service,\n"
			       "     $padddddddddddddddddd$ const $input_typename$ *input,\n"
			       "     $padddddddddddddddddd$ $output_typename$_Closure closure,\n"
			       "     $padddddddddddddddddd$ void *closure_data)\n"
			       "{\n"
			       "  assert(service->descriptor == &$lcfullname$__descriptor);\n"
			       "  service->invoke(service, $index$, (const ProtobufCMessage *) input, (ProtobufCClosure) "
			       "closure, closure_data);\n"
			       "}\n");
	}
}

/* GenerateCFile */
static void service_generate_c_file(ServiceGenerator *g, LpPrinter3 *printer)
{
	service_generate_service_descriptor(g, printer);
	service_generate_callers_implementations(g, printer);
	service_generate_init(g, printer);
}
