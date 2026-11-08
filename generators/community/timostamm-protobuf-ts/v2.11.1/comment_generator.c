/* Copyright (c) 2016 Timo Stamm
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

/* Ported from packages/plugin/src/code-gen/comment-generator.ts. */

enum { APPEND_TO_LEADING_BLOCK, TRAILING_LINES };

static Str strip_trailing_newline(Str s)
{
	if (s.n && s.p[s.n - 1] == '\n')
		s.n--;
	return s;
}

/* CommentGenerator.getComments */
static Comments cg_get_comments(const Desc *d)
{
	Comments c = get_comments(d);
	if (c.leading.p)
		c.leading = strip_trailing_newline(c.leading);
	if (c.trailing.p)
		c.trailing = strip_trailing_newline(c.trailing);
	Str *ld = xalloc(sizeof *ld * (size_t)(c.n_leading_detached + 1));
	for (int i = 0; i < c.n_leading_detached; i++)
		ld[i] = strip_trailing_newline(c.leading_detached[i]);
	c.leading_detached = ld;
	return c;
}

static bool cg_is_deprecated(const Desc *d)
{
	if (d->deprecated)
		return true;
	return desc_file_of(d)->h.deprecated;
}

/* makeDeprecatedTag */
static const char *make_deprecated_tag(const Desc *d) { return cg_is_deprecated(d) ? "@deprecated\n" : ""; }

/* makeGeneratedTag */
static Str make_generated_tag(const Desc *d)
{
	Buf b = {0};
	switch (d->kind) {
	case DK_ONEOF:
		buf_adds(&b, xcat("@generated from protobuf oneof: ", ((const DescOneof *)d)->name));
		break;
	case DK_ENUM_VALUE:
		buf_adds(&b, "@generated from protobuf enum value: ");
		buf_add_str(&b, declaration_string(d));
		buf_addc(&b, ';');
		break;
	case DK_FIELD:
		buf_adds(&b, ((const DescField *)d)->is_extension ? "@generated from protobuf extension: "
								   : "@generated from protobuf field: ");
		buf_add_str(&b, declaration_string(d));
		break;
	case DK_METHOD:
		buf_adds(&b, xcat("@generated from protobuf rpc: ", ((const DescMethod *)d)->name));
		break;
	default:
		buf_adds(&b, xcat("@generated from protobuf ", desc_string(d)));
	}
	return buf_view(&b);
}

/* getCommentBlock */
static Str get_comment_block(const Desc *d, bool append_trailing)
{
	Comments c = cg_get_comments(d);
	Buf b = {0};
	if (c.leading.p)
		buf_add_str(&b, c.leading);
	if (c.trailing.n && append_trailing) {
		if (b.n)
			buf_adds(&b, "\n\n");
		buf_add_str(&b, c.trailing);
	}
	if (b.n)
		buf_adds(&b, "\n\n");
	buf_adds(&b, make_deprecated_tag(d));
	buf_add_str(&b, make_generated_tag(d));
	return buf_view(&b);
}

/* addCommentsForDescriptor */
static void add_comments_for_descriptor(TsNode *node, const Desc *d, int mode)
{
	Comments c = cg_get_comments(d);
	add_comment_blocks_as_leading_detached_lines(node, c.leading_detached, c.n_leading_detached);
	add_comment_block_as_js_doc(node, get_comment_block(d, mode == APPEND_TO_LEADING_BLOCK));
	if (c.trailing.n && mode == TRAILING_LINES) {
		Str *lines;
		int n = str_split_s(c.trailing, '\n', &lines);
		for (int i = 0; i < n; i++) {
			Buf l = {0};
			if (!lines[i].n || lines[i].p[0] != ' ')
				buf_addc(&l, ' ');
			buf_add_str(&l, lines[i]);
			ts_add_synthetic_trailing_comment(node, TS_SingleLineCommentTrivia, buf_view(&l), true);
		}
	}
}
