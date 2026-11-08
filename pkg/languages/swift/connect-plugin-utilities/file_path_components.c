/* Copyright 2022-2025 The Connect Authors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License. */

/* Port of Plugins/ConnectPluginUtilities/FilePathComponents.swift (connect-swift 1.2.3). Paths are
 * proto file names, ASCII (sw_ascii_name), so Swift's splits by Character are splits by byte. */
#include "connect_plugin_utilities.h"

#include <string.h>

CpuFilePathComponents cpu_file_path_components(const char *path)
{
	CpuFilePathComponents c = {0};
	/* components(separatedBy: "/"): directory = all but the last, joined by "/" */
	const char *slash = strrchr(path, '/');
	const char *last = slash ? slash + 1 : path;
	c.directory = slash ? (Str){path, (size_t)(slash - path)} : STR_LIT("");
	/* last.split(separator: "."): the non-empty parts */
	Vec all = str_split(last, '.'), parts = {0};
	for (int i = 0; i < all.n; i++)
		if (*(const char *)all.v[i])
			vec_push(&parts, all.v[i]);
	c.suffix = STR_LIT("");
	if (parts.n > 1)
		c.suffix = str_c(xcat(".", parts.v[parts.n - 1]));
	/* dropLast().joined(separator: ".") */
	parts.n = parts.n ? parts.n - 1 : 0;
	c.base = str_c(str_join(&parts, "."));
	return c;
}

char *cpu_output_file_path(const CpuFilePathComponents *c, const char *path_extension, CpuFileNaming option)
{
	char *base = xstrndup(c->base.p, c->base.n);
	if (!c->directory.n)
		return xcat(base, path_extension);
	switch (option) {
	case CPU_DROP_PATH:
		return xcat(base, path_extension);
	case CPU_FULL_PATH:
		return xcat(xstrndup(c->directory.p, c->directory.n), "/", base, path_extension);
	case CPU_PATH_TO_UNDERSCORES:
		break;
	}
	char *underscored = str_replace_all(xstrndup(c->directory.p, c->directory.n), "/", "_");
	if (underscored[0] == '_')
		underscored++;
	return xcat(underscored, "_", base, path_extension);
}
