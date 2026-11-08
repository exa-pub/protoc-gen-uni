/* ts-poet 6.12.0 (github.com/stephenh/ts-poet, Apache-2.0) for generators that build the
 * formatter's nodes (pkg/languages/ecmascript/formatters/dprint-typescript) instead of text:
 * symbol specs and imports (Import.js), definitions (Def), conditional outputs
 * (ConditionalOutput.js) and the assembly of a file in Code.toString (imports collected
 * breadth-first, aliases on collisions, emitImports, the prefix, dprint).
 *
 * ts-poet's Code is a template whose placeholders are imports, defs, conditional outputs and
 * nested Codes; where it visits them (deepFindAll, breadth-first) decides import aliases and
 * which conditional outputs are used. TpCode mirrors one Code: its items are the placeholders
 * that matter (imports, defs, conditional outputs, nested TpCodes) in placeholder order, and it
 * carries the nodes the Code stands for (node or list). A generator mirrors its upstream's Code
 * nesting with TpCodes and builds nodes that refer to imports through tp_ref/tp_tref, whose
 * identifiers get their final names (aliases) when the file is assembled. */
#ifndef TS_POET_H
#define TS_POET_H

#include "dprint_typescript.h"

enum { TP_CODE = 1, TP_IMPORT, TP_DEF, TP_COND };
/* Import kinds: Implicit, ImportsName (`@`), ImportsDefault (`=`), ImportsAll (`*`), SideEffect (`+`) */
enum { IMP_IMPLICIT, IMP_NAME, IMP_DEFAULT, IMP_ALL, IMP_SIDE_EFFECT };

typedef struct TpCode TpCode;

typedef struct {
	uint8_t tp; /* TP_IMPORT */
	uint8_t kind;
	bool type_import;
	const char *symbol;	   /* changed by aliasing (shared imports keep it across files, like upstream) */
	const char *source_symbol; /* `Foo` of `Foo as Foo1` */
	const char *source;	   /* module; NULL for Implicit */
	const char *child_symbol;  /* `Transaction` of `Knex.Transaction@knex` */
	const char *defined_in;
} TpImport;

typedef struct {
	uint8_t tp; /* TP_DEF */
	const char *symbol;
} TpDef;

typedef struct {
	uint8_t tp; /* TP_COND */
	bool is_type;
	const char *name; /* usage site name */
	TpCode *decl;	  /* declaration site code (mirror) */
	unsigned used;	  /* file number in which it was found used (tp_find_used) */
} TpCond;

struct TpCode {
	uint8_t tp; /* TP_CODE */
	int n, cap;
	void **v;      /* TpCode, TpImport, TpDef or TpCond */
	Node *node;    /* what the code stands for (expression, type, statement, ...) */
	NodeList list; /* or several nodes (statements, members) */
	/* Code.toCodeString's cache (tp_cached_text): the import names of its first printing */
	bool cached;
	int ncache;
	const char **cache;
};

/* imp(spec) / Import.from: `Name@module`, `t:Name@module`, `Src:Name@module`, `Name.Child@module`,
 * `Name*module`, `Name=module`, `Name+module`; anything else is an Implicit symbol. */
TpImport *tp_imp(const char *spec);
TpCode *tp_code(Node *node);
/* Adds a placeholder (TpCode, TpImport, TpDef, TpCond; NULL is ignored); returns it. */
void *tp_add(TpCode *c, void *item);
/* Adds a nested code and returns a copy of its node. */
Node *tp_sub(TpCode *c, TpCode *child);
/* dts_clone that keeps the copies of import identifiers tied to their imports. */
Node *tp_clone(const Node *n);
/* A copy of a type name (`Foo`, `ns.Foo`, keyword) as an expression (`Foo.decode(...)`). */
Node *tp_expr(const Node *type);
/* joinCode(chunks): one code level holding the chunks. */
TpCode *tp_join(TpCode *const *chunks, int n);
TpCond *tp_cond(const char *name, TpCode *decl, bool is_type);
TpDef *tp_def(const char *symbol);

/* Nodes for placeholders, added to c: the identifier of an import (`symbol` or
 * `symbol.child`), as an expression or as a type reference with type arguments; a def; the
 * usage name of a conditional output. */
Node *tp_ref(TpCode *c, TpImport *imp);
Node *tp_tref(TpCode *c, TpImport *imp, NodeList targs);
Node *tp_entity(TpCode *c, TpImport *imp); /* type-position name (Ident or TsQualifiedName) */
Node *tp_defn(TpCode *c, TpDef *def);
Node *tp_co(TpCode *c, TpCond *co);
Node *tp_co_type(TpCode *c, TpCond *co, NodeList targs);

/* Code.toString(opts) of the root code. body: the statements of the root code; trailing: comments
 * after the last statement. prefix: the comments of opts.prefix. Returns the formatted file. */
typedef struct {
	const char *path;		/* opts.path */
	const NodeComment *prefix;	/* opts.prefix as comments (NULL: none) */
	int nprefix;
	const char *const *force_require; /* opts.forceRequireImport */
	int nforce_require;
	/* When dprint cannot parse the code (maybePretty fails: a reserved word as a name), ts-poet
	 * returns the unformatted text: raw_prefix + "\n" + the imports + "\n" + what raw_body writes
	 * (the text of the generator's templates). Without raw_body, or when an import needed an
	 * alias, that is an "unsupported" error. */
	const char *raw_prefix;
	void (*raw_body)(void *arg, Buf *out);
	void *raw_arg;
} TpOpts;
/* Starts a file: uses of imports recorded by tp_ref & co. belong to it. Also call it at the start
 * of a request, before any tp_ref (the arena may have been reset since the last one). */
void tp_file_begin(void);
void tp_to_string(TpCode *root, NodeList body, const NodeComment *trailing, int ntrailing, const TpOpts *opts,
		  Buf *out);
/* Code.toCodeString caches the text of a code in the code. A code that several files share (the
 * declaration of a helper, made once per request) prints in every file as in the first file that
 * printed it, with that file's import names (aliases) even where a later file aliases the import
 * differently. A generator that builds the nodes of such a code again for each file calls
 * tp_cached_text(shared, from) after building them, `from` being tp_uses_mark() before: when the
 * file is printed, the uses recorded since then take the names of the first printing of `shared`
 * (or define them, in the first file). */
int tp_uses_mark(void);
void tp_cached_text(TpCode *shared, int from);
/* deepFindAll's `used`: marks the conditional outputs reachable from root in the current file.
 * The `ifUsed` declarations (MaybeOutput) that a generator appends after its other chunks are
 * output for the marked ones (they add nothing reachable that is not already marked). */
void tp_find_used(TpCode *root);
bool tp_used(const TpCond *co);

/* sameModule of Import.js */
bool tp_same_module(const char *path1, const char *path2);

#endif
