// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* The generated pool of protoc_lib 36.2: the serialized descriptors compiled into it (copied
 * verbatim from the descriptor_table_protodef_* arrays of the checked-in .pb.cc files of the v36.2
 * tag) and src/google/protobuf/cpp_edition_defaults.h (PROTOBUF_INTERNAL_CPP_EDITION_DEFAULTS,
 * the FeatureSetDefaults of the generated pool). lp_builtin_pool() builds them like the generated
 * pool does: it provides the schema of the request, the response and the options (generated
 * message semantics, with the linked extensions pb.cpp, pb.java, pb.csharp, pb.file.cpp and
 * pb.enumvalue.json), and the FeatureSet descriptor for CompileDefaults. */

/* descriptor.proto (src/google/protobuf/descriptor.pb.cc, 11749 bytes) */
static const char lp_embedded_descriptor[] =
	"\n google/protobuf/descriptor.proto\022\017goog"
	"le.protobuf\"U\n\021FileDescriptorSet\0222\n\004file"
	"\030\001 \003(\0132$.google.protobuf.FileDescriptorP"
	"roto*\014\010\200\354\312\377\001\020\201\354\312\377\001\"\241\004\n\023FileDescriptorPro"
	"to\022\014\n\004name\030\001 \001(\t\022\017\n\007package\030\002 \001(\t\022\022\n\ndep"
	"endency\030\003 \003(\t\022\031\n\021public_dependency\030\n \003(\005"
	"\022\027\n\017weak_dependency\030\013 \003(\005\022\031\n\021option_depe"
	"ndency\030\017 \003(\t\0226\n\014message_type\030\004 \003(\0132 .goo"
	"gle.protobuf.DescriptorProto\0227\n\tenum_typ"
	"e\030\005 \003(\0132$.google.protobuf.EnumDescriptor"
	"Proto\0228\n\007service\030\006 \003(\0132\'.google.protobuf"
	".ServiceDescriptorProto\0228\n\textension\030\007 \003"
	"(\0132%.google.protobuf.FieldDescriptorProt"
	"o\022-\n\007options\030\010 \001(\0132\034.google.protobuf.Fil"
	"eOptions\0229\n\020source_code_info\030\t \001(\0132\037.goo"
	"gle.protobuf.SourceCodeInfo\022\016\n\006syntax\030\014 "
	"\001(\t\022)\n\007edition\030\016 \001(\0162\030.google.protobuf.E"
	"dition\"\340\005\n\017DescriptorProto\022\014\n\004name\030\001 \001(\t"
	"\0224\n\005field\030\002 \003(\0132%.google.protobuf.FieldD"
	"escriptorProto\0228\n\textension\030\006 \003(\0132%.goog"
	"le.protobuf.FieldDescriptorProto\0225\n\013nest"
	"ed_type\030\003 \003(\0132 .google.protobuf.Descript"
	"orProto\0227\n\tenum_type\030\004 \003(\0132$.google.prot"
	"obuf.EnumDescriptorProto\022H\n\017extension_ra"
	"nge\030\005 \003(\0132/.google.protobuf.DescriptorPr"
	"oto.ExtensionRange\0229\n\noneof_decl\030\010 \003(\0132%"
	".google.protobuf.OneofDescriptorProto\0220\n"
	"\007options\030\007 \001(\0132\037.google.protobuf.Message"
	"Options\022F\n\016reserved_range\030\t \003(\0132..google"
	".protobuf.DescriptorProto.ReservedRange\022"
	"\025\n\rreserved_name\030\n \003(\t\0225\n\nvisibility\030\013 \001"
	"(\0162!.google.protobuf.SymbolVisibility\032e\n"
	"\016ExtensionRange\022\r\n\005start\030\001 \001(\005\022\013\n\003end\030\002 "
	"\001(\005\0227\n\007options\030\003 \001(\0132&.google.protobuf.E"
	"xtensionRangeOptions\032+\n\rReservedRange\022\r\n"
	"\005start\030\001 \001(\005\022\013\n\003end\030\002 \001(\005\"\355\003\n\025ExtensionR"
	"angeOptions\022C\n\024uninterpreted_option\030\347\007 \003"
	"(\0132$.google.protobuf.UninterpretedOption"
	"\022L\n\013declaration\030\002 \003(\01322.google.protobuf."
	"ExtensionRangeOptions.DeclarationB\003\210\001\002\022-"
	"\n\010features\0302 \001(\0132\033.google.protobuf.Featu"
	"reSet\022_\n\014verification\030\003 \001(\01628.google.pro"
	"tobuf.ExtensionRangeOptions.Verification"
	"State:\nUNVERIFIEDB\003\210\001\002\032h\n\013Declaration\022\016\n"
	"\006number\030\001 \001(\005\022\021\n\tfull_name\030\002 \001(\t\022\014\n\004type"
	"\030\003 \001(\t\022\020\n\010reserved\030\005 \001(\010\022\020\n\010repeated\030\006 \001"
	"(\010J\004\010\004\020\005\"4\n\021VerificationState\022\017\n\013DECLARA"
	"TION\020\000\022\016\n\nUNVERIFIED\020\001*\006\010\336\007\020\347\007*\t\010\350\007\020\200\200\200\200"
	"\002\"\325\005\n\024FieldDescriptorProto\022\014\n\004name\030\001 \001(\t"
	"\022\016\n\006number\030\003 \001(\005\022:\n\005label\030\004 \001(\0162+.google"
	".protobuf.FieldDescriptorProto.Label\0228\n\004"
	"type\030\005 \001(\0162*.google.protobuf.FieldDescri"
	"ptorProto.Type\022\021\n\ttype_name\030\006 \001(\t\022\020\n\010ext"
	"endee\030\002 \001(\t\022\025\n\rdefault_value\030\007 \001(\t\022\023\n\013on"
	"eof_index\030\t \001(\005\022\021\n\tjson_name\030\n \001(\t\022.\n\007op"
	"tions\030\010 \001(\0132\035.google.protobuf.FieldOptio"
	"ns\022\027\n\017proto3_optional\030\021 \001(\010\"\266\002\n\004Type\022\017\n\013"
	"TYPE_DOUBLE\020\001\022\016\n\nTYPE_FLOAT\020\002\022\016\n\nTYPE_IN"
	"T64\020\003\022\017\n\013TYPE_UINT64\020\004\022\016\n\nTYPE_INT32\020\005\022\020"
	"\n\014TYPE_FIXED64\020\006\022\020\n\014TYPE_FIXED32\020\007\022\r\n\tTY"
	"PE_BOOL\020\010\022\017\n\013TYPE_STRING\020\t\022\016\n\nTYPE_GROUP"
	"\020\n\022\020\n\014TYPE_MESSAGE\020\013\022\016\n\nTYPE_BYTES\020\014\022\017\n\013"
	"TYPE_UINT32\020\r\022\r\n\tTYPE_ENUM\020\016\022\021\n\rTYPE_SFI"
	"XED32\020\017\022\021\n\rTYPE_SFIXED64\020\020\022\017\n\013TYPE_SINT3"
	"2\020\021\022\017\n\013TYPE_SINT64\020\022\"C\n\005Label\022\022\n\016LABEL_O"
	"PTIONAL\020\001\022\022\n\016LABEL_REPEATED\020\003\022\022\n\016LABEL_R"
	"EQUIRED\020\002\"T\n\024OneofDescriptorProto\022\014\n\004nam"
	"e\030\001 \001(\t\022.\n\007options\030\002 \001(\0132\035.google.protob"
	"uf.OneofOptions\"\333\002\n\023EnumDescriptorProto\022"
	"\014\n\004name\030\001 \001(\t\0228\n\005value\030\002 \003(\0132).google.pr"
	"otobuf.EnumValueDescriptorProto\022-\n\007optio"
	"ns\030\003 \001(\0132\034.google.protobuf.EnumOptions\022N"
	"\n\016reserved_range\030\004 \003(\01326.google.protobuf"
	".EnumDescriptorProto.EnumReservedRange\022\025"
	"\n\rreserved_name\030\005 \003(\t\0225\n\nvisibility\030\006 \001("
	"\0162!.google.protobuf.SymbolVisibility\032/\n\021"
	"EnumReservedRange\022\r\n\005start\030\001 \001(\005\022\013\n\003end\030"
	"\002 \001(\005\"l\n\030EnumValueDescriptorProto\022\014\n\004nam"
	"e\030\001 \001(\t\022\016\n\006number\030\002 \001(\005\0222\n\007options\030\003 \001(\013"
	"2!.google.protobuf.EnumValueOptions\"\236\001\n\026"
	"ServiceDescriptorProto\022\014\n\004name\030\001 \001(\t\0226\n\006"
	"method\030\002 \003(\0132&.google.protobuf.MethodDes"
	"criptorProto\0220\n\007options\030\003 \001(\0132\037.google.p"
	"rotobuf.ServiceOptionsJ\004\010\004\020\005R\006stream\"\301\001\n"
	"\025MethodDescriptorProto\022\014\n\004name\030\001 \001(\t\022\022\n\n"
	"input_type\030\002 \001(\t\022\023\n\013output_type\030\003 \001(\t\022/\n"
	"\007options\030\004 \001(\0132\036.google.protobuf.MethodO"
	"ptions\022\037\n\020client_streaming\030\005 \001(\010:\005false\022"
	"\037\n\020server_streaming\030\006 \001(\010:\005false\"\252\t\n\013Fil"
	"eOptions\022\024\n\014java_package\030\001 \001(\t\022\034\n\024java_o"
	"uter_classname\030\010 \001(\t\022\346\001\n\023java_multiple_f"
	"iles\030\n \001(\010:\005falseB\301\001\262\001\275\001\010\346\007 \351\007*\264\001This be"
	"havior is enabled by default in editions"
	" 2024 and above. To disable it, you can "
	"set `features.(pb.java).nest_in_file_cla"
	"ss = YES` on individual messages, enums,"
	" or services.\022)\n\035java_generate_equals_an"
	"d_hash\030\024 \001(\010B\002\030\001\022%\n\026java_string_check_ut"
	"f8\030\033 \001(\010:\005false\022F\n\014optimize_for\030\t \001(\0162)."
	"google.protobuf.FileOptions.OptimizeMode"
	":\005SPEED\022\022\n\ngo_package\030\013 \001(\t\022\"\n\023cc_generi"
	"c_services\030\020 \001(\010:\005false\022$\n\025java_generic_"
	"services\030\021 \001(\010:\005false\022\"\n\023py_generic_serv"
	"ices\030\022 \001(\010:\005false\022\031\n\ndeprecated\030\027 \001(\010:\005f"
	"alse\022\257\001\n\020cc_enable_arenas\030\037 \001(\010:\004trueB\216\001"
	"\262\001\212\001 \352\007*\204\001cc_enable_arenas is enabled by"
	" default in every edition and overrides "
	"are ignored.This option is removed in ed"
	"itions 2026 and above.\022\031\n\021objc_class_pre"
	"fix\030$ \001(\t\022\030\n\020csharp_namespace\030% \001(\t\022\024\n\014s"
	"wift_prefix\030\' \001(\t\022\030\n\020php_class_prefix\030( "
	"\001(\t\022\025\n\rphp_namespace\030) \001(\t\022\036\n\026php_metada"
	"ta_namespace\030, \001(\t\022\024\n\014ruby_package\030- \001(\t"
	"\022-\n\010features\0302 \001(\0132\033.google.protobuf.Fea"
	"tureSet\022C\n\024uninterpreted_option\030\347\007 \003(\0132$"
	".google.protobuf.UninterpretedOption\":\n\014"
	"OptimizeMode\022\t\n\005SPEED\020\001\022\r\n\tCODE_SIZE\020\002\022\020"
	"\n\014LITE_RUNTIME\020\003*\006\010\336\007\020\347\007*\t\010\350\007\020\200\200\200\200\002J\004\010*\020"
	"+J\004\010&\020\'R\024php_generic_services\"\357\002\n\016Messag"
	"eOptions\022&\n\027message_set_wire_format\030\001 \001("
	"\010:\005false\022.\n\037no_standard_descriptor_acces"
	"sor\030\002 \001(\010:\005false\022\031\n\ndeprecated\030\003 \001(\010:\005fa"
	"lse\022\021\n\tmap_entry\030\007 \001(\010\0222\n&deprecated_leg"
	"acy_json_field_conflicts\030\013 \001(\010B\002\030\001\022-\n\010fe"
	"atures\030\014 \001(\0132\033.google.protobuf.FeatureSe"
	"t\022C\n\024uninterpreted_option\030\347\007 \003(\0132$.googl"
	"e.protobuf.UninterpretedOption*\006\010\336\007\020\347\007*\t"
	"\010\350\007\020\200\200\200\200\002J\004\010\004\020\005J\004\010\005\020\006J\004\010\006\020\007J\004\010\010\020\tJ\004\010\t\020\n\""
	"\306\013\n\014FieldOptions\022:\n\005ctype\030\001 \001(\0162#.google"
	".protobuf.FieldOptions.CType:\006STRING\022\016\n\006"
	"packed\030\002 \001(\010\022\?\n\006jstype\030\006 \001(\0162$.google.pr"
	"otobuf.FieldOptions.JSType:\tJS_NORMAL\022\023\n"
	"\004lazy\030\005 \001(\010:\005false\022\036\n\017unverified_lazy\030\017 "
	"\001(\010:\005false\022\031\n\ndeprecated\030\003 \001(\010:\005false\022\027\n"
	"\004weak\030\n \001(\010:\005falseB\002\030\001\022\033\n\014debug_redact\030\020"
	" \001(\010:\005false\022@\n\tretention\030\021 \001(\0162-.google."
	"protobuf.FieldOptions.OptionRetention\022\?\n"
	"\007targets\030\023 \003(\0162..google.protobuf.FieldOp"
	"tions.OptionTargetType\022F\n\020edition_defaul"
	"ts\030\024 \003(\0132,.google.protobuf.FieldOptions."
	"EditionDefault\022-\n\010features\030\025 \001(\0132\033.googl"
	"e.protobuf.FeatureSet\022E\n\017feature_support"
	"\030\026 \001(\0132,.google.protobuf.FieldOptions.Fe"
	"atureSupport\022C\n\024uninterpreted_option\030\347\007 "
	"\003(\0132$.google.protobuf.UninterpretedOptio"
	"n\032J\n\016EditionDefault\022)\n\007edition\030\003 \001(\0162\030.g"
	"oogle.protobuf.Edition\022\r\n\005value\030\002 \001(\t\032\343\001"
	"\n\016FeatureSupport\0224\n\022edition_introduced\030\001"
	" \001(\0162\030.google.protobuf.Edition\0224\n\022editio"
	"n_deprecated\030\002 \001(\0162\030.google.protobuf.Edi"
	"tion\022\033\n\023deprecation_warning\030\003 \001(\t\0221\n\017edi"
	"tion_removed\030\004 \001(\0162\030.google.protobuf.Edi"
	"tion\022\025\n\rremoval_error\030\005 \001(\t\"/\n\005CType\022\n\n\006"
	"STRING\020\000\022\010\n\004CORD\020\001\022\020\n\014STRING_PIECE\020\002\"5\n\006"
	"JSType\022\r\n\tJS_NORMAL\020\000\022\r\n\tJS_STRING\020\001\022\r\n\t"
	"JS_NUMBER\020\002\"U\n\017OptionRetention\022\025\n\021RETENT"
	"ION_UNKNOWN\020\000\022\025\n\021RETENTION_RUNTIME\020\001\022\024\n\020"
	"RETENTION_SOURCE\020\002\"\214\002\n\020OptionTargetType\022"
	"\027\n\023TARGET_TYPE_UNKNOWN\020\000\022\024\n\020TARGET_TYPE_"
	"FILE\020\001\022\037\n\033TARGET_TYPE_EXTENSION_RANGE\020\002\022"
	"\027\n\023TARGET_TYPE_MESSAGE\020\003\022\025\n\021TARGET_TYPE_"
	"FIELD\020\004\022\025\n\021TARGET_TYPE_ONEOF\020\005\022\024\n\020TARGET"
	"_TYPE_ENUM\020\006\022\032\n\026TARGET_TYPE_ENUM_ENTRY\020\007"
	"\022\027\n\023TARGET_TYPE_SERVICE\020\010\022\026\n\022TARGET_TYPE"
	"_METHOD\020\t*\006\010\336\007\020\347\007*\t\010\350\007\020\200\200\200\200\002J\004\010\004\020\005J\004\010\022\020\023"
	"\"\225\001\n\014OneofOptions\022-\n\010features\030\001 \001(\0132\033.go"
	"ogle.protobuf.FeatureSet\022C\n\024uninterprete"
	"d_option\030\347\007 \003(\0132$.google.protobuf.Uninte"
	"rpretedOption*\006\010\336\007\020\347\007*\t\010\350\007\020\200\200\200\200\002\"\376\001\n\013Enu"
	"mOptions\022\023\n\013allow_alias\030\002 \001(\010\022\031\n\ndepreca"
	"ted\030\003 \001(\010:\005false\0222\n&deprecated_legacy_js"
	"on_field_conflicts\030\006 \001(\010B\002\030\001\022-\n\010features"
	"\030\007 \001(\0132\033.google.protobuf.FeatureSet\022C\n\024u"
	"ninterpreted_option\030\347\007 \003(\0132$.google.prot"
	"obuf.UninterpretedOption*\006\010\336\007\020\347\007*\t\010\350\007\020\200\200"
	"\200\200\002J\004\010\005\020\006\"\230\002\n\020EnumValueOptions\022\031\n\ndeprec"
	"ated\030\001 \001(\010:\005false\022-\n\010features\030\002 \001(\0132\033.go"
	"ogle.protobuf.FeatureSet\022\033\n\014debug_redact"
	"\030\003 \001(\010:\005false\022E\n\017feature_support\030\004 \001(\0132,"
	".google.protobuf.FieldOptions.FeatureSup"
	"port\022C\n\024uninterpreted_option\030\347\007 \003(\0132$.go"
	"ogle.protobuf.UninterpretedOption*\006\010\336\007\020\347"
	"\007*\t\010\350\007\020\200\200\200\200\002\"\262\001\n\016ServiceOptions\022-\n\010featu"
	"res\030\" \001(\0132\033.google.protobuf.FeatureSet\022\031"
	"\n\ndeprecated\030! \001(\010:\005false\022C\n\024uninterpret"
	"ed_option\030\347\007 \003(\0132$.google.protobuf.Unint"
	"erpretedOption*\006\010\336\007\020\347\007*\t\010\350\007\020\200\200\200\200\002\"\344\002\n\rMe"
	"thodOptions\022\031\n\ndeprecated\030! \001(\010:\005false\022_"
	"\n\021idempotency_level\030\" \001(\0162/.google.proto"
	"buf.MethodOptions.IdempotencyLevel:\023IDEM"
	"POTENCY_UNKNOWN\022-\n\010features\030# \001(\0132\033.goog"
	"le.protobuf.FeatureSet\022C\n\024uninterpreted_"
	"option\030\347\007 \003(\0132$.google.protobuf.Uninterp"
	"retedOption\"P\n\020IdempotencyLevel\022\027\n\023IDEMP"
	"OTENCY_UNKNOWN\020\000\022\023\n\017NO_SIDE_EFFECTS\020\001\022\016\n"
	"\nIDEMPOTENT\020\002*\006\010\336\007\020\347\007*\t\010\350\007\020\200\200\200\200\002\"\236\002\n\023Uni"
	"nterpretedOption\022;\n\004name\030\002 \003(\0132-.google."
	"protobuf.UninterpretedOption.NamePart\022\030\n"
	"\020identifier_value\030\003 \001(\t\022\032\n\022positive_int_"
	"value\030\004 \001(\004\022\032\n\022negative_int_value\030\005 \001(\003\022"
	"\024\n\014double_value\030\006 \001(\001\022\024\n\014string_value\030\007 "
	"\001(\014\022\027\n\017aggregate_value\030\010 \001(\t\0323\n\010NamePart"
	"\022\021\n\tname_part\030\001 \002(\t\022\024\n\014is_extension\030\002 \002("
	"\010\"\336\020\n\nFeatureSet\022\202\001\n\016field_presence\030\001 \001("
	"\0162).google.protobuf.FeatureSet.FieldPres"
	"enceB\?\210\001\001\230\001\004\230\001\001\242\001\r\022\010EXPLICIT\030\204\007\242\001\r\022\010IMPL"
	"ICIT\030\347\007\242\001\r\022\010EXPLICIT\030\350\007\262\001\003\010\350\007\022b\n\tenum_ty"
	"pe\030\002 \001(\0162$.google.protobuf.FeatureSet.En"
	"umTypeB)\210\001\001\230\001\006\230\001\001\242\001\013\022\006CLOSED\030\204\007\242\001\t\022\004OPEN"
	"\030\347\007\262\001\003\010\350\007\022\201\001\n\027repeated_field_encoding\030\003 "
	"\001(\01621.google.protobuf.FeatureSet.Repeate"
	"dFieldEncodingB-\210\001\001\230\001\004\230\001\001\242\001\r\022\010EXPANDED\030\204"
	"\007\242\001\013\022\006PACKED\030\347\007\262\001\003\010\350\007\022n\n\017utf8_validation"
	"\030\004 \001(\0162*.google.protobuf.FeatureSet.Utf8"
	"ValidationB)\210\001\001\230\001\004\230\001\001\242\001\t\022\004NONE\030\204\007\242\001\013\022\006VE"
	"RIFY\030\347\007\262\001\003\010\350\007\022m\n\020message_encoding\030\005 \001(\0162"
	"+.google.protobuf.FeatureSet.MessageEnco"
	"dingB&\210\001\001\230\001\004\230\001\001\242\001\024\022\017LENGTH_PREFIXED\030\204\007\262\001"
	"\003\010\350\007\022v\n\013json_format\030\006 \001(\0162&.google.proto"
	"buf.FeatureSet.JsonFormatB9\210\001\001\230\001\003\230\001\006\230\001\001\242"
	"\001\027\022\022LEGACY_BEST_EFFORT\030\204\007\242\001\n\022\005ALLOW\030\347\007\262\001"
	"\003\010\350\007\022\250\001\n\024enforce_naming_style\030\007 \001(\0162..go"
	"ogle.protobuf.FeatureSet.EnforceNamingSt"
	"yleBZ\210\001\002\230\001\001\230\001\002\230\001\003\230\001\004\230\001\005\230\001\006\230\001\007\230\001\010\230\001\t\242\001\021\022\014"
	"STYLE_LEGACY\030\204\007\242\001\016\022\tSTYLE2024\030\351\007\242\001\016\022\tSTY"
	"LE2026\030\352\007\262\001\003\010\351\007\022\256\001\n\031default_symbol_visib"
	"ility\030\010 \001(\0162E.google.protobuf.FeatureSet"
	".VisibilityFeature.DefaultSymbolVisibili"
	"tyBD\210\001\002\230\001\001\242\001\017\022\nEXPORT_ALL\030\204\007\242\001\025\022\020EXPORT_"
	"TOP_LEVEL\030\351\007\242\001\013\022\006STRICT\030\352\007\262\001\003\010\351\007\022\257\001\n\024enf"
	"orce_proto_limits\030\t \001(\0162A.google.protobu"
	"f.FeatureSet.ProtoLimitsFeature.EnforceP"
	"rotoLimitsBN\210\001\002\230\001\006\230\001\003\230\001\004\230\001\005\242\001\036\022\031LEGACY_N"
	"O_EXPLICIT_LIMITS\030\204\007\242\001\025\022\020PROTO_LIMITS202"
	"6\030\352\007\262\001\003\010\352\007\032\241\001\n\021VisibilityFeature\"\201\001\n\027Def"
	"aultSymbolVisibility\022%\n!DEFAULT_SYMBOL_V"
	"ISIBILITY_UNKNOWN\020\000\022\016\n\nEXPORT_ALL\020\001\022\024\n\020E"
	"XPORT_TOP_LEVEL\020\002\022\r\n\tLOCAL_ALL\020\003\022\n\n\006STRI"
	"CT\020\004J\010\010\001\020\200\200\200\200\002\032y\n\022ProtoLimitsFeature\"c\n\022"
	"EnforceProtoLimits\022\030\n\024PROTO_LIMITS_UNKNO"
	"WN\020\000\022\035\n\031LEGACY_NO_EXPLICIT_LIMITS\020\001\022\024\n\020P"
	"ROTO_LIMITS2026\020\002\"\\\n\rFieldPresence\022\032\n\026FI"
	"ELD_PRESENCE_UNKNOWN\020\000\022\014\n\010EXPLICIT\020\001\022\014\n\010"
	"IMPLICIT\020\002\022\023\n\017LEGACY_REQUIRED\020\003\"7\n\010EnumT"
	"ype\022\025\n\021ENUM_TYPE_UNKNOWN\020\000\022\010\n\004OPEN\020\001\022\n\n\006"
	"CLOSED\020\002\"V\n\025RepeatedFieldEncoding\022#\n\037REP"
	"EATED_FIELD_ENCODING_UNKNOWN\020\000\022\n\n\006PACKED"
	"\020\001\022\014\n\010EXPANDED\020\002\"I\n\016Utf8Validation\022\033\n\027UT"
	"F8_VALIDATION_UNKNOWN\020\000\022\n\n\006VERIFY\020\002\022\010\n\004N"
	"ONE\020\003\"\004\010\001\020\001\"S\n\017MessageEncoding\022\034\n\030MESSAG"
	"E_ENCODING_UNKNOWN\020\000\022\023\n\017LENGTH_PREFIXED\020"
	"\001\022\r\n\tDELIMITED\020\002\"H\n\nJsonFormat\022\027\n\023JSON_F"
	"ORMAT_UNKNOWN\020\000\022\t\n\005ALLOW\020\001\022\026\n\022LEGACY_BES"
	"T_EFFORT\020\002\"f\n\022EnforceNamingStyle\022 \n\034ENFO"
	"RCE_NAMING_STYLE_UNKNOWN\020\000\022\r\n\tSTYLE2024\020"
	"\001\022\020\n\014STYLE_LEGACY\020\002\022\r\n\tSTYLE2026\020\003*\006\010\350\007\020"
	"\213N*\006\010\213N\020\220N*\006\010\220N\020\221NJ\006\010\347\007\020\350\007\"\230\003\n\022FeatureSe"
	"tDefaults\022N\n\010defaults\030\001 \003(\0132<.google.pro"
	"tobuf.FeatureSetDefaults.FeatureSetEditi"
	"onDefault\0221\n\017minimum_edition\030\004 \001(\0162\030.goo"
	"gle.protobuf.Edition\0221\n\017maximum_edition\030"
	"\005 \001(\0162\030.google.protobuf.Edition\032\313\001\n\030Feat"
	"ureSetEditionDefault\022)\n\007edition\030\003 \001(\0162\030."
	"google.protobuf.Edition\0229\n\024overridable_f"
	"eatures\030\004 \001(\0132\033.google.protobuf.FeatureS"
	"et\0223\n\016fixed_features\030\005 \001(\0132\033.google.prot"
	"obuf.FeatureSetJ\004\010\001\020\002J\004\010\002\020\003R\010features\"\343\001"
	"\n\016SourceCodeInfo\022:\n\010location\030\001 \003(\0132(.goo"
	"gle.protobuf.SourceCodeInfo.Location\032\206\001\n"
	"\010Location\022\020\n\004path\030\001 \003(\005B\002\020\001\022\020\n\004span\030\002 \003("
	"\005B\002\020\001\022\030\n\020leading_comments\030\003 \001(\t\022\031\n\021trail"
	"ing_comments\030\004 \001(\t\022!\n\031leading_detached_c"
	"omments\030\006 \003(\t*\014\010\200\354\312\377\001\020\201\354\312\377\001\"\234\002\n\021Generate"
	"dCodeInfo\022A\n\nannotation\030\001 \003(\0132-.google.p"
	"rotobuf.GeneratedCodeInfo.Annotation\032\303\001\n"
	"\nAnnotation\022\020\n\004path\030\001 \003(\005B\002\020\001\022\023\n\013source_"
	"file\030\002 \001(\t\022\r\n\005begin\030\003 \001(\005\022\013\n\003end\030\004 \001(\005\022H"
	"\n\010semantic\030\005 \001(\01626.google.protobuf.Gener"
	"atedCodeInfo.Annotation.Semantic\"(\n\010Sema"
	"ntic\022\010\n\004NONE\020\000\022\007\n\003SET\020\001\022\t\n\005ALIAS\020\002*\321\002\n\007E"
	"dition\022\023\n\017EDITION_UNKNOWN\020\000\022\023\n\016EDITION_L"
	"EGACY\020\204\007\022\023\n\016EDITION_PROTO2\020\346\007\022\023\n\016EDITION"
	"_PROTO3\020\347\007\022\021\n\014EDITION_2023\020\350\007\022\021\n\014EDITION"
	"_2024\020\351\007\022\021\n\014EDITION_2026\020\352\007\022\025\n\020EDITION_U"
	"NSTABLE\020\217N\022\027\n\023EDITION_1_TEST_ONLY\020\001\022\027\n\023E"
	"DITION_2_TEST_ONLY\020\002\022\035\n\027EDITION_99997_TE"
	"ST_ONLY\020\235\215\006\022\035\n\027EDITION_99998_TEST_ONLY\020\236"
	"\215\006\022\035\n\027EDITION_99999_TEST_ONLY\020\237\215\006\022\023\n\013EDI"
	"TION_MAX\020\377\377\377\377\007*U\n\020SymbolVisibility\022\024\n\020VI"
	"SIBILITY_UNSET\020\000\022\024\n\020VISIBILITY_LOCAL\020\001\022\025"
	"\n\021VISIBILITY_EXPORT\020\002B~\n\023com.google.prot"
	"obufB\020DescriptorProtosH\001Z-google.golang."
	"org/protobuf/types/descriptorpb\370\001\001\242\002\003GPB"
	"\252\002\032Google.Protobuf.Reflection";

