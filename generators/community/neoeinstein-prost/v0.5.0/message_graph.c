/* Port of src/message_graph.rs of prost-build 0.14.1 (https://github.com/tokio-rs/prost,
 * Apache-2.0): a graph of messages whose edges are non-repeated message fields, to find
 * recursively nested messages (their fields are boxed). */

typedef struct {
	int n, cap;
	int *v;
} IntVec;

typedef struct {
	Map index;    /* message name -> node + 1 */
	Vec names;    /* node -> name */
	Vec edges;    /* node -> IntVec * */
	Map messages; /* message name -> DescriptorProto */
	/* Built on the first is_nested, when the graph is complete. */
	int *comp;    /* node -> strongly connected component */
	Map edge_set; /* edge_key(from, to) -> 1 */
	int *stack, *mark; /* depth-first search: node -> generation */
	int generation;
} MessageGraph;

static void int_vec_push(IntVec *v, int x)
{
	if (v->n == v->cap) {
		int cap = v->cap ? v->cap * 2 : 4;
		v->v = xrealloc(v->v, sizeof *v->v * (size_t)v->cap, sizeof *v->v * (size_t)cap);
		v->cap = cap;
	}
	v->v[v->n++] = x;
}

static const char *edge_key(int from, int to)
{
	Buf b = {0};
	buf_fmt(&b, "%d>%d", from, to);
	return buf_str(&b);
}

static int get_or_insert_index(MessageGraph *g, const char *name)
{
	if (!*name)
		fail("index out of bounds: the len is 0 but the index is 0");
	if (*name != '.')
		fail("assertion `left == right` failed\n  left: 46\n right: %d", (unsigned char)*name);
	intptr_t node = (intptr_t)map_get(&g->index, name);
	if (node)
		return (int)node - 1;
	vec_push(&g->names, (void *)name);
	vec_push(&g->edges, xalloc(sizeof(IntVec)));
	map_put(&g->index, name, (void *)(intptr_t)g->names.n);
	return g->names.n - 1;
}

static void add_message(MessageGraph *g, const char *package, const PbMsg *msg)
{
	if (!pb_has(msg, DP_NAME))
		fail("called `Option::unwrap()` on a `None` value");
	const char *msg_name = xcat(package, ".", pb_str(msg, DP_NAME));
	int msg_index = get_or_insert_index(g, msg_name);
	for (int i = 0; i < pb_count(msg, DP_FIELD); i++) {
		const PbMsg *field = pb_msg_at(msg, DP_FIELD, i);
		if (pb_int(field, FD_TYPE, PT_DOUBLE) == PT_MESSAGE && pb_int(field, FD_LABEL, LABEL_OPTIONAL) != LABEL_REPEATED) {
			if (!pb_has(field, FD_TYPE_NAME))
				fail("called `Option::unwrap()` on a `None` value");
			int field_index = get_or_insert_index(g, pb_str(field, FD_TYPE_NAME));
			int_vec_push(g->edges.v[msg_index], field_index);
		}
	}
	map_put(&g->messages, msg_name, (void *)msg);
	for (int i = 0; i < pb_count(msg, DP_NESTED_TYPE); i++)
		add_message(g, msg_name, pb_msg_at(msg, DP_NESTED_TYPE, i));
}

/* MessageGraph::new over files (PbMsg *: FileDescriptorProto). */
static void message_graph_init(MessageGraph *g, const Vec *files)
{
	for (int i = 0; i < files->n; i++) {
		const PbMsg *file = files->v[i];
		const char *package = pb_has(file, FDP_PACKAGE) ? xcat(".", pb_str(file, FDP_PACKAGE)) : "";
		for (int j = 0; j < pb_count(file, FDP_MESSAGE_TYPE); j++)
			add_message(g, package, pb_msg_at(file, FDP_MESSAGE_TYPE, j));
	}
}

