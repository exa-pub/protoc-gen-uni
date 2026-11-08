/* sys.stdlib_module_names of CPython 3.13 (Python/stdlib_module_names.h of v3.13.5; the list is
 * the same in every 3.13 release). */
#include "python.h"

#include <string.h>

/* Sorted, separated by NUL. */
static const char names[] =
	"__future__\0_abc\0_aix_support\0_android_support\0_apple_support\0_ast\0_asyncio\0_bisect\0"
	"_blake2\0_bz2\0_codecs\0_codecs_cn\0_codecs_hk\0_codecs_iso2022\0_codecs_jp\0_codecs_kr\0"
	"_codecs_tw\0_collections\0_collections_abc\0_colorize\0_compat_pickle\0_compression\0"
	"_contextvars\0_csv\0_ctypes\0_curses\0_curses_panel\0_datetime\0_dbm\0_decimal\0_elementtree\0"
	"_frozen_importlib\0_frozen_importlib_external\0_functools\0_gdbm\0_hashlib\0_heapq\0_imp\0"
	"_interpchannels\0_interpqueues\0_interpreters\0_io\0_ios_support\0_json\0_locale\0_lsprof\0"
	"_lzma\0_markupbase\0_md5\0_multibytecodec\0_multiprocessing\0_opcode\0_opcode_metadata\0"
	"_operator\0_osx_support\0_overlapped\0_pickle\0_posixshmem\0_posixsubprocess\0_py_abc\0"
	"_pydatetime\0_pydecimal\0_pyio\0_pylong\0_pyrepl\0_queue\0_random\0_scproxy\0_sha1\0_sha2\0"
	"_sha3\0_signal\0_sitebuiltins\0_socket\0_sqlite3\0_sre\0_ssl\0_stat\0_statistics\0_string\0"
	"_strptime\0_struct\0_suggestions\0_symtable\0_sysconfig\0_thread\0_threading_local\0_tkinter\0"
	"_tokenize\0_tracemalloc\0_typing\0_uuid\0_warnings\0_weakref\0_weakrefset\0_winapi\0_wmi\0"
	"_zoneinfo\0abc\0antigravity\0argparse\0array\0ast\0asyncio\0atexit\0base64\0bdb\0binascii\0"
	"bisect\0builtins\0bz2\0cProfile\0calendar\0cmath\0cmd\0code\0codecs\0codeop\0collections\0"
	"colorsys\0compileall\0concurrent\0configparser\0contextlib\0contextvars\0copy\0copyreg\0csv\0"
	"ctypes\0curses\0dataclasses\0datetime\0dbm\0decimal\0difflib\0dis\0doctest\0email\0encodings\0"
	"ensurepip\0enum\0errno\0faulthandler\0fcntl\0filecmp\0fileinput\0fnmatch\0fractions\0ftplib\0"
	"functools\0gc\0genericpath\0getopt\0getpass\0gettext\0glob\0graphlib\0grp\0gzip\0hashlib\0"
	"heapq\0hmac\0html\0http\0idlelib\0imaplib\0importlib\0inspect\0io\0ipaddress\0itertools\0"
	"json\0keyword\0linecache\0locale\0logging\0lzma\0mailbox\0marshal\0math\0mimetypes\0mmap\0"
	"modulefinder\0msvcrt\0multiprocessing\0netrc\0nt\0ntpath\0nturl2path\0numbers\0opcode\0"
	"operator\0optparse\0os\0pathlib\0pdb\0pickle\0pickletools\0pkgutil\0platform\0plistlib\0"
	"poplib\0posix\0posixpath\0pprint\0profile\0pstats\0pty\0pwd\0py_compile\0pyclbr\0pydoc\0"
	"pydoc_data\0pyexpat\0queue\0quopri\0random\0re\0readline\0reprlib\0resource\0rlcompleter\0"
	"runpy\0sched\0secrets\0select\0selectors\0shelve\0shlex\0shutil\0signal\0site\0smtplib\0"
	"socket\0socketserver\0sqlite3\0sre_compile\0sre_constants\0sre_parse\0ssl\0stat\0statistics\0"
	"string\0stringprep\0struct\0subprocess\0symtable\0sys\0sysconfig\0syslog\0tabnanny\0tarfile\0"
	"tempfile\0termios\0textwrap\0this\0threading\0time\0timeit\0tkinter\0token\0tokenize\0"
	"tomllib\0trace\0traceback\0tracemalloc\0tty\0turtle\0turtledemo\0types\0typing\0unicodedata\0"
	"unittest\0urllib\0uuid\0venv\0warnings\0wave\0weakref\0webbrowser\0winreg\0winsound\0wsgiref\0"
	"xml\0xmlrpc\0zipapp\0zipfile\0zipimport\0zlib\0zoneinfo\0";

bool py_is_stdlib_module(Str name)
{
	/* linear over 290 names, once per imported module */
	for (const char *p = names; p < names + sizeof names - 1; p += strlen(p) + 1)
		if (str_eqc(name, p))
			return true;
	return false;
}