/* cpp_features.proto (src/google/protobuf/cpp_features.pb.cc, 795 bytes) */
static const char lp_embedded_cpp_features[] =
	"\n\"google/protobuf/cpp_features.proto\022\002pb"
	"\032 google/protobuf/descriptor.proto\"\222\005\n\013C"
	"ppFeatures\022\373\001\n\022legacy_closed_enum\030\001 \001(\010B"
	"\336\001\210\001\001\230\001\004\230\001\001\242\001\t\022\004true\030\204\007\242\001\n\022\005false\030\347\007\262\001\270\001"
	"\010\350\007\020\350\007\032\257\001The legacy closed enum behavior"
	" in C++ is deprecated and is scheduled t"
	"o be removed in edition 2025.  See http:"
	"//protobuf.dev/programming-guides/enum/#"
	"cpp for more information\022Z\n\013string_type\030"
	"\002 \001(\0162\032.pb.CppFeatures.StringTypeB)\210\001\001\230\001"
	"\004\230\001\001\242\001\013\022\006STRING\030\204\007\242\001\t\022\004VIEW\030\351\007\262\001\003\010\350\007\022L\n\032"
	"enum_name_uses_string_view\030\003 \001(\010B(\210\001\001\230\001\006"
	"\230\001\001\242\001\n\022\005false\030\204\007\242\001\t\022\004true\030\351\007\262\001\003\010\351\007\022R\n\rre"
	"peated_type\030\004 \001(\0162\034.pb.CppFeatures.Repea"
	"tedTypeB\035\210\001\001\230\001\004\230\001\001\242\001\013\022\006LEGACY\030\204\007\262\001\003\010\217N\"E"
	"\n\nStringType\022\027\n\023STRING_TYPE_UNKNOWN\020\000\022\010\n"
	"\004VIEW\020\001\022\010\n\004CORD\020\002\022\n\n\006STRING\020\003\"@\n\014Repeate"
	"dType\022\031\n\025REPEATED_TYPE_UNKNOWN\020\000\022\n\n\006LEGA"
	"CY\020\001\022\t\n\005PROXY\020\002::\n\003cpp\022\033.google.protobuf"
	".FeatureSet\030\350\007 \001(\0132\017.pb.CppFeatures";

