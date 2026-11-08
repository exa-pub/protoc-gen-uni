// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* Port of src/google/protobuf/compiler/cpp/service.{h,cc} (protobuf v36.2). */

static ServiceGenerator *service_generator_new(const LpService *descriptor, const Subs *vars, const Options *options)
{
	ServiceGenerator *g = xalloc(sizeof *g);
	g->descriptor = descriptor;
	g->options = options;
	g->vars = subs_copy(vars);
	subs_set(&g->vars, "classname", descriptor->name);
	subs_set(&g->vars, "full_name", descriptor->full_name);
	return g;
}

typedef struct {
	const ServiceGenerator *g;
	bool is_virtual;
	bool request;
} ServiceCtx;

static void service_method_signatures(LpPrinter *p, void *arg)
{
	const ServiceCtx *c = arg;
	const LpService *d = c->g->descriptor;
	for (int i = 0; i < d->n_methods; ++i) {
		const LpMethod *m = &d->methods[i];
		EMIT(p,
		     "\n"
		     " $virtual $void $name$($pb$::RpcController* $nullable$ controller,\n"
		     "                       const $input$* $nonnull$ request,\n"
		     "                       $output$* $nonnull$ response,\n"
		     "                       ::google::protobuf::Closure* $nullable$ done)$ override$;\n",
		     S_("name", m->name), S_("input", qualified_class_name(m->input_type, c->g->options)),
		     S_("output", qualified_class_name(m->output_type, c->g->options)),
		     S_("virtual", c->is_virtual ? "virtual" : ""), S_("override", !c->is_virtual ? "override" : ""));
	}
}

static void service_generate_declarations(ServiceGenerator *g, LpPrinter *p)
{
	PUSH_SUBS(p, &g->vars);
	ServiceCtx virts = {.g = g, .is_virtual = true}, impls = {.g = g};
	EMIT(p,
	     "\n"
	     " class $classname$_Stub;\n"
	     " class $dllexport_decl $$classname$ : public $pb$::Service {\n"
	     "  protected:\n"
	     "   $classname$() = default;\n"
	     "\n"
	     "  public:\n"
	     "   using Stub = $classname$_Stub;\n"
	     "\n"
	     "   $classname$(const $classname$&) = delete;\n"
	     "   $classname$& operator=(const $classname$&) = delete;\n"
	     "   virtual ~$classname$() = default;\n"
	     "\n"
	     "   static const $pb$::ServiceDescriptor* $nonnull$ descriptor();\n"
	     "\n"
	     "   $virts$;\n"
	     "\n"
	     "   // implements Service ----------------------------------------------\n"
	     "   const $pb$::ServiceDescriptor* $nonnull$ GetDescriptor() override;\n"
	     "\n"
	     "   void CallMethod(\n"
	     "       //~\n"
	     "       const $pb$::MethodDescriptor* $nonnull$ method,\n"
	     "       $pb$::RpcController* $nullable$ controller,\n"
	     "       const $pb$::Message* $nonnull$ request,\n"
	     "       $pb$::Message* $nonnull$ response,\n"
	     "       ::google::protobuf::Closure* $nullable$ done) override;\n"
	     "\n"
	     "   const $pb$::Message& GetRequestPrototype(\n"
	     "       const $pb$::MethodDescriptor* $nonnull$ method) const override;\n"
	     "\n"
	     "   const $pb$::Message& GetResponsePrototype(\n"
	     "       const $pb$::MethodDescriptor* $nonnull$ method) const override;\n"
	     " };\n"
	     "\n"
	     " class $dllexport_decl $$classname$_Stub final : public $classname$ {\n"
	     "  public:\n"
	     "   //~ It seems like channel should be nonnull, but some tests use\n"
	     "   //~ nullptr. TODO: clean up and switch to nonnull.\n"
	     "   $classname$_Stub($pb$::RpcChannel* $nullable$ channel);\n"
	     "   $classname$_Stub($pb$::RpcChannel* $nullable$ channel,\n"
	     "                    $pb$::Service::ChannelOwnership ownership);\n"
	     "\n"
	     "   $classname$_Stub(const $classname$_Stub&) = delete;\n"
	     "   $classname$_Stub& operator=(const $classname$_Stub&) = delete;\n"
	     "\n"
	     "   ~$classname$_Stub() override;\n"
	     "\n"
	     "   inline $pb$::RpcChannel* $nullable$ channel() { return channel_; }\n"
	     "\n"
	     "   // implements $classname$ ------------------------------------------\n"
	     "   $impls$;\n"
	     "\n"
	     "  private:\n"
	     "   $pb$::RpcChannel* $nullable$ channel_;\n"
	     "   bool owns_channel_;\n"
	     " };\n",
	     SCB("virts", service_method_signatures, &virts), SCB("impls", service_method_signatures, &impls));
	POP_VARS(p);
}

