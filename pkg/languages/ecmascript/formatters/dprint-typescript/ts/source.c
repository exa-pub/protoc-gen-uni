/* The laid out source as dprint-swc-ext 0.12.0 sees it (src/common/{text_info.rs,tokens.rs,
 * comments.rs,pos.rs}): line index, token container (token index by start/end position, previous
 * and next tokens, tokens in a range) and comment container (leading comments at a token start,
 * trailing comments at a token end).
 *
 * dprint-swc-ext: Copyright (c) 2020-2023 David Sherret, MIT License. */

static Source *source_new(const char *text, uint32_t n)
{
	Source *s = xalloc(sizeof *s);
	s->text = text;
	s->n = n;
	int cap = 64;
	s->line_starts = xalloc(sizeof(uint32_t) * (size_t)cap);
	s->line_starts[s->nlines++] = 0;
	for (uint32_t i = 0; i < n; i++) {
		if (text[i] == '\n') {
			if (s->nlines == cap) {
				s->line_starts = xrealloc(s->line_starts, sizeof(uint32_t) * (size_t)cap, sizeof(uint32_t) * (size_t)cap * 2);
				cap *= 2;
			}
			s->line_starts[s->nlines++] = i + 1;
		}
	}
	return s;
}

/* SourceTextInfo::line_index */
static int src_line(const Source *s, uint32_t pos)
{
	int lo = 0, hi = s->nlines;
	while (hi - lo > 1) {
		int mid = (lo + hi) / 2;
		if (s->line_starts[mid] <= pos)
			lo = mid;
		else
			hi = mid;
	}
	return lo;
}

/* Token and comment indexes of a laid out source (layout.c): token index by start/end position
 * (last wins, like the FxHashMap collect), comments by attachment position. */
static void source_index(Source *s)
{
	uint32_t n = s->n + 1;
	s->tok_at_start = xalloc(sizeof(int32_t) * n);
	s->tok_at_end = xalloc(sizeof(int32_t) * n);
	memset(s->tok_at_start, -1, sizeof(int32_t) * n);
	memset(s->tok_at_end, -1, sizeof(int32_t) * n);
	for (int i = 0; i < s->ntoks; i++) {
		s->tok_at_start[s->toks[i].lo] = i;
		s->tok_at_end[s->toks[i].hi] = i;
	}
	s->lead_first = xalloc(sizeof(int32_t) * n);
	s->trail_first = xalloc(sizeof(int32_t) * n);
	s->lead_count = xalloc(sizeof(int32_t) * n);
	s->trail_count = xalloc(sizeof(int32_t) * n);
	memset(s->lead_first, -1, sizeof(int32_t) * n);
	memset(s->trail_first, -1, sizeof(int32_t) * n);
	s->lead_idx = xalloc(sizeof(int32_t) * (size_t)(s->ncomments + 1));
	s->trail_idx = xalloc(sizeof(int32_t) * (size_t)(s->ncomments + 1));
	/* comments at one position are contiguous in source order per kind: count then fill */
	for (int i = 0; i < s->ncomments; i++) {
		Comment *c = &s->comments[i];
		if (c->leading)
			s->lead_count[c->pos]++;
		else
			s->trail_count[c->pos]++;
	}
	int nl = 0, ntr = 0;
	for (uint32_t pos = 0; pos < n; pos++) {
		if (s->lead_count[pos]) {
			s->lead_first[pos] = nl;
			nl += s->lead_count[pos];
		}
		if (s->trail_count[pos]) {
			s->trail_first[pos] = ntr;
			ntr += s->trail_count[pos];
		}
	}
	int32_t *lfill = xalloc(sizeof(int32_t) * n);
	int32_t *tfill = xalloc(sizeof(int32_t) * n);
	for (int i = 0; i < s->ncomments; i++) {
		Comment *c = &s->comments[i];
		if (c->leading)
			s->lead_idx[s->lead_first[c->pos] + lfill[c->pos]++] = i;
		else
			s->trail_idx[s->trail_first[c->pos] + tfill[c->pos]++] = i;
	}
}