/* java_features.proto (src/google/protobuf/compiler/java/java_features.pb.cc, 1223 bytes) */
static const char lp_embedded_java_features[] =
	"\n1google/protobuf/compiler/java/java_fea"
	"tures.proto\022\002pb\032 google/protobuf/descrip"
	"tor.proto\"\203\010\n\014JavaFeatures\022\376\001\n\022legacy_cl"
	"osed_enum\030\001 \001(\010B\341\001\210\001\001\230\001\004\230\001\001\242\001\t\022\004true\030\204\007\242"
	"\001\n\022\005false\030\347\007\262\001\273\001\010\350\007\020\350\007\032\262\001The legacy clos"
	"ed enum behavior in Java is deprecated a"
	"nd is scheduled to be removed in edition"
	" 2025.  See http://protobuf.dev/programm"
	"ing-guides/enum/#java for more informati"
	"on.\022\237\002\n\017utf8_validation\030\002 \001(\0162\037.pb.JavaF"
	"eatures.Utf8ValidationB\344\001\210\001\001\230\001\004\230\001\001\242\001\014\022\007D"
	"EFAULT\030\204\007\262\001\310\001\010\350\007\020\351\007\032\277\001The Java-specific "
	"utf8 validation feature is deprecated an"
	"d is scheduled to be removed in edition "
	"2025.  Utf8 validation behavior should u"
	"se the global cross-language utf8_valida"
	"tion feature.\0220\n\nlarge_enum\030\003 \001(\010B\034\210\001\001\230\001"
	"\006\230\001\001\242\001\n\022\005false\030\204\007\262\001\003\010\351\007\022Q\n\037use_old_outer"
	"_classname_default\030\004 \001(\010B(\210\001\001\230\001\001\242\001\t\022\004tru"
	"e\030\204\007\242\001\n\022\005false\030\351\007\262\001\006\010\351\007 \351\007\022\177\n\022nest_in_fi"
	"le_class\030\005 \001(\01627.pb.JavaFeatures.NestInF"
	"ileClassFeature.NestInFileClassB*\210\001\001\230\001\003\230"
	"\001\006\230\001\010\242\001\013\022\006LEGACY\030\204\007\242\001\007\022\002NO\030\351\007\262\001\003\010\351\007\032|\n\026N"
	"estInFileClassFeature\"X\n\017NestInFileClass"
	"\022\036\n\032NEST_IN_FILE_CLASS_UNKNOWN\020\000\022\006\n\002NO\020\001"
	"\022\007\n\003YES\020\002\022\024\n\006LEGACY\020\003\032\010\"\006\010\351\007 \351\007J\010\010\001\020\200\200\200\200"
	"\002\"F\n\016Utf8Validation\022\033\n\027UTF8_VALIDATION_U"
	"NKNOWN\020\000\022\013\n\007DEFAULT\020\001\022\n\n\006VERIFY\020\002J\004\010\006\020\007:"
	"<\n\004java\022\033.google.protobuf.FeatureSet\030\351\007 "
	"\001(\0132\020.pb.JavaFeaturesB(\n\023com.google.prot"
	"obufB\021JavaFeaturesProto";

