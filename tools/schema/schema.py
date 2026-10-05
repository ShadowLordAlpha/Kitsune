"""Reader for the NNet type table (see image.py for where it comes from).

Type byte: low 6 bits = kind, bit 7 = fixed.
"""
import struct
from image import blob, count, offs, doffs, code_start, code_size

SIZES = {0: (1, True), 1: (1, False), 2: (2, True), 3: (2, False), 4: (4, True), 5: (4, False), 6: (8, True), 7: (8, False)}
KINDS = {1: "array", 2: "blobv", 3: "bitarray", 4: "blob", 5: "bool", 6: "choice", 7: "enum", 8: "u32", 9: "int",
         10: "null", 11: "optional", 12: "u32b", 13: "u64", 14: "struct", 15: "string", 16: "alias"}
B = blob


def rd(pos, code):
    n, signed = SIZES[code]
    return int.from_bytes(B[pos:pos + n], "big", signed=signed), pos + n


def u32(pos):
    return struct.unpack(">I", B[pos:pos + 4])[0], pos + 4


def bounds(pos, fixed):
    """FUN_020d4590: fixed -> [code][value]; else [code][flag:1|bits:7][min][max]."""
    code = B[pos]
    pos += 1
    if fixed:
        v, pos = rd(pos, code)
        return dict(fixed=True, min=v, max=v, bits=0, code=code), pos
    bits = B[pos] & 0x7f
    flag = B[pos] >> 7
    pos += 1
    lo, pos = rd(pos, code)
    hi, pos = rd(pos, code)
    return dict(fixed=False, bits=bits, min=lo, max=hi, code=code, flag=flag), pos


def table(pos, fixed, bd, with_types):
    """Entry list for choice / enum / struct (FUN_020d48d0 family)."""
    if not fixed:
        sc = B[pos]
        pos += 1
        n, pos = rd(pos, sc)
        vals = []
        for _ in range(n):
            v, pos = rd(pos, bd["code"])
            vals.append(v)
    else:
        n = bd["max"] + 1 - bd["min"]
        vals = list(range(bd["min"], bd["max"] + 1))
    types = []
    if with_types:
        for _ in range(n):
            t, pos = u32(pos)
            types.append(t)
    return vals, types, pos


def parse(t):
    pos = offs[t]
    first = B[pos]
    pos += 1
    kind = first & 0x3f
    fixed = bool(first & 0x80)
    ty = dict(t=t, kind=KINDS.get(kind, "?%d" % kind), fixed=fixed)
    if kind == 1:
        ty["len"], pos = bounds(pos, fixed)
        ty["elem"], pos = u32(pos)
    elif kind in (3, 4):
        ty["len"], pos = bounds(pos, fixed)
    elif kind == 2:
        ty["len"], pos = bounds(pos, False)
    elif kind == 15:
        ty["len"], pos = bounds(pos, False)
        ty["len"]["bits"] += 2
        ty["len"]["max"] <<= 2
    elif kind == 9:
        ty["len"], pos = bounds(pos, False)
    elif kind == 11:
        ty["inner"], pos = u32(pos)
    elif kind == 16:
        ty["target"], pos = u32(pos)
    elif kind in (6, 7, 14):
        bd, pos = bounds(pos, False)
        ty["tag"] = bd
        vals, types, pos = table(pos, fixed, bd, kind != 7)
        ty["values"] = vals
        ty["types"] = types
    elif kind in (5, 8, 10, 12, 13):
        pass
    else:
        ty["unknown"] = True
    ty["end"] = pos
    return ty


def all_types():
    return [parse(t) for t in range(count)]


if __name__ == "__main__":
    ts = all_types()
    starts = sorted(set(offs))
    nxt = {s: starts[i + 1] if i + 1 < len(starts) else code_start + code_size for i, s in enumerate(starts)}
    bad = [t["t"] for t in ts if t.get("unknown") or t["end"] != nxt[offs[t["t"]]]]
    print("types", len(ts), "mismatched or unknown:", len(bad), bad[:20])
    from collections import Counter
    print(Counter(t["kind"] for t in ts))