/* ---- comments iterator (dprint-swc-ext CommentsIterator): a list of comment vectors ---- */

typedef struct {
	int32_t first, count;
} CommentVec;

typedef struct {
	CommentVec *vecs;
	int n, cap;
} Comments;

static void cm_push_vec(Comments *it, int32_t first_idx_list_start, int32_t count, bool leading, const Source *s)
{
	(void)leading;
	(void)s;
	if (count <= 0)
		return;
	if (it->n == it->cap) {
		int cap = it->cap ? it->cap * 2 : 4;
		it->vecs = xrealloc(it->vecs, sizeof(CommentVec) * (size_t)it->cap, sizeof(CommentVec) * (size_t)cap);
		it->cap = cap;
	}
	it->vecs[it->n++] = (CommentVec){first_idx_list_start, count};
}

/* Encodes a vector of the leading (positive) or trailing index list. */
static void cm_push_leading_at(Comments *it, const Source *s, uint32_t pos)
{
	if (pos <= s->n && s->lead_first[pos] >= 0)
		cm_push_vec(it, s->lead_first[pos] + 1, s->lead_count[pos], true, s);
}

static void cm_push_trailing_at(Comments *it, const Source *s, uint32_t pos)
{
	if (pos <= s->n && s->trail_first[pos] >= 0)
		cm_push_vec(it, -(s->trail_first[pos] + 1), s->trail_count[pos], false, s);
}

static void cm_extend(Comments *it, Comments other)
{
	for (int i = 0; i < other.n; i++)
		cm_push_vec(it, other.vecs[i].first, other.vecs[i].count, false, NULL);
}

static int cm_count(const Comments *it)
{
	int c = 0;
	for (int i = 0; i < it->n; i++)
		c += it->vecs[i].count;
	return c;
}

static bool cm_empty(const Comments *it) { return it->n == 0; }

/* The k-th comment of the iterator. */
static Comment *cm_get(const Comments *it, const Source *s, int k)
{
	for (int i = 0; i < it->n; i++) {
		if (k < it->vecs[i].count) {
			int32_t f = it->vecs[i].first;
			int32_t idx = f > 0 ? s->lead_idx[f - 1 + k] : s->trail_idx[-f - 1 + k];
			return &s->comments[idx];
		}
		k -= it->vecs[i].count;
	}
	return NULL;
}

