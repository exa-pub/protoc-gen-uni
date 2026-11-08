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

/* nanopb_generator.py: the command line (optparser, process_cmdline) and main_plugin. */

/* The options of optparser, in the order they were added (with the help option first). */
enum {
	O_HELP, O_VERSION, O_X, O_EXTENSION, O_HEADER_EXT, O_SOURCE_EXT, O_OPTIONS_FILE, O_OPTIONS_PATH,
	O_ERROR_ON_UNMATCHED, O_NO_ERROR_ON_UNMATCHED, O_OUTPUT_DIR, O_GENFORMAT, O_LIBFORMAT,
	O_STRIP_PATH, O_NO_STRIP_PATH, O_CPP_DESCRIPTORS, O_NO_TIMESTAMP, O_TIMESTAMP, O_QUIET,
	O_VERBOSE, O_SETTINGS, O_PROTOC_OPT, O_INSERTION_POINTS, O_C_STYLE,
};
static const PyOption nb_options[] = {
	{"-h --help", false}, {"-V --version", false}, {"-x", true}, {"-e --extension", true},
	{"-H --header-extension", true}, {"-S --source-extension", true}, {"-f --options-file", true},
	{"-I --options-path --proto-path", true}, {"--error-on-unmatched", false},
	{"--no-error-on-unmatched", false}, {"-D --output-dir", true}, {"-Q --generated-include-format", true},
	{"-L --library-include-format", true}, {"--strip-path", false}, {"--no-strip-path", false},
	{"--cpp-descriptors", false}, {"-T --no-timestamp", false}, {"-t --timestamp", false},
	{"-q --quiet", false}, {"-v --verbose", false}, {"-s", true}, {"--protoc-opt", true},
	{"--protoc-insertion-points", false}, {"-C --c-style", false},
};

#define USAGE "Usage: protoc --nanopb_out=outdir [--nanopb_opt=option] ['--nanopb_opt=option with spaces'] file.proto\n"

/* optparser.print_help() with the plugin's usage and epilog (78 columns: no terminal). */
static const char HELP[] = USAGE
	"\n"
	"Options:\n"
	"  -h, --help            show this help message and exit\n"
	"  -V, --version         Show version info and exit (add -v for protoc version\n"
	"                        info)\n"
	"  -x FILE               Exclude file from generated #include list.\n"
	"  -e EXTENSION, --extension=EXTENSION\n"
	"                        Set extension to use instead of '.pb' for generated\n"
	"                        files. [default: .pb]\n"
	"  -H EXTENSION, --header-extension=EXTENSION\n"
	"                        Set extension to use for generated header files.\n"
	"                        [default: .h]\n"
	"  -S EXTENSION, --source-extension=EXTENSION\n"
	"                        Set extension to use for generated source files.\n"
	"                        [default: .c]\n"
	"  -f FILE, --options-file=FILE\n"
	"                        Set name of a separate generator options file.\n"
	"  -I DIR, --options-path=DIR, --proto-path=DIR\n"
	"                        Search path for .options and .proto files. Also\n"
	"                        determines relative paths for output directory\n"
	"                        structure.\n"
	"  --error-on-unmatched  Stop generation if there are unmatched fields in\n"
	"                        options file\n"
	"  --no-error-on-unmatched\n"
	"                        Continue generation if there are unmatched fields in\n"
	"                        options file (default)\n"
	"  -D OUTPUTDIR, --output-dir=OUTPUTDIR\n"
	"                        Output directory of .pb.h and .pb.c files\n"
	"  -Q FORMAT, --generated-include-format=FORMAT\n"
	"                        Set format string to use for including other .pb.h\n"
	"                        files. Value can be 'quote', 'bracket' or a format\n"
	"                        string. [default: #include \"%s\"]\n"
	"  -L FORMAT, --library-include-format=FORMAT\n"
	"                        Set format string to use for including the nanopb pb.h\n"
	"                        header. Value can be 'quote', 'bracket' or a format\n"
	"                        string. [default: #include <%s>]\n"
	"  --strip-path          Strip directory path from #included .pb.h file name\n"
	"  --no-strip-path       Opposite of --strip-path (default since 0.4.0)\n"
	"  --cpp-descriptors     Generate C++ descriptors to lookup by type (e.g.\n"
	"                        pb_field_t for a message)\n"
	"  -T, --no-timestamp    Don't add timestamp to .pb.h and .pb.c preambles\n"
	"                        (default since 0.4.0)\n"
	"  -t, --timestamp       Add timestamp to .pb.h and .pb.c preambles\n"
	"  -q, --quiet           Don't print anything except errors.\n"
	"  -v, --verbose         Print more information.\n"
	"  -s OPTION:VALUE       Set generator option (max_size, max_count etc.).\n"
	"  --protoc-opt=OPTION   Pass an option to protoc when compiling .proto files\n"
	"  --protoc-insertion-points\n"
	"                        Include insertion point comments in output for use by\n"
	"                        custom protoc plugins\n"
	"  -C, --c-style         Use C naming convention.\n"
	"\n"
	"Output will be written to file.pb.h and file.pb.c.\n";