/* c_sharp_features.proto (src/google/protobuf/compiler/csharp/c_sharp_features.pb.cc, 304 bytes) */
static const char lp_embedded_c_sharp_features[] =
	"\n6google/protobuf/compiler/csharp/c_shar"
	"p_features.proto\022\002pb\032 google/protobuf/de"
	"scriptor.proto\"S\n\016CSharpFeatures\022A\n\030null"
	"able_reference_types\030\001 \001(\010B\037\210\001\001\230\001\004\230\001\003\230\001\001"
	"\242\001\n\022\005false\030\204\007\262\001\003\010\352\007:@\n\006csharp\022\033.google.p"
	"rotobuf.FeatureSet\030\354\007 \001(\0132\022.pb.CSharpFea"
	"turesB9B\030CSharpFeaturesOuterClassP\001\252\002\032Go"
	"ogle.Protobuf.Reflection";

/* cpp_file_options.proto (src/google/protobuf/cpp_file_options.pb.cc, 212 bytes) */
static const char lp_embedded_cpp_file_options[] =
	"\n&google/protobuf/cpp_file_options.proto"
	"\022\007pb.file\032 google/protobuf/descriptor.pr"
	"oto\"%\n\016CppFileOptions\022\021\n\tnamespace\030\001 \001(\t"
	"X\001:K\n\003cpp\022\034.google.protobuf.FileOptions\030"
	"\336\007 \001(\0132\027.pb.file.CppFileOptionsB\006\262\001\003\010\352\007b"
	"\010editionsp\351\007";

