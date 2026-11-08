// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

// Author: kenton@google.com (Kenton Varda)
//  Based on original Protocol Buffers design by
//  Sanjay Ghemawat, Jeff Dean, and others.

/* Port of src/google/protobuf/compiler/csharp/csharp_doc_comment.{h,cc} (protobuf v36.2): C# XML
 * documentation comments from the comments of the .proto file. WriteMessageDocComment,
 * WritePropertyDocComment, WriteEnumDocComment, WriteEnumValueDocComment and
 * WriteMethodDocComment are the same function of the descriptor (cs_write_doc_comment). */

/* printer->Print("///$line$\n", "line", line): the value is a std::string, so a NUL byte is
 * printed too; it is written past the printer, which is mid-line after "///". */
static void cs_print_doc_line(LpPrinter *printer, Str line)
{
	if (!memchr(line.p, 0, line.n)) {
		lp_print(printer, "///$line$\n", "line", xstrndup(line.p, line.n), NULL);
		return;
	}
	lp_print(printer, "///", NULL);
	for (size_t i = 0; i <= line.n;) {
		const char *nul = memchr(line.p + i, 0, line.n - i);
		size_t j = nul ? (size_t)(nul - line.p) : line.n;
		if (j > i)
			lp_print(printer, "$line$", "line", xstrndup(line.p + i, j - i), NULL);
		if (j < line.n)
			buf_addc(printer->out, '\0');
		i = j + 1;
	}
	lp_print(printer, "\n", NULL);
}

static void cs_write_doc_comment_body_impl(LpPrinter *printer, const LpSourceLocation *location)
{
	Str comments = location->leading_comments.n ? location->leading_comments : location->trailing_comments;
	if (!comments.n)
		return;
	/* XML escaping... no need for apostrophes etc as the whole text is going to be a child node
	 * of a summary element, not part of an attribute. */
	Buf escaped = {0};
	for (size_t i = 0; i < comments.n; i++) {
		if (comments.p[i] == '&')
			buf_adds(&escaped, "&amp;");
		else if (comments.p[i] == '<')
			buf_adds(&escaped, "&lt;");
		else
			buf_addc(&escaped, comments.p[i]);
	}
	Str *lines;
	int n = str_split_s(buf_view(&escaped), '\n', &lines);
	lp_print(printer, "/// <summary>\n", NULL);
	/* We squash multiple blank lines down to one, and remove any trailing blank lines. We need
	 * to preserve the blank lines themselves, as this is relevant in the markdown. */
	bool last_was_empty = false;
	for (int i = 0; i < n; i++) {
		Str line = lines[i];
		if (!line.n) {
			last_was_empty = true;
			continue;
		}
		if (last_was_empty)
			lp_print(printer, "///\n", NULL);
		last_was_empty = false;
		/* If the comment has an extra slash at the start then this can cause the C# compiler to
		 * complain when generating the XML documentation (grpc/grpc#35905). */
		if (line.p[0] == '/') {
			Buf l = {0};
			buf_adds(&l, "&#x2F;");
			buf_add(&l, line.p + 1, line.n - 1);
			line = buf_view(&l);
		}
		cs_print_doc_line(printer, line);
	}
	lp_print(printer, "/// </summary>\n", NULL);
}

/* WriteDocCommentBody, behind the strip_nonfunctional_codegen check of the Write*DocComment
 * functions. */
static void cs_write_doc_comment(LpPrinter *printer, const CsOptions *options, const void *descriptor)
{
	if (options->strip_nonfunctional_codegen)
		return;
	LpSourceLocation location;
	if (lp_get_source_location(options->locations, descriptor, &location))
		cs_write_doc_comment_body_impl(printer, &location);
}