/* An option value as a str without NUL characters. */
static const char *arg_text(Str s)
{
	if (memchr(s.p, 0, s.n))
		fail("unsupported by protoc-gen-uni: a NUL character in the plugin parameter");
	return xstrndup(s.p, s.n);
}

static bool str_contains(Str s, const char *sub)
{
	size_t n = strlen(sub);
	for (size_t i = 0; i + n <= s.n; i++)
		if (!memcmp(s.p + i, sub, n))
			return true;
	return false;
}

typedef enum { RUN_OK, RUN_EXIT0 } RunResult;

static const char F_MAIN_SHLEX_SPLIT[] = NBF(2714, "main_plugin", "args = shlex.split(params)");
static const char F_MAIN_SHLEX_LIST[] = NBF(2722, "main_plugin", "args = list(lex)");

/* main_plugin up to process_cmdline: returns RUN_EXIT0 for -V (an empty response). */
static COLD RunResult parse_parameter(Str params)
{
	req_text(params);
	Str *args;
	const char *err;
	int nargs;
	bool posix = !memchr(params.p, ',', params.n) && str_contains(params, " -");
	nargs = py_shlex(params, posix, posix ? " \t\r\n" : ",", &args, &err);
	if (err) {
		Buf frames = {0};
		if (posix) {
			buf_adds(&frames, F_MAIN_SHLEX_SPLIT);
			buf_adds(&frames, SHLEXF(313, "split", "return list(lex)"));
		} else {
			buf_adds(&frames, F_MAIN_SHLEX_LIST);
		}
		buf_adds(&frames, SHLEXF(300, "__next__", "token = self.get_token()"));
		buf_adds(&frames, SHLEXF(109, "get_token", "raw = self.read_token()"));
		buf_adds(&frames, *err == 'N' && err[3] == 'c'
				? SHLEXF(191, "read_token", "raise ValueError(\"No closing quotation\")")
				: SHLEXF(210, "read_token", "raise ValueError(\"No escaped character\")"));
		nb_raise(buf_str(&frames), xcat("ValueError: ", err, "\n"));
	}
	for (int i = 0; i < nargs; i++)
		if (str_eqc(args[i], "-h") || str_eqc(args[i], "--help"))
			fail_str(str_c(HELP));
	PyOptEvent *ev;
	Str *pos;
	int npos;
	char *perr;
	int nev = py_optparse(nb_options, (int)(sizeof nb_options / sizeof nb_options[0]), args, nargs, &ev, &pos, &npos, &perr);
	cmd = (CmdOpts){
		.extension = ".pb", .header_extension = ".h", .source_extension = ".c",
		.options_file = "%s.options", .genformat = "#include \"%s\"", .libformat = "#include <%s>",
		.notimestamp = true,
	};
	for (int i = 0; i < nev; i++) {
		const char *v = ev[i].value.p ? arg_text(ev[i].value) : NULL;
		switch (ev[i].option) {
		case O_HELP: fail("unsupported by protoc-gen-uni: an abbreviated --help (upstream prints the help on stdout)");
		case O_VERSION: cmd.version = true; break;
		case O_X: vec_push(&cmd.exclude, (void *)v); break;
		case O_EXTENSION: cmd.extension = v; break;
		case O_HEADER_EXT: cmd.header_extension = v; break;
		case O_SOURCE_EXT: cmd.source_extension = v; break;
		case O_OPTIONS_FILE: cmd.options_file = v; break;
		case O_GENFORMAT: cmd.genformat = v; break;
		case O_LIBFORMAT: cmd.libformat = v; break;
		case O_STRIP_PATH: cmd.strip_path = true; break;
		case O_NO_STRIP_PATH: cmd.strip_path = false; break;
		case O_CPP_DESCRIPTORS: cmd.cpp_descriptors = true; break;
		case O_NO_TIMESTAMP: cmd.notimestamp = true; break;
		case O_TIMESTAMP: cmd.notimestamp = false; break;
		case O_QUIET: cmd.quiet = true; break;
		case O_VERBOSE: cmd.verbose = true; break;
		case O_SETTINGS: vec_push(&cmd.settings, (void *)v); break;
		case O_INSERTION_POINTS: protoc_insertion_points = true; break;
		case O_C_STYLE: c_style = true; break;
		default: break; /* -I, -D, --protoc-opt, --[no-]error-on-unmatched: no files are read or written */
		}
	}
	if (perr)
		fail_str(str_c(xcat(USAGE, "\nprotoc-gen-nanopb: error: ", perr, "\n")));
	if (cmd.version)
		return RUN_EXIT0;
	if (cmd.quiet)
		cmd.verbose = false;
	if (str_eq(cmd.libformat, "quote"))
		cmd.libformat = "#include \"%s\"";
	else if (str_eq(cmd.libformat, "bracket"))
		cmd.libformat = "#include <%s>";
	if (str_eq(cmd.genformat, "quote"))
		cmd.genformat = "#include \"%s\"";
	else if (str_eq(cmd.genformat, "bracket"))
		cmd.genformat = "#include <%s>";
	return RUN_OK;
}