/* json_enumvalue_options.proto (src/google/protobuf/json_enumvalue_options.pb.cc, 271 bytes) */
static const char lp_embedded_json_enumvalue_options[] =
	"\n,google/protobuf/json_enumvalue_options"
	".proto\022\014pb.enumvalue\032 google/protobuf/de"
	"scriptor.proto\"(\n\024JsonEnumValueOptions\022\016"
	"\n\006string\030\001 \001(\tX\001:\\\n\004json\022!.google.protob"
	"uf.EnumValueOptions\030\346\007 \001(\0132\".pb.enumvalu"
	"e.JsonEnumValueOptionsB\006\262\001\003\010\352\007B\032\n\030com.go"
	"ogle.protobuf.utilb\010editionsp\351\007";

/* plugin.proto (src/google/protobuf/compiler/plugin.pb.cc, 952 bytes) */
static const char lp_embedded_plugin[] =
	"\n%google/protobuf/compiler/plugin.proto\022"
	"\030google.protobuf.compiler\032 google/protob"
	"uf/descriptor.proto\"F\n\007Version\022\r\n\005major\030"
	"\001 \001(\005\022\r\n\005minor\030\002 \001(\005\022\r\n\005patch\030\003 \001(\005\022\016\n\006s"
	"uffix\030\004 \001(\t\"\201\002\n\024CodeGeneratorRequest\022\030\n\020"
	"file_to_generate\030\001 \003(\t\022\021\n\tparameter\030\002 \001("
	"\t\0228\n\nproto_file\030\017 \003(\0132$.google.protobuf."
	"FileDescriptorProto\022E\n\027source_file_descr"
	"iptors\030\021 \003(\0132$.google.protobuf.FileDescr"
	"iptorProto\022;\n\020compiler_version\030\003 \001(\0132!.g"
	"oogle.protobuf.compiler.Version\"\222\003\n\025Code"
	"GeneratorResponse\022\r\n\005error\030\001 \001(\t\022\032\n\022supp"
	"orted_features\030\002 \001(\004\022\027\n\017minimum_edition\030"
	"\003 \001(\005\022\027\n\017maximum_edition\030\004 \001(\005\022B\n\004file\030\017"
	" \003(\01324.google.protobuf.compiler.CodeGene"
	"ratorResponse.File\032\177\n\004File\022\014\n\004name\030\001 \001(\t"
	"\022\027\n\017insertion_point\030\002 \001(\t\022\017\n\007content\030\017 \001"
	"(\t\022\?\n\023generated_code_info\030\020 \001(\0132\".google"
	".protobuf.GeneratedCodeInfo\"W\n\007Feature\022\020"
	"\n\014FEATURE_NONE\020\000\022\033\n\027FEATURE_PROTO3_OPTIO"
	"NAL\020\001\022\035\n\031FEATURE_SUPPORTS_EDITIONS\020\002Br\n\034"
	"com.google.protobuf.compilerB\014PluginProt"
	"osZ)google.golang.org/protobuf/types/plu"
	"ginpb\252\002\030Google.Protobuf.Compiler";


