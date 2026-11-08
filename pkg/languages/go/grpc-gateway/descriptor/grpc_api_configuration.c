/* Copyright (c) 2015, Gengo, Inc. All rights reserved. BSD-3-Clause (see LICENSE of
 * github.com/grpc-ecosystem/grpc-gateway). */

/* internal/descriptor/grpc_api_configuration.go and openapi_configuration.go: the
 * configuration files start with os.ReadFile. The buf.build plugins run in an image with nothing
 * but /etc/passwd and the plugin binary at the root, which is the working directory, so a user's
 * file does not exist there: the port gives that error, and an "unsupported" error for the paths
 * that exist in such a container (the root, its directories and files, pseudo file systems),
 * which it does not read (YAML configurations are not ported). */
#include "internal.h"

#include <string.h>

Str gw_ReadConfigFile(Str yamlFile, const char *what, const char *program, const char *option)
{
	const char *q = go_std_quote(yamlFile.p, yamlFile.n, NULL);
	Buf b = {0};
	buf_fmt(&b, "failed to read %s description from %s: open ", what, q);
	buf_add_str(&b, yamlFile);
	if (memchr(yamlFile.p, 0, yamlFile.n)) {
		buf_adds(&b, ": invalid argument");
		return buf_view(&b);
	}
	/* the working directory of the plugin process is the root of the image */
	const char *path = pg_path_clean(xcat("/", xstrndup(yamlFile.p, yamlFile.n)));
	const char *const existing[] = {"etc", "proc", "dev", "sys", program};
	bool exists = strcmp(path, "/") == 0;
	for (size_t i = 0; i < sizeof existing / sizeof *existing && !exists; i++) {
		size_t n = strlen(existing[i]);
		exists = strncmp(path + 1, existing[i], n) == 0 && (path[1 + n] == 0 || path[1 + n] == '/');
	}
	if (exists)
		return str_c(xcat("unsupported by protoc-gen-uni: ", option, "=", q,
				  " (the port does not read files; the buf.build plugin reads it from its container)"));
	buf_adds(&b, ": no such file or directory");
	return buf_view(&b);
}