static const char S_MAIN_INDEX[] = NBF(2741, "main_plugin",
	"options.options_path.append(os.path.dirname(request.file_to_generate[0]))")
	"                                                ~~~~~~~~~~~~~~~~~~~~~~~~^^^\n"
	PBINTERNAL("containers") "83, in __getitem__\n"
	"    return self._values[key]\n"
	"           ~~~~~~~~~~~~^^^^^\n";

static void merge_settings(void);

/* main_plugin after the parameter: the response files. */
static COLD void run_plugin(const PbMsg *req, Buf *files)
{
	if (!pb_count(req, CGR_FILE_TO_GENERATE))
		nb_raise(S_MAIN_INDEX, "IndexError: list index out of range\n");
	int n = pb_count(req, CGR_PROTO_FILE);
	FileDesc **fds = xalloc(sizeof *fds * (size_t)(n + 1));
	for (int i = 0; i < n; i++)
		fds[i] = read_file(pb_msg_at(req, CGR_PROTO_FILE, i));
	for (int i = 0; i < pb_count(req, CGR_FILE_TO_GENERATE); i++)
		req_text(pb_bytes_at(req, CGR_FILE_TO_GENERATE, i));
	/* long names of enums without long_names anywhere (see add_dependency) */
	toplevel_options = opts_new();
	toplevel_options->has = 0;
	for (int i = 0; i < n; i++) {
		TB_PUSH(F_MAIN_PARSE);
		if (!i)
			merge_settings();
		ProtoFile *pf = parse_file(fds[i]);
		TB_POP();
		map_put(&other_files, fds[i]->name, pf);
		for (int k = 0; k < pf->enums.n; k++) {
			Enum *e = VEC_AT(pf->enums, Enum, k);
			if (!opt(e->options, NO_LONG_NAMES))
				for (int j = 0; j < e->n; j++)
					map_put(&all_longnames, names_key(e->longnames[j]), (void *)1);
		}
	}
	Map byname = {0}; /* name -> Vec of the indexes of the files of that name */
	for (int k = 0; k < n; k++) {
		Vec *v = map_get(&byname, fds[k]->name);
		if (!v) {
			v = xalloc(sizeof *v);
			map_put(&byname, fds[k]->name, v);
		}
		vec_push(v, fds[k]);
	}
	for (int i = 0; i < pb_count(req, CGR_FILE_TO_GENERATE); i++) {
		const Vec *v = map_get(&byname, pb_str_at(req, CGR_FILE_TO_GENERATE, i));
		for (int k = 0; v && k < v->n; k++) {
			TB_PUSH(F_MAIN_PROCESS);
			process_file(v->v[k], files);
			TB_POP();
		}
	}
}
