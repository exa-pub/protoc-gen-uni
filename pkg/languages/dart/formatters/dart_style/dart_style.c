/* Unity build of the dart_style 3.1.13 port: every file is included here and everything except
 * the API of dart_style.h is static. Order follows the dependencies. */
#include <stdarg.h>

#include "dart_style.h"
#include "internal.h"

#include "collections.c"
#include "nodes.c"
#include "ast_extensions.c"
#include "short/rule.c"
#include "short/chunk.c"
#include "short/line_splitter.c"
#include "short/line_writer.c"
#include "short/chunk_builder.c"
#include "short/source_visitor.c"
#include "dart_formatter.c"

typedef struct {
	Node *unit;
	Str text;
	Str result;
} FormatJob;

static void format_job(void *arg)
{
	FormatJob *job = arg;
	DartFormatterOpts opts = {.pageWidth = 80};
	job->result = df_formatSource(&opts, job->unit, job->text, 307);
}

const char *ds_format(Node *unit, Str text, Buf *out)
{
	FormatJob job = {.unit = unit, .text = text};
	const char *err = try_run(format_job, &job);
	if (err)
		return err;
	buf_add(out, job.result.p, job.result.n);
	return NULL;
}
