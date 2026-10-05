"""Print the NNet::Game event table from the client's type table: ids, then one layout per event.

The output is what docs/game-events.md quotes. `python catalog.py ids` or `python catalog.py layouts`.
"""
import sys
from events import *

_seen = set()


def tname(t):
    p = struct.unpack(">I", blob[doffs[t]:doffs[t] + 4])[0]
    return cstr(p)


def short_name(n):
    return n.replace("NNet::", "").replace("::Enum", "")


def expr(t, depth=0):
    ty = TS[t]
    k = ty["kind"]
    if k == "alias":
        n = tname(t)
        inner = expr(ty["target"], depth)
        return inner
    if k == "int":
        ln = ty["len"]
        return "Int%d %d..%d" % (ln["bits"], ln["min"], ln["max"]) if ln["bits"] else "Int0"
    if k in ("u32", "u32b"):
        return "u32"
    if k == "u64":
        return "u64"
    if k == "bool":
        return "bool"
    if k == "null":
        return "null"
    if k == "enum":
        n = tname(t)
        tg = ty["tag"]
        return "enum %s (%d bits, %d..%d)" % (short_name(n) or "?", tg["bits"], tg["min"], tg["max"])
    if k == "optional":
        return "optional " + expr(ty["inner"], depth)
    if k == "array":
        ln = ty["len"]
        c = ("count %d" % ln["min"]) if ln["fixed"] else "count Int%d %d..%d" % (ln["bits"], ln["min"], ln["max"])
        return "array(%s) of %s" % (c, expr(ty["elem"], depth))
    if k in ("blob", "blobv", "string"):
        ln = ty["len"]
        c = ("%d bytes" % ln["min"]) if ln["fixed"] else "len Int%d %d..%d" % (ln["bits"], ln["min"], ln["max"])
        return "%s(%s)" % ("string" if k == "string" else "blob", c)
    if k == "bitarray":
        ln = ty["len"]
        c = ("%d bits" % ln["min"]) if ln["fixed"] else "len Int%d %d..%d" % (ln["bits"], ln["min"], ln["max"])
        return "bitarray(%s)" % c
    if k == "choice":
        tg = ty["tag"]
        nm = members(t)[1:]
        if depth > 2:
            return "choice(%s)" % short_name(tname(t))
        items = ["%d %s: %s" % (v, nm[i] or "", expr(ty["types"][i], depth + 1)) for i, v in enumerate(ty["values"])]
        return "choice tag Int%d {%s}" % (tg["bits"], "; ".join(items))
    if k == "struct":
        nm = members(t)[1:]
        n = short_name(tname(t))
        if depth > 2:
            return "struct %s" % n
        fs = ["%s: %s" % ((nm[i] or "f%d" % i), expr(ft, depth + 1)) for i, ft in enumerate(ty["types"])]
        return "{%s}" % "; ".join(fs)
    return k


def layouts():
    out = []
    for i in sorted(EVENT_NAMES):
        t = EVENT_TYPE[i]
        nm = members(t)[1:]
        fs = ["%s: %s" % (nm[j], expr(ft, 0)) for j, ft in enumerate(TS[t]["types"])]
        out.append("%d %s\n    %s" % (i, EVENT_NAMES[i], ";\n    ".join(fs) if fs else "(empty)"))
    return out


if __name__ == "__main__":
    what = sys.argv[1] if len(sys.argv) > 1 else "layouts"
    if what == "ids":
        print("\n".join("%3d %s" % (i, EVENT_NAMES[i]) for i in sorted(EVENT_NAMES)))
    else:
        print("\n".join(layouts()))
