#!/usr/bin/env python3
"""Writes schema.c: the descriptor.proto and plugin.proto schemas of protobuf-py v0.7.0, with the
fields in declaration order (the order of protobuf-py's to_binary) and the known values of the
closed enums (unknown values become unknown fields).

    python3 -I mkschema.py <site-packages>/protobuf > schema.c   (protobuf-py-pure 0.7.0 installed)

descriptor.proto comes from the boot repr in wkt/google/protobuf/descriptor_pb.py, plugin.proto
from the descriptor bytes in wkt/google/protobuf/compiler/plugin_pb.py, and the names that repr()
of the descriptor.proto messages prints (class qualnames, attribute names, enum member names, the
values of unset fields) from the imported classes: the parent directory of the protobuf package
needs typing_extensions."""
import ast
import sys

root = sys.argv[1]
T = {1: "DOUBLE", 2: "FLOAT", 3: "INT64", 4: "UINT64", 5: "INT32", 6: "FIXED64", 7: "FIXED32",
     8: "BOOL", 9: "STRING", 10: "GROUP", 11: "MESSAGE", 12: "BYTES", 13: "UINT32", 14: "ENUM",
     15: "SFIXED32", 16: "SFIXED64", 17: "SINT32", 18: "SINT64"}
TN = {v: k for k, v in T.items()}
LN = {"OPTIONAL": 1, "REQUIRED": 2, "REPEATED": 3}

# ---- descriptor.proto from the boot repr ----
src = open(f"{root}/wkt/google/protobuf/descriptor_pb.py").read()
i = src.index("_DESC = boot(")
expr = src[i + len("_DESC = boot("):src.index("\n)\n", i)]
expr = expr[:expr.rindex("{")].rstrip().rstrip(",")


class Unset:
    def __init__(self, v):
        self.v = v


class Ctor:
    def __init__(self, name):
        self.name = name

    def __call__(self, **kw):
        return kw

    def __getattr__(self, a):
        if a[0].isupper() and not a.isupper():
            return Ctor(self.name + "." + a)
        return a


class NS(dict):
    def __missing__(self, k):
        return Unset if k == "unset" else Ctor(k)


fd = eval("(" + expr + ")", {}, NS())


def norm_file(fd, typeof):
    msgs, enums = [], {}

    def walk_enum(e, prefix):
        enums[prefix + e["name"]] = [v["number"] for v in e["value"]]

    def walk(m, prefix):
        full = prefix + m["name"]
        fields = []
        for f in m["field"]:
            t, l = typeof(f)
            opts = f.get("options")
            packed = bool(opts) and opts.get("packed") is True
            tn = f.get("type_name")
            tn = None if isinstance(tn, Unset) or not tn else tn
            fields.append((f["number"], f["name"], t, l, tn, packed))
        msgs.append((full, fields))
        for n in m["nested_type"]:
            walk(n, full + ".")
        for e in m["enum_type"]:
            walk_enum(e, full + ".")

    pkg = "." + fd["package"] + "."
    for m in fd["message_type"]:
        walk(m, pkg)
    for e in fd["enum_type"]:
        walk_enum(e, pkg)
    return msgs, enums


desc_msgs, desc_enums = norm_file(fd, lambda f: (TN[f["type"]], LN[f["label"]]))

# ---- plugin.proto from its descriptor bytes ----
psrc = open(f"{root}/wkt/google/protobuf/compiler/plugin_pb.py").read()
j = psrc.index("_DESC = file_desc(")
line = psrc[psrc.index("\n", j) + 1:]
line = line[:line.index("\n")].strip().rstrip(",")
data = ast.literal_eval(line)


def varint(b, p):
    v = s = 0
    while True:
        c = b[p]
        p += 1
        v |= (c & 0x7F) << s
        s += 7
        if c < 0x80:
            return v, p