static const char lp_embedded_cpp_edition_defaults[] =
	"\n\'\030\204\007\"\003\302>\000*\035\010\001\020\002\030\002 \003(\0010\0028\002@\001H\001\302>\010\010\001\020\003\030\000 \001\n\'\030\347\007\"\003\302>\000*\035\010\002\020\001\030\001 \002(\0010\0018\002@\001H\001\302>\010\010\000\020\003\030\000 \001\n\'\030\350\007\"\023\010\001\020\001\030\001 \002(\0010\001\302>\004\010\000\020\003*\r8\002@\001H\001\302>\004\030\000 \001\n\'\030\351\007\"\031\010\001\020\001\030\001 \002(\0010\0018\001@\002\302>\006\010\000\020\001\030\001*\007H\001\302>\002 \001\n\'\030\352\007\"\033\010\001\020\001\030\001 \002(\0010\0018\003@\004H\002\302>\006\010\000\020\001\030\001*\005\302>\002 \001\n\'\030\217N\"\035\010\001\020\001\030\001 \002(\0010\0018\003@\004H\002\302>\010\010\000\020\001\030\001 \001*\003\302>\000 \346\007(\352\007";

/* FeatureSetDefaults and FeatureSetEditionDefault, for the bootstrap of the generated pool. */
static const PbField lp_f_fsed[] = {
	{3, PT_ENUM, PL_OPT, NULL, NULL, 0},
	{4, PT_MESSAGE, PL_OPT, &pbd_FeatureSet, NULL, 0},
	{5, PT_MESSAGE, PL_OPT, &pbd_FeatureSet, NULL, 0},
};
static const PbMsgDef lp_d_fsed = {3, lp_f_fsed};
static const PbField lp_f_fsd[] = {
	{1, PT_MESSAGE, PL_REP, &lp_d_fsed, NULL, 0},
	{4, PT_ENUM, PL_OPT, NULL, NULL, 0},
	{5, PT_ENUM, PL_OPT, NULL, NULL, 0},
};
static const PbMsgDef lp_d_fsd = {3, lp_f_fsd};