#define CM_FOR(it, s, c)                                                                                            \
	for (int c##_k = 0, c##_n = cm_count(&(it)); c##_k < c##_n; c##_k++)                                        \
		for (Comment *c = cm_get(&(it), (s), c##_k); c; c = NULL)

static Comment *cm_last(const Comments *it, const Source *s)
{
	if (it->n == 0)
		return NULL;
	const CommentVec *v = &it->vecs[it->n - 1];
	int32_t idx = v->first > 0 ? s->lead_idx[v->first - 1 + v->count - 1] : s->trail_idx[-v->first - 1 + v->count - 1];
	return &s->comments[idx];
}

/* CommentContainer::leading_comments(start) */
static Comments src_leading_comments(const Source *s, uint32_t start)
{
	Comments it = {0};
	if (start <= s->n && s->tok_at_start[start] >= 0) {
		int index = s->tok_at_start[start];
		uint32_t prev_hi = index == 0 ? 0 : s->toks[index - 1].hi;
		cm_push_trailing_at(&it, s, prev_hi);
	}
	cm_push_leading_at(&it, s, start);
	return it;
}

/* CommentContainer::trailing_comments(end) */
static Comments src_trailing_comments(const Source *s, uint32_t end)
{
	Comments it = {0};
	cm_push_trailing_at(&it, s, end);
	if (end <= s->n && s->tok_at_end[end] >= 0) {
		int index = s->tok_at_end[end];
		uint32_t next_lo = index + 1 < s->ntoks ? s->toks[index + 1].lo : s->n;
		cm_push_leading_at(&it, s, next_lo);
	}
	return it;
}

/* ---- tokens (TokenContainer) ---- */

/* Binary searches over the tokens (sorted, not overlapping): the index of the first token whose lo
 * (by_hi: hi) is greater than pos (strict) or not less than pos (!strict); ntoks if none. They
 * replace the linear scans of the fallbacks below (same results). */
static int tok_search(const Source *s, uint32_t pos, bool by_hi, bool strict)
{
	int lo = 0, hi = s->ntoks;
	while (lo < hi) {
		int mid = lo + (hi - lo) / 2;
		uint32_t v = by_hi ? s->toks[mid].hi : s->toks[mid].lo;
		if (strict ? v > pos : v >= pos)
			hi = mid;
		else
			lo = mid + 1;
	}
	return lo;
}

static const Tok *src_previous_token(const Source *s, uint32_t start)
{
	if (start <= s->n && s->tok_at_start[start] >= 0) {
		int index = s->tok_at_start[start];
		return index == 0 ? NULL : &s->toks[index - 1];
	}
	/* the token before the first one ending after start (none if no token ends after it) */
	int i = tok_search(s, start, true, true);
	return i < s->ntoks && i > 0 ? &s->toks[i - 1] : NULL;
}

static const Tok *src_next_token(const Source *s, uint32_t end)
{
	if (end <= s->n && s->tok_at_end[end] >= 0) {
		int index = s->tok_at_end[end];
		return index + 1 < s->ntoks ? &s->toks[index + 1] : NULL;
	}
	int i = tok_search(s, end, false, true);
	return i < s->ntoks ? &s->toks[i] : NULL;
}

/* get_tokens_in_range: [*first, *last) token indexes */
static void src_tokens_in_range(const Source *s, uint32_t start, uint32_t end, int *first, int *last)
{
	int start_index = -1, end_index = -1;
	if (start <= s->n && s->tok_at_start[start] >= 0)
		start_index = s->tok_at_start[start];
	else if (start <= s->n && s->tok_at_end[start] >= 0)
		start_index = s->tok_at_end[start] + 1;
	else {
		int i = tok_search(s, start, false, false); /* first token with lo >= start */
		if (i < s->ntoks)
			start_index = i;
	}
	if (end <= s->n && s->tok_at_end[end] >= 0)
		end_index = s->tok_at_end[end];
	else if (end <= s->n && s->tok_at_start[end] >= 0)
		end_index = s->tok_at_start[end] > 0 ? s->tok_at_start[end] - 1 : -1;
	else {
		end_index = tok_search(s, end, true, true) - 1; /* last token with hi <= end */
	}
	if (start_index < 0)
		start_index = end_index >= 0 ? end_index : 0;
	int e = end_index >= 0 ? end_index + 1 : start_index;
	*first = start_index;
	*last = e < start_index ? start_index : e;
}

/* previous_tokens_fast: tokens before the token at start (index) */
static int src_token_index_before(const Source *s, uint32_t start)
{
	if (start <= s->n && s->tok_at_start[start] >= 0)
		return s->tok_at_start[start];
	if (start <= s->n && s->tok_at_end[start] >= 0)
		return s->tok_at_end[start];
	fail("dprint: position %u did not have a token index", start);
}

/* next_tokens_fast: index of the first token after the token ending at end */
static int src_token_index_after(const Source *s, uint32_t end)
{
	if (end <= s->n && s->tok_at_end[end] >= 0)
		return s->tok_at_end[end] + 1;
	if (end <= s->n && s->tok_at_start[end] >= 0)
		return s->tok_at_start[end] + 1;
	fail("dprint: position %u did not have a token index", end);
}
