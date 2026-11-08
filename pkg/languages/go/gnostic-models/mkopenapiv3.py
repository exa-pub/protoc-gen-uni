#!/usr/bin/env python3
"""Generates openapiv3_tables.inc and openapiv3_fields.h from gnostic-models v0.7.0
(openapiv3/OpenAPIv3.proto: the messages; openapiv3/OpenAPIv3.go: their ToRawInfo methods).

    mkopenapiv3.py <gnostic-models dir> <output dir>

The ToRawInfo methods are generated code with a fixed set of statement shapes; each becomes an
operation of the table that openapiv3.c interprets."""
import re
import sys

src, out = sys.argv[1], sys.argv[2]
proto = open(f"{src}/openapiv3/OpenAPIv3.proto").read()
gocode = open(f"{src}/openapiv3/OpenAPIv3.go").read()

# ---- messages of the proto ----
proto = re.sub(r"//[^\n]*", "", proto)
SCALARS = {"double": "DOUBLE", "float": "FLOAT", "int64": "INT64", "int32": "INT32", "bool": "BOOL",
           "string": "STRING", "bytes": "BYTES"}
messages = []  # (name, [fields])
for m in re.finditer(r"\bmessage\s+(\w+)\s*\{", proto):
    name = m.group(1)
    i, depth = m.end(), 1
    while depth:
        depth += {"{": 1, "}": -1}.get(proto[i], 0)
        i += 1
    body = proto[m.end():i - 1]
    fields = []
    for f in re.finditer(r"(oneof\s+\w+\s*\{)|(\})|(repeated\s+)?([\w.]+)\s+(\w+)\s*=\s*(\d+)\s*;", body):
        if f.group(1):
            in_oneof = True
            continue
        if f.group(2):
            in_oneof = False
            continue
        fields.append(dict(name=f.group(5), num=int(f.group(6)), type=f.group(4), rep=bool(f.group(3)),
                           oneof=locals().get("in_oneof", False)))
    in_oneof = False
    messages.append((name, fields))
msg_index = {n: i for i, (n, _) in enumerate(messages)}
ANY = len(messages)  # google.protobuf.Any


def go_camel(s):
    # protoc-gen-go strs.GoCamelCase
    out, i = "", 0
    while i < len(s):
        c = s[i]
        if c == "." and i + 1 < len(s) and s[i + 1].islower():
            pass
        elif c == ".":
            out += "_"
        elif c == "_" and (i == 0 or s[i - 1] == "."):
            out += "X"
        elif c == "_" and i + 1 < len(s) and s[i + 1].islower():
            pass
        elif c.isdigit():
            out += c
        else:
            if c.islower():
                c = c.upper()
            out += c
            while i + 1 < len(s) and s[i + 1].islower():
                i += 1
                out += s[i]
        i += 1
    return out


# ---- ToRawInfo bodies ----
OPS = []  # per message: (kind, [(op, field index, key)])
for name, fields in messages:
    gofield = {}
    for k, f in enumerate(fields):
        gofield[go_camel(f["name"])] = k
        gofield[go_camel(f["name"]) + "_"] = k
    m = re.search(r"^func \(m \*" + name + r"\) ToRawInfo\(\) \*yaml.Node \{\n(.*?)^\}", gocode, re.S | re.M)
    body = m.group(1)
    ops = []
    if name == "Any":
        OPS.append(("ANY", ops))
        continue
    if name == "StringArray":
        OPS.append(("STRINGARRAY", ops))
        continue
    if "// ONE OF WRAPPER" in body:
        for g in re.finditer(r"v\d+ := m\.Get(\w+)\(\)|\(\*" + name + r"_(\w+)\); ok \{\n\s*return compiler\.NewScalarNodeFor(\w+)", body):
            if g.group(1):
                ops.append(("ONEOF_MSG", gofield[g.group(1)], None))
            else:
                ops.append(("ONEOF_" + g.group(3).upper(), gofield[g.group(2)], None))
        OPS.append(("ONEOF", ops))
        continue
    lines = body.split("\n")
    i = 0
    while i < len(lines):
        l = lines[i]
        if l.startswith("\tinfo := ") or l.startswith("\tif m == nil") or l in ("\t\treturn info", "\t}", "\treturn info", "") \
                or l.startswith("\t// &{"):
            i += 1
            continue
        if l == "\t// always include this required field.":
            key = re.match(r'\tinfo.Content = append\(info.Content, compiler.NewScalarNodeForString\("([^"]*)"\)\)', lines[i + 1]).group(1)
            v = lines[i + 2]
            g = re.match(r"\tinfo.Content = append\(info.Content, compiler.NewScalarNodeForString\(m\.(\w+)\)\)", v)
            if g:
                ops.append(("REQ_STR", gofield[g.group(1)], key))
            else:
                g = re.match(r"\tinfo.Content = append\(info.Content, m\.(\w+)\.ToRawInfo\(\)\)", v)
                ops.append(("REQ_MSG", gofield[g.group(1)], key))
            i += 3
            continue
        g = re.match(r"\tif (len\()?m\.(\w+)\)? != (\"\"|false|0\.0|0|nil) \{", l)
        assert g, (name, l)
        j = i + 1
        while lines[j] != "\t}":
            j += 1
        block = "\n".join(lines[i + 1:j])
        fi = gofield[g.group(2)]
        keym = re.search(r'compiler.NewScalarNodeForString\("([^"]*)"\)', block)
        key = keym.group(1) if keym else None
        cond = g.group(3)
        if g.group(1):
            if "NewSequenceNodeForStringArray" in block:
                ops.append(("REP_STR", fi, key))
            else:
                ops.append(("REP_MSG", fi, key))
        elif "item.Name" in block:
            ops.append(("INLINE", fi, None))
        elif cond == '""':
            ops.append(("STR", fi, key))
        elif cond == "false":
            ops.append(("BOOL", fi, key))
        elif cond == "0.0":
            ops.append(("FLOAT", fi, key))
        elif cond == "0":
            ops.append(("INT", fi, key))
        elif "if len(items.Content) == 1" in block:
            ops.append(("ITEMS", fi, key))
        else:
            assert re.search(r"m\.\w+\.ToRawInfo\(\)", block), (name, block)
            ops.append(("MSG", fi, key))
        i = j + 1
    OPS.append(("MAPPING", ops))