PbMsg *lp_desc_find(const PbMsg *m, uint32_t num, const char *name)
{
	for (int i = 0; m && i < pb_count(m, num); i++)
		if (str_eq(pb_str(pb_msg_at(m, num, i), 1), name))
			return pb_msg_at(m, num, i);
	return NULL;
}

void lp_desc_remove_if(PbMsg *m, uint32_t num, bool (*drop)(const PbMsg *e, const void *arg), const void *arg)
{
	PbSlot *s = m ? (PbSlot *)pb_slot(m, num) : NULL;
	if (!s)
		return;
	int k = 0;
	for (int i = 0; i < s->n; i++)
		if (!drop(s->v[i].m, arg))
			s->v[k++] = s->v[i];
	s->n = k;
}

/* LpVersion.builtin_file of a version on top of those of its bases. */
static bool version_builtin_file(const LpVersion *version, int i, PbMsg *file)
{
	if (version->base && !version_builtin_file(version->base, i, file))
		return false;
	return version->builtin_file(i, file);
}

/* The generated pool (DescriptorPool::generated_pool() of protoc_lib). The embedded protos are
 * parsed with the descriptor.proto schema of pkg/pb (they are canonical, no unknown fields matter
 * here); another protobuf version edits them (LpVersion.builtin_file). Built once per request
 * (arena memory). */
