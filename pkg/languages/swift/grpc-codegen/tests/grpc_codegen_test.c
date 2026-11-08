/* pkg/languages/swift/grpc-codegen: expected values from the tests of grpc-swift-2 2.4.4
 * (Tests/GRPCCodeGenTests/Internal/Translator: DocsTests.swift,
 * IDLToStructuredSwiftTranslatorSnippetBasedTests.swift; the imports with @_spi of testGeneration
 * are left out: the port has no spi imports). */
#include "grpc_codegen.h"
#include "structured_swift.h"
#include "test.h"

static void check_str(Str got, const char *want) { CHECK_MEM(got.p, got.n, want, strlen(want)); }

static void test_docs(void)
{
	check_str(gc_docs_suffix(STR_LIT("/// Foo"),
				 STR_LIT("/// Some additional pre-formatted docs\n/// split over multiple lines.")),
		  "/// Foo\n///\n/// > Source IDL Documentation:\n/// >\n/// > Some additional pre-formatted docs\n"
		  "/// > split over multiple lines.");
	check_str(gc_docs_suffix(STR_LIT("/// Foo"), STR_LIT("")), "/// Foo");
	check_str(gc_docs_interpose(STR_LIT("/// Additional docs\n/// On multiple lines"), STR_LIT("/// Header"),
				    STR_LIT("/// Footer")),
		  "/// Header\n///\n/// > Source IDL Documentation:\n/// >\n/// > Additional docs\n/// > On multiple lines\n"
		  "///\n/// Footer");
	check_str(gc_docs_interpose(STR_LIT(""), STR_LIT("/// Header"), STR_LIT("/// Footer")),
		  "/// Header\n///\n/// Footer");
}

static char *serializer(void *ctx, const char *type)
{
	(void)ctx;
	return xcat("GRPCProtobuf.ProtobufSerializer<", type, ">()");
}

static char *deserializer(void *ctx, const char *type)
{
	(void)ctx;
	return xcat("GRPCProtobuf.ProtobufDeserializer<", type, ">()");
}

static GcRequest request(GcService *services, int n)
{
	return (GcRequest){.file_name = "test.grpc",
			   .leading_trivia = STR_LIT("/// Some really exciting license header 2023.\n"),
			   .services = services,
			   .n_services = n,
			   .make_serializer = serializer,
			   .make_deserializer = deserializer};
}

