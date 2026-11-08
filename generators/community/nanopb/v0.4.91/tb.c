/* Copyright (c) 2011 Petteri Aimonen <jpa at nanopb.mail.kapsi.fi>
 *
 * This software is provided 'as-is', without any express or
 * implied warranty. In no event will the authors be held liable
 * for any damages arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any
 * purpose, including commercial applications, and to alter it and
 * redistribute it freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you
 *    must not claim that you wrote the original software. If you use
 *    this software in a product, an acknowledgment in the product
 *    documentation would be appreciated but is not required.
 *
 * 2. Altered source versions must be plainly marked as such, and
 *    must not be misrepresented as being the original software.
 *
 * 3. This notice may not be removed or altered from any source
 *    distribution. */

/* Python exceptions of the generator: an uncaught exception prints a traceback after everything
 * the run wrote to stderr and exits with status 1, which is the error upstream reports. The
 * frames of the call stack are pushed at the call sites that can raise (TB_PUSH/TB_POP), with
 * the file, line, function, source line and caret line that CPython 3.13 prints for them. */

/* Frames start with a code for the file: \001 nanopb_generator.py, \002 shlex.py, \003 a module
 * of google/protobuf/internal (followed by its name), expanded by nb_raise. */
#define NBPY "/app/lib/python3.13/site-packages/nanopb/generator/nanopb_generator.py"
#define NBF(line, func, src) "\001" #line ", in " func "\n    " src "\n"
#define SHLEXF(line, func, src) "\002" #line ", in " func "\n    " src "\n"
#define PBINTERNAL(module) "\003" module ".py\", line "

static COLD void add_expanded(Buf *b, const char *s)
{
	for (; *s; s++) {
		if (*s == 1)
			buf_adds(b, "  File \"" NBPY "\", line ");
		else if (*s == 2)
			buf_adds(b, "  File \"/usr/lib/python3.13/shlex.py\", line ");
		else if (*s == 3)
			buf_adds(b, "  File \"/app/lib/python3.13/site-packages/google/protobuf/internal/");
		else
			buf_addc(b, *s);
	}
}

static const char *tb_stack[128];
static int tb_depth;

#define TB_PUSH(f) (tb_stack[tb_depth < 128 ? tb_depth : 127] = (f), tb_depth++)
#define TB_POP() (tb_depth--)

/* Raises an exception: site is the frame of the raising line, exc the last line ("Type: msg"),
 * chained the tracebacks of the exceptions it occurred during (or NULL; already expanded). */
static COLD const char *nb_expand(const char *s)
{
	Buf b = {0};
	add_expanded(&b, s);
	return buf_str(&b);
}

NORETURN COLD static void nb_raise_chained(const char *chained, const char *site, const char *exc)
{
	Buf b = {0};
	if (chained)
		buf_adds(&b, chained);
	buf_adds(&b, "Traceback (most recent call last):\n");
	buf_adds(&b, "  File \"/app/bin/protoc-gen-nanopb\", line 6, in <module>\n"
		     "    sys.exit(main_plugin())\n"
		     "             ~~~~~~~~~~~^^\n");
	for (int i = 0; i < tb_depth && i < 128; i++)
		add_expanded(&b, tb_stack[i]);
	if (site)
		add_expanded(&b, site);
	buf_adds(&b, exc);
	fail_str(buf_view(&b));
}

NORETURN COLD static void nb_raise(const char *site, const char *exc)
{
	nb_raise_chained(NULL, site, exc);
}

/* Frames of the call sites. */
static const char F_MAIN_PARSE[] = NBF(2747, "main_plugin",
	"other_files[fdesc.name] = parse_file(fdesc.name, fdesc, options)")
	"                              ~~~~~~~~~~^^^^^^^^^^^^^^^^^^^^^^^^^^^^\n";
static const char F_MAIN_PROCESS[] = NBF(2752, "main_plugin",
	"results = process_file(filename, fdesc, options, other_files)");
static const char F_PARSE_FILE_MERGE[] = NBF(2528, "parse_file", "text_format.Merge(s, toplevel_options)")
	"    ~~~~~~~~~~~~~~~~~^^^^^^^^^^^^^^^^^^^^^\n";
static const char F_PARSE_FILE_PROTOFILE[] = NBF(2564, "parse_file", "f = ProtoFile(fdesc, file_options)");
static const char F_PROCESS_HEADER[] = NBF(2609, "process_file",
	"headerdata = ''.join(f.generate_header(includes, headerbasename, options))");
static const char F_PROCESS_SOURCE[] = NBF(2610, "process_file",
	"sourcedata = ''.join(f.generate_source(headerbasename, options))");
static const char F_PROTOFILE_PARSE[] = NBF(1900, "__init__", "self.parse()") "    ~~~~~~~~~~^^\n";
static const char F_PARSE_MANGLE[] = NBF(1915, "parse",
	"self.manglenames = MangleNames(self.fdesc, self.file_options)")
	"                       ~~~~~~~~~~~^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^\n";