static void service_not_implemented_methods(LpPrinter *p, void *arg)
{
	const ServiceGenerator *g = arg;
	for (int i = 0; i < g->descriptor->n_methods; ++i) {
		const LpMethod *m = &g->descriptor->methods[i];
		EMIT(p,
		     "\n"
		     " void $classname$::$name$($pb$::RpcController* $nullable$ controller,\n"
		     "                          const $input$* $nonnull$,\n"
		     "                          $output$* $nonnull$,\n"
		     "                          ::google::protobuf::Closure* $nullable$ done) {\n"
		     "   controller->SetFailed(\"Method $name$() not implemented.\");\n"
		     "   done->Run();\n"
		     " }\n",
		     S_("name", m->name), S_("input", qualified_class_name(m->input_type, g->options)),
		     S_("output", qualified_class_name(m->output_type, g->options)));
	}
}

static void service_call_method_cases(LpPrinter *p, void *arg)
{
	const ServiceGenerator *g = arg;
	for (int i = 0; i < g->descriptor->n_methods; ++i) {
		const LpMethod *m = &g->descriptor->methods[i];
		EMIT(p,
		     "\n"
		     " case $index$:\n"
		     "   this->$name$(controller, $pb$::DownCastMessage<$input$>(request),\n"
		     "                $pb$::DownCastMessage<$output$>(response), done);\n"
		     "   break;\n",
		     S_("name", m->name), S_("input", qualified_class_name(m->input_type, g->options)),
		     S_("output", qualified_class_name(m->output_type, g->options)), SINT("index", i));
	}
}

static void service_call_method(LpPrinter *p, void *arg)
{
	const ServiceGenerator *g = arg;
	EMIT(p,
	     "\n"
	     " void $classname$::CallMethod(\n"
	     "     const $pb$::MethodDescriptor* $nonnull$ method,\n"
	     "     $pb$::RpcController* $nullable$ controller,\n"
	     "     const $pb$::Message* $nonnull$ request,\n"
	     "     $pb$::Message* $nonnull$ response, ::google::protobuf::Closure* $nullable$ done) {\n"
	     "   ABSL_DCHECK_EQ(method->service(), $file_level_service_descriptors$[$index$]);\n"
	     "   switch (method->index()) {\n"
	     "     $cases$;\n"
	     "\n"
	     "     default:\n"
	     "       ABSL_LOG(FATAL) << \"Bad method index; this should never happen.\";\n"
	     "       break;\n"
	     "   }\n"
	     " }\n",
	     SINT("index", g->index_in_metadata), SCB("cases", service_call_method_cases, g));
}

static void service_prototype_cases(LpPrinter *p, void *arg)
{
	const ServiceCtx *c = arg;
	for (int i = 0; i < c->g->descriptor->n_methods; ++i) {
		const LpMethod *m = &c->g->descriptor->methods[i];
		EMIT(p,
		     "\n"
		     " case $index$:\n"
		     "   return $type$::default_instance();\n",
		     SINT("index", i),
		     S_("type", qualified_class_name(c->request ? m->input_type : m->output_type, c->g->options)));
	}
}