static void test_generation(void)
{
	GcService service = {.documentation = STR_LIT("/// Documentation for AService\n"),
			     .name = {"namespaceA.ServiceA", "NamespaceA_ServiceA", "namespaceA_ServiceA"}};
	GcRequest r = request(&service, 1);
	GcConfig config = gc_config(GC_LEVEL_PUBLIC, true, false, true, 4);
	check_str(gc_generate(&config, &r),
	"/// Some really exciting license header 2023.\n"
	"\n"
	"public import GRPCCore\n"
	"\n"
	"// MARK: - namespaceA.ServiceA\n"
	"\n"
	"/// Namespace containing generated types for the \"namespaceA.ServiceA\" service.\n"
	"@available(macOS 15.0, iOS 18.0, watchOS 11.0, tvOS 18.0, visionOS 2.0, *)\n"
	"public enum NamespaceA_ServiceA: Sendable {\n"
	"    /// Service descriptor for the \"namespaceA.ServiceA\" service.\n"
	"    public static let descriptor = GRPCCore.ServiceDescriptor(fullyQualifiedService: \"namespaceA.ServiceA\")\n"
	"    /// Namespace for method metadata.\n"
	"    public enum Method: Sendable {\n"
	"        /// Descriptors for all methods in the \"namespaceA.ServiceA\" service.\n"
	"        public static let descriptors: [GRPCCore.MethodDescriptor] = []\n"
	"    }\n"
	"}\n"
	"\n"
	"@available(macOS 15.0, iOS 18.0, watchOS 11.0, tvOS 18.0, visionOS 2.0, *)\n"
	"extension GRPCCore.ServiceDescriptor {\n"
	"    /// Service descriptor for the \"namespaceA.ServiceA\" service.\n"
	"    public static let namespaceA_ServiceA = GRPCCore.ServiceDescriptor(fullyQualifiedService: \"namespaceA.ServiceA\")\n"
	"}\n"
	"\n"
	"// MARK: namespaceA.ServiceA (server)\n"
	"\n"
	"@available(macOS 15.0, iOS 18.0, watchOS 11.0, tvOS 18.0, visionOS 2.0, *)\n"
	"extension NamespaceA_ServiceA {\n"
	"    /// Streaming variant of the service protocol for the \"namespaceA.ServiceA\" service.\n"
	"    ///\n"
	"    /// This protocol is the lowest-level of the service protocols generated for this service\n"
	"    /// giving you the most flexibility over the implementation of your service. This comes at\n"
	"    /// the cost of more verbose and less strict APIs. Each RPC requires you to implement it in\n"
	"    /// terms of a request stream and response stream. Where the RPC expects only a single\n"
	"    /// request or response message, you are responsible for ensuring your implementation\n"
	"    /// maintains this invariant.\n"
	"    ///\n"
	"    /// Where possible, prefer using the stricter, less-verbose ``ServiceProtocol``\n"
	"    /// or ``SimpleServiceProtocol`` instead.\n"
	"    ///\n"
	"    /// > Source IDL Documentation:\n"
	"    /// >\n"
	"    /// > Documentation for AService\n"
	"    public protocol StreamingServiceProtocol: GRPCCore.RegistrableRPCService {}\n"
	"\n"
	"    /// Service protocol for the \"namespaceA.ServiceA\" service.\n"
	"    ///\n"
	"    /// This protocol is higher level than ``StreamingServiceProtocol`` but lower level than\n"
	"    /// the ``SimpleServiceProtocol``, it provides access to request and response metadata and\n"
	"    /// trailing response metadata. If you don't need these then consider using\n"
	"    /// the ``SimpleServiceProtocol``. If you need fine grained control over your RPCs then\n"
	"    /// use ``StreamingServiceProtocol``.\n"
	"    ///\n"
	"    /// > Source IDL Documentation:\n"
	"    /// >\n"
	"    /// > Documentation for AService\n"
	"    public protocol ServiceProtocol: NamespaceA_ServiceA.StreamingServiceProtocol {}\n"
	"\n"
	"    /// Simple service protocol for the \"namespaceA.ServiceA\" service.\n"
	"    ///\n"
	"    /// This is the highest level protocol for the service. The API is the easiest to use but\n"
	"    /// doesn't provide access to request or response metadata. If you need access to these\n"
	"    /// then use ``ServiceProtocol`` instead.\n"
	"    ///\n"
	"    /// > Source IDL Documentation:\n"
	"    /// >\n"
	"    /// > Documentation for AService\n"
	"    public protocol SimpleServiceProtocol: NamespaceA_ServiceA.ServiceProtocol {}\n"
	"}\n"
	"\n"
	"// Default implementation of 'registerMethods(with:)'.\n"
	"@available(macOS 15.0, iOS 18.0, watchOS 11.0, tvOS 18.0, visionOS 2.0, *)\n"
	"extension NamespaceA_ServiceA.StreamingServiceProtocol {\n"
	"    public func registerMethods<Transport>(with router: inout GRPCCore.RPCRouter<Transport>) where Transport: GRPCCore.ServerTransport {}\n"
	"}\n"
	"\n"
	"// Default implementation of streaming methods from 'StreamingServiceProtocol'.\n"
	"@available(macOS 15.0, iOS 18.0, watchOS 11.0, tvOS 18.0, visionOS 2.0, *)\n"
	"extension NamespaceA_ServiceA.ServiceProtocol {\n"
	"}\n"
	"\n"
	"// Default implementation of methods from 'ServiceProtocol'.\n"
	"@available(macOS 15.0, iOS 18.0, watchOS 11.0, tvOS 18.0, visionOS 2.0, *)\n"
	"extension NamespaceA_ServiceA.SimpleServiceProtocol {\n"
	"}");
}

static void test_empty_file_generation(void)
{
	GcRequest r = request(NULL, 0);
	GcConfig config = gc_config(GC_LEVEL_PUBLIC, true, false, true, 4);
	check_str(gc_generate(&config, &r), "/// Some really exciting license header 2023.\n\n// This file contained no services.");
}

static GcRequest *error_request;

static void run_generate(void *arg)
{
	GcConfig config = gc_config(GC_LEVEL_PUBLIC, true, true, true, 4);
	gc_generate(&config, (GcRequest *)arg);
}

static void test_same_lower_case_name_methods_error(void)
{
	GcMethod methods[2] = {{.name = {"MethodA", "MethodA", "methodA"}, .input_type = "A", .output_type = "B"},
			       {.name = {"MethodB", "MethodB", "methodA"}, .input_type = "A", .output_type = "B"}};
	GcService service = {.name = {"namespacea.AService", "NamespaceA_AService", "namespacea_AService"},
			     .methods = methods,
			     .n_methods = 2};
	GcRequest r = request(&service, 1);
	error_request = &r;
	CHECK(try_run(run_generate, error_request) != NULL);
	CHECK_STR(fail_message().p,
		  "Code(value: GRPCCodeGen.CodeGenError.Code.(unknown context at $0).Value.nonUniqueMethodName): \"Methods of "
		  "a service must have unique lower case names. methodA is used as a signature name for multiple methods of "
		  "the namespacea.AService service.\"");
}

TESTS(TEST(test_docs), TEST(test_generation), TEST(test_empty_file_generation),
      TEST(test_same_lower_case_name_methods_error))
