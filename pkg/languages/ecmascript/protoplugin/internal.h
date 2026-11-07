/* Shared between the files of this library only. */
#ifndef PROTOPLUGIN_INTERNAL_H
#define PROTOPLUGIN_INTERNAL_H

#include "protoplugin.h"

/* parameter.c */
NORETURN COLD void plugin_option_error(Str option, const char *detail);
COLD void parse_parameter(Schema *s, const EsPlugin *p, Str parameter);
bool has_target(const EsOptions *o, int target);

/* schema.c */
COLD void create_schema(Schema *s, PbMsg *req);

/* runtime_imports.c */
ImportSymbol *new_import_symbol(const char *name, const char *from, bool type_only);
COLD RuntimeImports *create_runtime_imports(bool bootstrap_wkt);

/* import_path.c */
bool is_relative_path(const char *p);
/* Matches the pattern of rewrite_imports (glob false) or map_imports (glob true). */
bool import_pattern_match(const char *pattern, const char *s, bool glob);
const char *rewrite_import_path(const Schema *s, const char *path);
const char *derive_import_path(const char *filename);
const char *make_import_path_relative(const char *importer, const char *path);

/* names.c, safe_identifier.c */
const char *safe_identifier(const char *name);
void all_names(DescFile *file);
const char *generate_file_path(const Schema *s, const DescFile *file);

/* source_code_info.c */
typedef struct {
	Str *leading_detached;
	int n_leading_detached;
	Str leading, trailing; /* p == NULL if absent */
} Comments;
Comments get_comments(const Desc *d);
Comments get_syntax_comments(const DescFile *f);
Comments get_package_comments(const DescFile *f);
Vec feature_option_strings(const Desc *d); /* const char * */
Str declaration_string(const Desc *d);

/* jsdoc.c */
Str jsdoc_text_from_desc(const Desc *d);
Str format_jsdoc_block(Str text, const char *indent);

/* file_preamble.c */
Str make_file_preamble(const Schema *s, const DescFile *file);
const char *edition_to_string(int64_t edition);

/* generated_file.c */
Str escape_string(Str s); /* a string literal */
Str generated_file_content(GeneratedFile *f);

/* collate.c */
int locale_compare(const char *a, const char *b);

#endif