static const char F_PARSE_MSG1[] = NBF(1948, "parse",
	"msgobject = Message(name, message, message_options, comment_path, self.comment_locations)");
static const char F_PARSE_MSG2[] = NBF(1953, "parse",
	"msgobject = Message(name, message, message_options, comment_path, self.comment_locations)");
static const char F_PARSE_MANGLE_FIELD[] = NBF(1945, "parse",
	"field.type_name = self.manglenames.mangle_field_typename(field.type_name)")
	"                      ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~^^^^^^^^^^^^^^^^^\n";
static const char F_PARSE_MANGLE_EXT[] = NBF(1969, "parse",
	"extension.type_name = self.manglenames.mangle_field_typename(extension.type_name)")
	"                          ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~^^^^^^^^^^^^^^^^^^^^^\n";
static const char F_PARSE_EXT[] = NBF(1972, "parse",
	"self.extensions.append(ExtensionField(name, extension, field_options))")
	"                           ~~~~~~~~~~~~~~^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^\n";
static const char F_MESSAGE_LOAD[] = NBF(1325, "__init__", "self.load_fields(desc, message_options)")
	"    ~~~~~~~~~~~~~~~~^^^^^^^^^^^^^^^^^^^^^^^\n";
static const char F_LOAD_FIELD[] = NBF(1366, "load_fields",
	"field = Field(self.name, f, field_options, self.element_path + (ProtoElement.FIELD, index), self.comments)");
static const char F_EXT_EXTENDEE[] = NBF(1149, "__init__",
	"self.extendee_name = names_from_type_name(desc.extendee)")
	"                         ~~~~~~~~~~~~~~~~~~~~^^^^^^^^^^^^^^^\n";
static const char F_EXT_FIELD[] = NBF(1150, "__init__",
	"Field.__init__(self, self.fullname + \"extmsg\", desc, field_options)")
	"    ~~~~~~~~~~~~~~^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^\n";
static const char F_FIELD_ENUM_NAME[] = NBF(728, "__init__", "self.ctype = names_from_type_name(desc.type_name)")
	"                 ~~~~~~~~~~~~~~~~~~~~^^^^^^^^^^^^^^^^\n";
static const char F_FIELD_MSG_NAME[] = NBF(760, "__init__",
	"self.ctype = self.submsgname = names_from_type_name(desc.type_name)")
	"                                   ~~~~~~~~~~~~~~~~~~~~^^^^^^^^^^^^^^^^\n";
#define SORT_CARETS "               ~~~~~~~~~~~~~~~~~^^^^^^^^^^^^^^^\n"
static const char F_GEN_SORT1[] = NBF(2087, "generate_header", "for msg in sort_dependencies(self.messages):") SORT_CARETS;
static const char F_GEN_SORT2[] = NBF(2130, "generate_header", "for msg in sort_dependencies(self.messages):") SORT_CARETS;
static const char F_GEN_AUX[] = NBF(2105, "generate_header", "yield enum.auxiliary_defines() + '\\n'")
	"          ~~~~~~~~~~~~~~~~~~~~~~^^\n";
static const char F_GEN_FIELDS_DECL[] = NBF(2139, "generate_header",
	"yield msg.fields_declaration(self.dependencies) + '\\n'")
	"          ~~~~~~~~~~~~~~~~~~~~~~^^^^^^^^^^^^^^^^^^^\n";
static const char F_GEN_MSGSIZE[] = NBF(2155, "generate_header",
	"messagesizes.append((identifier, msg.encoded_size(self.dependencies)))")
	"                                     ~~~~~~~~~~~~~~~~^^^^^^^^^^^^^^^^^^^\n";
static const char F_GEN_ONEOFSIZE[] = NBF(2166, "generate_header", "msize = f.encoded_size(self.dependencies)");
static const char F_DECL_DEFVAL[] = NBF(1517, "fields_declaration", "defval = self.default_value(dependencies)");

/* The raising lines. */
static const char S_ARRAY_DECL[] = NBF(737, "__init__", "self.array_decl += '[%d]' % self.max_size")
	"                       ~~~~~~~^~~~~~~~~~~~~~~\n";
static const char S_BYTES_NONE[] = NBF(757, "__init__", "self.enc_size = varint_max_size(self.max_size) + self.max_size")
	"                    ~~~~~~~~~~~~~~~^^^^^^^^^^^^^^^\n"
	NBF(265, "varint_max_size", "if max_value < 0:")
	"       ^^^^^^^^^^^^^\n";
static const char S_NOTIMPL_TYPE[] = NBF(765, "__init__", "raise NotImplementedError(desc.type)");
static const char S_NOTIMPL_ALLOC[] = NBF(713, "__init__", "raise NotImplementedError(field_options.type)");
static const char S_STATIC[] = "\001" "699, in __init__\n"
	"    raise Exception(\"Field '%s' is defined as static, but max_size or \"\n"
	"                    \"max_count is not given.\" % self.name)\n";
static const char S_FIXCOUNT[] = "\001" "703, in __init__\n"
	"    raise Exception(\"Field '%s' is defined as fixed count, \"\n"
	"                    \"but max_count is not given.\" % self.name)\n";