static void service_get_prototype(LpPrinter *p, void *arg)
{
	const ServiceCtx *c = arg;
	EMIT(p,
	     "\n"
	     " const $pb$::Message& $classname$::Get$which$Prototype(\n"
	     "     const $pb$::MethodDescriptor* $nonnull$ method) const {\n"
	     "   ABSL_DCHECK_EQ(method->service(), descriptor());\n"
	     "   switch (method->index()) {\n"
	     "     $cases$;\n"
	     "\n"
	     "     default:\n"
	     "       ABSL_LOG(FATAL) << \"Bad method index; this should never happen.\";\n"
	     "       return *$pb$::MessageFactory::generated_factory()->GetPrototype(\n"
	     "           method->$which_type$_type());\n"
	     "   }\n"
	     " }\n",
	     S_("which", c->request ? "Request" : "Response"), S_("which_type", c->request ? "input" : "output"),
	     SCB("cases", service_prototype_cases, c));
}

static void service_stub_methods(LpPrinter *p, void *arg)
{
	const ServiceGenerator *g = arg;
	for (int i = 0; i < g->descriptor->n_methods; ++i) {
		const LpMethod *m = &g->descriptor->methods[i];
		EMIT(p,
		     "\n"
		     " void $classname$_Stub::$name$(\n"
		     "     $pb$::RpcController* $nullable$ controller,\n"
		     "     const $input$* $nonnull$ request, $output$* $nonnull$ response,\n"
		     "     ::google::protobuf::Closure* $nullable$ done) {\n"
		     "   channel_->CallMethod(descriptor()->method($index$), controller,\n"
		     "                        request, response, done);\n"
		     " }\n",
		     S_("name", m->name), S_("input", qualified_class_name(m->input_type, g->options)),
		     S_("output", qualified_class_name(m->output_type, g->options)), SINT("index", i));
	}
}

static void service_generate_implementation(ServiceGenerator *g, LpPrinter *p)
{
	PUSH_SUBS(p, &g->vars);
	ServiceCtx req = {.g = g, .request = true}, resp = {.g = g};
	EMIT(p,
	     "\n"
	     " const $pb$::ServiceDescriptor* $nonnull$ $classname$::descriptor() {\n"
	     "   $pbi$::AssignDescriptors(&$desc_table$);\n"
	     "   return $file_level_service_descriptors$[$index$];\n"
	     " }\n"
	     "\n"
	     " const $pb$::ServiceDescriptor* $nonnull$ $classname$::GetDescriptor() {\n"
	     "   return descriptor();\n"
	     " }\n"
	     "\n"
	     " $no_impl_methods$;\n"
	     "\n"
	     " $call_method$;\n"
	     "\n"
	     " $get_request$;\n"
	     "\n"
	     " $get_response$;\n"
	     "\n"
	     " $classname$_Stub::$classname$_Stub($pb$::RpcChannel* $nullable$ channel)\n"
	     "     : channel_(channel), owns_channel_(false) {}\n"
	     "\n"
	     " $classname$_Stub::$classname$_Stub(\n"
	     "     $pb$::RpcChannel* $nullable$ channel,\n"
	     "     $pb$::Service::ChannelOwnership ownership)\n"
	     "     : channel_(channel),\n"
	     "       owns_channel_(ownership == $pb$::Service::STUB_OWNS_CHANNEL) {}\n"
	     "\n"
	     " $classname$_Stub::~$classname$_Stub() {\n"
	     "   if (owns_channel_) delete channel_;\n"
	     " }\n"
	     "\n"
	     " $stub_methods$;\n",
	     SINT("index", g->index_in_metadata), SCB("no_impl_methods", service_not_implemented_methods, g),
	     SCB("call_method", service_call_method, g), SCB("get_request", service_get_prototype, &req),
	     SCB("get_response", service_get_prototype, &resp), SCB("stub_methods", service_stub_methods, g));
	POP_VARS(p);
}
