/* Copyright (c) 2011-2019 Canonical Ltd
 * Copyright (c) 2006-2010 Kirill Simonov
 * Licensed under the Apache License, Version 2.0 and the MIT License (see LICENSE of
 * go.yaml.in/yaml/v3). */

/* Port of go.yaml.in/yaml/v3 v3.0.4 (github.com/yaml/go-yaml; v3.0.5 differs only in comments
 * and tests), the parts that work on yaml.Node:
 * yaml.Marshal of a *Node (encode.go: (*encoder).node, emitterc.go, writerc.go, apic.go) and
 * yaml.Unmarshal into a *Node (decode.go: parser, parserc.go, scannerc.go, readerc.go), with
 * resolve.go (implicit tags). Go strings are Str (they may contain NUL). Marshalling or
 * unmarshalling Go values other than Node is not ported. */
#ifndef GO_YAML_H
#define GO_YAML_H

#include "util.h"

/* yaml.Kind */
enum {
	YAML_DOCUMENT_NODE = 1,
	YAML_SEQUENCE_NODE = 2,
	YAML_MAPPING_NODE = 4,
	YAML_SCALAR_NODE = 8,
	YAML_ALIAS_NODE = 16,
};

/* yaml.Style */
enum {
	YAML_TAGGED_STYLE = 1,
	YAML_DOUBLE_QUOTED_STYLE = 2,
	YAML_SINGLE_QUOTED_STYLE = 4,
	YAML_LITERAL_STYLE = 8,
	YAML_FOLDED_STYLE = 16,
	YAML_FLOW_STYLE = 32,
};

/* yaml.Node */
typedef struct YamlNode YamlNode;
struct YamlNode {
	int kind, style;
	Str tag, value, anchor;
	YamlNode *alias;
	YamlNode **content;
	int n_content, cap_content;
	bool content_set; /* Content != nil (an empty but non-nil slice also counts) */
	Str head_comment, line_comment, foot_comment;
	int line, column;
};

YamlNode *yaml_node_new(int kind);
/* node.Content = append(node.Content, child) */
void yaml_node_append(YamlNode *n, YamlNode *child);

/* yaml.Marshal(node): appends the document to out and returns NULL, or returns the error text
 * ("yaml: ..."). */
const char *yaml_marshal(const YamlNode *n, Buf *out);
/* yaml.Unmarshal(in, out) for out of type *yaml.Node: out is left untouched by an empty stream.
 * Returns NULL or the error text ("yaml: ..."). */
const char *yaml_unmarshal(Str in, YamlNode *out);

/* yaml.NewEncoder(out), SetIndent(indent), Encode(n) (no Close: no stream end): appends the
 * document to out and returns NULL, or returns the error text. */
const char *yaml_encode(const YamlNode *n, int indent, Buf *out);

/* The encoder of Go values (encode.go), for callers that build the nodes of their values
 * themselves: the style (*encoder).stringv gives a string outside flow context (0: plain,
 * YAML_DOUBLE_QUOTED_STYLE, YAML_LITERAL_STYLE; invalid UTF-8: 0, node() makes it !!binary), and a
 * scalar node with that style. A node of it emits what stringv emits. */
int yaml_string_style(Str s);
YamlNode *yaml_node_string(Str s);
/* The order in which mapv and the inline maps of structv emit string keys (sort.Sort(keyList)):
 * order receives the indexes of keys[0:n] sorted. */
void yaml_sort_keys(int *order, const Str *keys, int n);

#endif