static const char S_FIXLEN[] = "\001" "746, in __init__\n"
	"    raise Exception(\"Field '%s' is defined as fixed length, \"\n"
	"                    \"but max_size is not given.\" % self.name)\n";
static const char S_ABS_NAME[] = NBF(260, "names_from_type_name",
	"raise NotImplementedError(\"Lookup of non-absolute type names is not supported\")");
static const char S_ABS_INDEX[] = NBF(259, "names_from_type_name", "if type_name[0] != '.':")
	"       ~~~~~~~~~^^^\n";
static const char S_VARINT[] = NBF(270, "varint_max_size",
	"raise ValueError(\"Value too large for varint: \" + str(max_value))");
static const char S_SORT_REMOVE[] = NBF(1788, "sort_dependencies", "remainset.remove(str(candidate.name))")
	"    ~~~~~~~~~~~~~~~~^^^^^^^^^^^^^^^^^^^^^\n";
static const char S_SORT_REMOVE2[] = NBF(1794, "sort_dependencies", "remainset.remove(str(candidate.name))")
	"    ~~~~~~~~~~~~~~~~^^^^^^^^^^^^^^^^^^^^^\n";
static const char S_AUX_INDEX[] = NBF(496, "auxiliary_defines", "Globals.naming_style.enum_entry(sorted_values[0][0]))")
	"                                    ~~~~~~~~~~~~~^^^\n";
/* get_initializer and default_value with bad default values */
static const char F_GEN_INIT_DEFAULT[] = NBF(2115, "generate_header",
	"yield '#define %-40s %s\\n' % (identifier, msg.get_initializer(False))")
	"                                              ~~~~~~~~~~~~~~~~~~~^^^^^^^\n";
static const char F_MSG_GETINIT[] = NBF(1448, "get_initializer", "parts.append(field.get_initializer(null_init))")
	"                 ~~~~~~~~~~~~~~~~~~~~~^^^^^^^^^^^\n";
static const char F_ONEOF_GETINIT_CB[] = NBF(1255, "get_initializer",
	"return '{{NULL}, NULL}, 0, {' + self.fields[0].get_initializer(null_init) + '}'")
	"                                     ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~^^^^^^^^^^^\n";
static const char F_ONEOF_GETINIT[] = NBF(1257, "get_initializer",
	"return '0, {' + self.fields[0].get_initializer(null_init) + '}'")
	"                     ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~^^^^^^^^^^^\n";
static const char S_INIT_BYTES[] = NBF(870, "get_initializer", "data = codecs.escape_decode(self.default)[0]")
	"           ~~~~~~~~~~~~~~~~~~~~^^^^^^^^^^^^^^\n";
static const char S_INIT_FIXED_BYTES[] = NBF(877, "get_initializer", "data = codecs.escape_decode(self.default)[0]")
	"           ~~~~~~~~~~~~~~~~~~~~^^^^^^^^^^^^^^\n";
static const char S_DEFVAL_BYTES[] = NBF(1709, "default_value",
	"setattr(msg, field.name, codecs.escape_decode(field.default_value)[0])")
	"                             ~~~~~~~~~~~~~~~~~~~~^^^^^^^^^^^^^^^^^^^^^\n";
static const char S_DEFVAL_FLOAT[] = NBF(1711, "default_value", "setattr(msg, field.name, float(field.default_value))")
	"                             ~~~~~^^^^^^^^^^^^^^^^^^^^^\n";
static const char S_DEFVAL_INT[] = NBF(1715, "default_value", "setattr(msg, field.name, int(field.default_value))")
	"                             ~~~^^^^^^^^^^^^^^^^^^^^^\n";
static const char S_DEFVAL_RANGE[] = NBF(1715, "default_value", "setattr(msg, field.name, int(field.default_value))")
	"    ~~~~~~~^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^\n"
	PBINTERNAL("python_message") "826, in field_setter\n"
	"    new_value = type_checker.CheckValue(new_value)\n"
	PBINTERNAL("type_checkers") "180, in CheckValue\n"
	"    raise ValueError('Value out of range: %d' % proposed_value)\n";
static const char C_ENUM_KEYERROR[] = "Traceback (most recent call last):\n"
	NBF(1671, "default_value", "enumtype = dependencies[str(enumname)]")
	"               ~~~~~~~~~~~~^^^^^^^^^^^^^^^\n";

static const char S_ENUM_NOT_FOUND[] = "\001" "1673, in default_value\n"
	"    raise Exception(\"Could not find enum type %s while generating default values for %s.\\n\" % (enumname, self.name)\n"
	"                    + \"Try passing all source files to generator at once, or use -I option.\")\n";
static const char S_ENUM_EXPECTED[] = NBF(1677, "default_value",
	"raise Exception(\"Expected enum type as %s, got %s\" % (enumname, repr(enumtype)))");
static const char S_INITIALS[] = NBF(1830, "__init__", "self.replacement_prefix += part[0]")
	"                               ~~~~^^^\n";