static const PbMsg *message_graph_get_message(const MessageGraph *g, const char *name)
{
	return map_get(&g->messages, name);
}

/* Strongly connected components (iterative Tarjan), numbered in completion order: if a node
 * reaches a node of another component, that component has the lower number. */
COLD static void message_graph_components(MessageGraph *g)
{
	int n = g->names.n;
	g->comp = xalloc(sizeof *g->comp * (size_t)(n ? n : 1));
	int *index = xalloc(sizeof *index * (size_t)(n ? n : 1)); /* 0: unvisited, else dfs index + 1 */
	int *low = xalloc(sizeof *low * (size_t)(n ? n : 1));
	int *edge = xalloc(sizeof *edge * (size_t)(n ? n : 1)); /* next edge to follow */
	int *call = xalloc(sizeof *call * (size_t)(n ? n : 1)); /* dfs call stack */
	int *stack = xalloc(sizeof *stack * (size_t)(n ? n : 1)); /* Tarjan stack */
	bool *on = xalloc(sizeof *on * (size_t)(n ? n : 1));
	int counter = 0, n_comps = 0, sp = 0;
	for (int root = 0; root < n; root++) {
		if (index[root])
			continue;
		int cp = 0;
		call[cp++] = root;
		index[root] = low[root] = ++counter;
		stack[sp++] = root, on[root] = true;
		while (cp) {
			int v = call[cp - 1];
			const IntVec *e = g->edges.v[v];
			if (edge[v] < e->n) {
				int w = e->v[edge[v]++];
				if (!index[w]) {
					index[w] = low[w] = ++counter;
					stack[sp++] = w, on[w] = true;
					call[cp++] = w;
				} else if (on[w] && index[w] < low[v]) {
					low[v] = index[w];
				}
				continue;
			}
			if (low[v] == index[v]) {
				int w;
				do {
					w = stack[--sp];
					on[w] = false;
					g->comp[w] = n_comps;
				} while (w != v);
				n_comps++;
			}
			if (--cp && low[v] < low[call[cp - 1]])
				low[call[cp - 1]] = low[v];
		}
	}
	for (int v = 0; v < n; v++) {
		const IntVec *e = g->edges.v[v];
		for (int i = 0; i < e->n; i++)
			map_put(&g->edge_set, edge_key(v, e->v[i]), (void *)1);
	}
	g->stack = xalloc(sizeof *g->stack * (size_t)(n ? n : 1));
	g->mark = xalloc(sizeof *g->mark * (size_t)(n ? n : 1));
}

/* is_nested: true if message type inner is nested in (reachable from) message type outer. The
 * components answer it when both are in one component (true), when the order of the components
 * rules a path out, or when an edge inner -> outer exists (a non-repeated message field, the
 * usual question: then a path back means one component); otherwise a depth-first search like
 * petgraph's has_path_connecting, which skips nodes that cannot reach inner. */
static bool message_graph_is_nested(MessageGraph *g, const char *outer, const char *inner)
{
	intptr_t from = (intptr_t)map_get(&g->index, outer), to = (intptr_t)map_get(&g->index, inner);
	if (!from || !to)
		return false;
	from--, to--;
	if (!g->comp)
		message_graph_components(g);
	if (g->comp[from] == g->comp[to])
		return true;
	if (g->comp[to] > g->comp[from] || map_get(&g->edge_set, edge_key((int)to, (int)from)))
		return false;
	int gen = ++g->generation, sp = 0, min = g->comp[to];
	g->stack[sp++] = (int)from;
	g->mark[from] = gen;
	while (sp) {
		int x = g->stack[--sp];
		if (x == to)
			return true;
		const IntVec *e = g->edges.v[x];
		for (int i = 0; i < e->n; i++) {
			int y = e->v[i];
			if (g->mark[y] != gen && g->comp[y] >= min) {
				g->mark[y] = gen;
				g->stack[sp++] = y;
			}
		}
	}
	return false;
}
