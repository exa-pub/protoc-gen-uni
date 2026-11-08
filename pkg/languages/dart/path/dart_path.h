/* package:path (pub, 1.9) as protoc_plugin uses it: the url context (`path.url`) and the posix
 * platform context of the Linux build of the remote plugin, for relative paths only. Paths are
 * the percent-encoded strings of Dart Uris (Uri.file(...).toString()), which these functions treat
 * as plain strings like the url style does. */
#ifndef DART_PATH_H
#define DART_PATH_H

#include "util.h"

/* path.withoutExtension: drops the extension of the last part ("a/b.proto" -> "a/b"; a leading
 * dot does not start an extension). */
char *dpath_without_extension(const char *p);
/* path.url.dirname ("a/b/c" -> "a/b", "c" -> "."). */
char *dpath_dirname(const char *p);
/* path.url.relative(p, from: from) of relative paths. Returns NULL where package:path throws a
 * PathException (from leaves the root through ".."). */
char *dpath_relative(const char *p, const char *from);
/* Uri.file(p).toString() on posix: every segment percent-encoded except the characters of Dart's
 * path character table. *absolute is set for paths starting with '/'. */
char *dpath_file_uri(const char *p, bool *absolute);

/* Context(style: Style.posix) (current directory "."), the full semantics of package:path 1.9.1
 * (context.c): dirname, basenameWithoutExtension, withoutExtension, join of two parts, normalize,
 * and relative, which returns NULL and sets *err to the PathException text where it throws.
 * Paths must not contain NUL. */
char *dpath_posix_dirname(const char *path);
char *dpath_posix_basename_without_extension(const char *path);
char *dpath_posix_without_extension(const char *path);
char *dpath_posix_join(const char *a, const char *b);
char *dpath_posix_normalize(const char *path);
char *dpath_posix_relative(const char *path, const char *from, const char **err);

#endif