# ---- output ----
def cstr(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'

h = ["/* Generated by mkopenapiv3.py from gnostic-models v0.7.0 openapiv3/OpenAPIv3.proto. DO NOT EDIT. */",
     "#ifndef GNOSTIC_OPENAPIV3_FIELDS_H", "#define GNOSTIC_OPENAPIV3_FIELDS_H", "",
     "/* The messages of openapi.v3 (and google.protobuf.Any). */", "enum {"]
for n, _ in messages:
    h.append(f"\tOA_{n},")
h.append("\tOA_GoogleProtobufAny,")
h.append("\tOA_COUNT,")
h.append("};")
h.append("")
h.append("/* Field numbers: OA_<Message>_<field>. */")
h.append("enum {")
for n, fields in messages:
    for f in fields:
        h.append(f"\tOA_{n}_{f['name']} = {f['num']},")
h.append("};")
h.append("")
h.append("#endif")
open(f"{out}/openapiv3_fields.h", "w").write("\n".join(h) + "\n")

c = ["/* Generated by mkopenapiv3.py from gnostic-models v0.7.0 openapiv3/OpenAPIv3.proto and",
     " * openapiv3/OpenAPIv3.go (the ToRawInfo methods). DO NOT EDIT. Included by openapiv3.c. */", ""]
for (n, fields), (kind, ops) in zip(messages, OPS):
    fs = []
    for f in fields:
        if f["type"] in SCALARS:
            t, msg = SCALARS[f["type"]], -1
        elif f["type"] == "google.protobuf.Any":
            t, msg = "MESSAGE", "OA_GoogleProtobufAny"
        else:
            t, msg = "MESSAGE", "OA_" + f["type"]
        lab = "PL_REP" if f["rep"] else "PL_OPT"
        fs.append(f"{{{f['num']}, PT_{t}, {lab}, {1 if f['oneof'] else 0}, {msg}}}")
    c.append(f"static const OaField oa_f_{n}[] = {{{', '.join(fs)}}};")
    if ops:
        os_ = [f"{{OP_{o}, {fi}, {cstr(k) if k is not None else 'NULL'}}}" for o, fi, k in ops]
        c.append(f"static const OaOp oa_o_{n}[] = {{{', '.join(os_)}}};")
names = []
c.append("")
c.append("static const OaMsg oa_msgs[OA_COUNT] = {")
for (n, fields), (kind, ops) in zip(messages, OPS):
    fnames = "".join(f["name"] + "\\0" for f in fields)
    o = f"oa_o_{n}" if ops else "NULL"
    c.append(f'\t[OA_{n}] = {{"{n}", "{fnames}", oa_f_{n}, {len(fields)}, {o}, {len(ops)}, K_{kind}}},')
c.append('\t[OA_GoogleProtobufAny] = {"google.protobuf.Any", "type_url\\0value\\0", oa_f_GoogleProtobufAny, 2, NULL, 0, K_MAPPING},')
c.append("};")
c.insert(3, "static const OaField oa_f_GoogleProtobufAny[] = {{1, PT_STRING, PL_OPT, 0, -1}, {2, PT_BYTES, PL_OPT, 0, -1}};")
open(f"{out}/openapiv3_tables.inc", "w").write("\n".join(c) + "\n")