def fields(b):
    p, out = 0, []
    while p < len(b):
        k, p = varint(b, p)
        n, wt = k >> 3, k & 7
        if wt == 0:
            v, p = varint(b, p)
        elif wt == 2:
            ln, p = varint(b, p)
            v = b[p:p + ln]
            p += ln
        elif wt == 1:
            v, p = b[p:p + 8], p + 8
        elif wt == 5:
            v, p = b[p:p + 4], p + 4
        out.append((n, v))
    return out


def decode_msg(b):
    d = {"name": "", "field": [], "nested_type": [], "enum_type": []}
    for n, v in fields(b):
        if n == 1:
            d["name"] = v.decode()
        elif n == 2:
            f = {"options": None}
            for fn, fv in fields(v):
                if fn == 1: f["name"] = fv.decode()
                elif fn == 3: f["number"] = fv
                elif fn == 4: f["label"] = fv
                elif fn == 5: f["type"] = fv
                elif fn == 6: f["type_name"] = fv.decode()
                elif fn == 8: f["options"] = {"packed": any(on == 2 and ov == 1 for on, ov in fields(fv))}
            d["field"].append(f)
        elif n == 3:
            d["nested_type"].append(decode_msg(v))
        elif n == 4:
            e = {"name": "", "value": []}
            for en, ev in fields(v):
                if en == 1: e["name"] = ev.decode()
                elif en == 2: e["value"].append({"number": dict(fields(ev)).get(2, 0)})
            d["enum_type"].append(e)
    return d


pfd = {"package": "", "message_type": [], "enum_type": []}
for n, v in fields(data):
    if n == 2: pfd["package"] = v.decode()
    elif n == 4: pfd["message_type"].append(decode_msg(v))
plugin_msgs, plugin_enums = norm_file(pfd, lambda f: (f["type"], f["label"]))

# ---- C ----
msgs = desc_msgs + plugin_msgs
enums = {**desc_enums, **plugin_enums}


def cname(full):
    return "pbpy_" + full.split(".", 3)[-1].replace("compiler.", "").replace(".", "_")


print("""/* Code generated by mkschema.py from protobuf-py v0.7.0 (wkt/google/protobuf/descriptor_pb.py and
 * compiler/plugin_pb.py); DO NOT EDIT.
 *
 * The messages of descriptor.proto and plugin.proto as protobuf-py knows them: the fields in
 * declaration order (to_binary writes them in this order), the closed enums with their known
 * values. Labels: PL_OPT, PL_REP, PL_PACKED and PBPY_REQUIRED. */
#include "protobuf_py.h"

#define N(a) (int)(sizeof(a) / sizeof((a)[0]))
#define S(num, t, l) {num, PT_##t, l, NULL, NULL, 0}
#define M(num, l, d) {num, PT_MESSAGE, l, &d, NULL, 0}
#define E(num, l, vals) {num, PT_ENUM, l, NULL, vals, N(vals)}
#define DEF(name) const PbMsgDef name = {N(f_##name), f_##name}
""")
line = "extern const PbMsgDef"
for i, (m, _) in enumerate(msgs):
    piece = " " + cname(m) + ("," if i + 1 < len(msgs) else ";")
    if len(line) + len(piece) > 100:
        print(line)
        line = "\t"
    line += piece
print(line + "\n")
used_enums = {}
for full, fs in msgs:
    for f in fs:
        if f[2] == 14:
            used_enums[f[4]] = enums[f[4]]
ev_names = {}
for full, vals in used_enums.items():
    key = tuple(vals)
    if key in ev_names.values():
        continue
    ev_names[full] = key
ev_by_vals = {v: k for k, v in ev_names.items()}


def ev_cname(full):
    return "ev_" + full.split(".")[-1]


for full, vals in ev_names.items():
    text = f"static const int32_t {ev_cname(full)}[] = {{{', '.join(map(str, vals))}}};"
    if len(text) > 100:
        cut = text.rindex(", ", 0, 100)
        text = text[:cut + 1] + "\n\t" + text[cut + 2:]
    print(text)