static LpPool *lp_builtin_pool(const LpVersion *version)
{
	static const struct {
		const char *data;
		size_t n;
	} files[] = {
#define F(x) {x, sizeof(x) - 1}
		F(lp_embedded_descriptor),       F(lp_embedded_cpp_features),
		F(lp_embedded_java_features),    F(lp_embedded_c_sharp_features),
		F(lp_embedded_cpp_file_options), F(lp_embedded_json_enumvalue_options),
		F(lp_embedded_plugin),
#undef F
	};
	PbMsg *defaults = pb_new(&lp_d_fsd);
	if (!pb_decode(defaults, (const uint8_t *)lp_embedded_cpp_edition_defaults,
		       sizeof(lp_embedded_cpp_edition_defaults) - 1))
		fail("libprotoc: invalid edition defaults");
	LpPool *pool = lp_pool_new(NULL, defaults, &pbd_FeatureSet);
	pool->version = version;
	for (size_t i = 0; i < sizeof files / sizeof *files; i++) {
		PbMsg *fp = pb_new(&pbd_FileDescriptorProto);
		Buf log = {0};
		if (!pb_decode(fp, (const uint8_t *)files[i].data, files[i].n))
			fail("libprotoc: invalid builtin descriptor %d", (int)i);
		if (version && !version_builtin_file(version, (int)i, fp))
			continue;
		if (!lp_build_file(pool, fp, &log))
			fail("libprotoc: invalid builtin descriptor %d: %s", (int)i, buf_str(&log));
	}
	return pool;
}