print()
for full, fs in msgs:
    name = cname(full)
    items = []
    for num, fname, t, l, tn, packed in fs:
        lab = {1: "PL_OPT", 2: "PBPY_REQUIRED", 3: "PL_PACKED" if packed else "PL_REP"}[l]
        if t in (11, 10):
            items.append(f"M({num}, {lab}, {cname(tn)})")
        elif t == 14:
            items.append(f"E({num}, {lab}, {ev_cname(ev_by_vals[tuple(enums[tn])])})")
        else:
            items.append(f"S({num}, {T[t]}, {lab})")
    if not items:
        print(f"const PbMsgDef {name} = {{0, NULL}};")
        continue
    print(f"static const PbField f_{name}[] = {{")
    line = "\t"
    for it in items:
        piece = it + ","
        if len(line) + len(piece) + 1 > 100 and line != "\t":
            print(line.rstrip())
            line = "\t"
        line += piece + " "
    print(line.rstrip())
    print("};")
    print(f"DEF({name});")

# ---- the names of protobuf-py's descriptor.proto classes (for the repr of _generate_boot) ----
# Needs the protobuf package importable (its parent directory with typing_extensions on sys.path).
import os
sys.path.insert(0, os.path.dirname(os.path.abspath(root)))
from protobuf import Enum, Message  # noqa: E402
from protobuf.wkt.google.protobuf import descriptor_pb  # noqa: E402

file_desc = descriptor_pb.desc()


def walk_types(m, out):
    out.append(m)
    for n in m.nested_messages:
        walk_types(n, out)


types = []
for m in file_desc.messages:
    walk_types(m, types)
by_cname = {cname("." + m.type_name): m for m in types}


def cstr(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def default_repr(v):
    if isinstance(v, Enum):
        return f"{type(v).__qualname__}.{v.name}"
    return repr(v)


enum_infos = {}
print()
print("/* The names that repr() of descriptor.proto messages prints. */")
infos = []
for full, fs in desc_msgs:
    name = cname(full)
    m = by_cname[name]
    inst = m.type()
    rows = []
    for num, fname, t, l, tn, packed in fs:
        member = next(f for f in m.fields if f.number == num)
        enm = "NULL"
        if t == 14:
            ecls = member.value.enum.type if hasattr(member.value, "enum") else member.value.element.type
            key = ecls.__qualname__
            if key not in enum_infos:
                enum_infos[key] = ("ei_" + key.replace(".", "_"), [(int(x), x.name) for x in ecls])
            enm = "&" + enum_infos[key][0]
        dflt = "NULL"
        if l != 3 and t not in (10, 11):
            dflt = cstr(default_repr(inst[member]))
        rows.append(f"{{{cstr(member.local_name)}, {dflt}, {enm}}}")
    infos.append((name, m.type.__qualname__, rows))
for key, (cn, vals) in enum_infos.items():
    print(f"static const PbpyEnumName {cn}_v[] = {{")
    line = "\t"
    for n, sname in vals:
        piece = f"{{{n}, {cstr(sname)}}},"
        if len(line) + len(piece) + 1 > 100 and line != "\t":
            print(line.rstrip())
            line = "\t"
        line += piece + " "
    print(line.rstrip())
    print("};")
    print(f"static const PbpyEnumInfo {cn} = {{{cstr(key)}, {len(vals)}, {cn}_v}};")
for name, qual, rows in infos:
    if not rows:
        continue
    print(f"static const PbpyFieldInfo fi_{name}[] = {{")
    for r in rows:
        print(f"\t{r},")
    print("};")
print("const PbpyMsgInfo pbpy_descriptor_info[] = {")
for name, qual, rows in infos:
    print(f"\t{{&{name}, {cstr(qual)}, {('fi_' + name) if rows else 'NULL'}}},")
print("\t{NULL, NULL, NULL},")
print("};")
