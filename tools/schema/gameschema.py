"""Names and a decoder over the type table, used to check the generated C++ against real traffic."""
import struct
from schema import *
from image import blob, doffs, count

TS = all_types()


def cstr(off):
    if off == 0:
        return ""
    return blob[off:blob.index(b"\0", off)].decode("latin1")


def members(t):
    ty = TS[t]
    n = {"enum": len(ty.get("values", [])), "struct": len(ty.get("values", [])), "choice": len(ty.get("values", []))}.get(ty["kind"], 0)
    # enums with no explicit values list use the bounds count
    a = doffs[t]
    ptrs = struct.unpack(">%dI" % (1 + n), blob[a:a + 4 * (1 + n)])
    return [cstr(p) for p in ptrs]


def enum_pairs(t):
    nm = members(t)
    return nm[0], list(zip(TS[t]["values"], nm[1:]))


# ---- decoder --------------------------------------------------------------
class Bits:
    def __init__(self, data, pos=0):
        self.d, self.c = data, pos * 8

    def read(self, n):
        v, rem = 0, n
        while rem:
            i, p = self.c >> 3, self.c & 7
            if i >= len(self.d):
                raise EOFError("truncated at bit %d" % self.c)
            take = min(8 - p, rem)
            v |= ((self.d[i] >> p) & ((1 << take) - 1)) << (rem - take)
            self.c += take
            rem -= take
        return v

    def align(self):
        if self.c & 7:
            self.c += 8 - (self.c & 7)

    def bytes(self, n):
        self.align()
        i = self.c >> 3
        if i + n > len(self.d):
            raise EOFError("truncated blob")
        self.c += 8 * n
        return self.d[i:i + n]


def read_len(b, ln):
    if ln["fixed"]:
        return ln["min"]
    v = b.read(ln["bits"]) + ln["min"]
    if not (ln["min"] <= v <= ln["max"]):
        raise ValueError("length %d outside %d..%d" % (v, ln["min"], ln["max"]))
    return v


def decode(b, t, depth=0):
    ty = TS[t]
    k = ty["kind"]
    if k == "alias":
        return decode(b, ty["target"], depth)
    if k == "int":
        ln = ty["len"]
        v = b.read(ln["bits"]) + ln["min"]
        return v
    if k in ("u32", "u32b"):
        return b.read(32)
    if k == "u64":
        return b.read(64)
    if k == "bool":
        return bool(b.read(1))
    if k == "null":
        return None
    if k == "enum":
        tg = ty["tag"]
        v = b.read(tg["bits"]) + tg["min"] if not ty["fixed"] or tg["bits"] else tg["min"]
        if ty["fixed"]:
            if not (tg["min"] <= v <= tg["max"]):
                raise ValueError("enum %d out of range" % v)
        elif v not in ty["values"]:
            raise ValueError("enum %d not a member" % v)
        return v
    if k == "optional":
        return decode(b, ty["inner"], depth + 1) if b.read(1) else None
    if k == "array":
        n = read_len(b, ty["len"])
        return [decode(b, ty["elem"], depth + 1) for _ in range(n)]
    if k in ("blob", "blobv", "string"):
        n = read_len(b, ty["len"])
        raw = b.bytes(n)
        return raw.decode("utf-8", "replace") if k == "string" else raw
    if k == "bitarray":
        n = read_len(b, ty["len"])
        return [b.read(1) for _ in range(n)]
    if k == "choice":
        tg = ty["tag"]
        v = b.read(tg["bits"]) + tg["min"]
        if ty["fixed"]:
            idx = v - tg["min"]
        else:
            if v not in ty["values"]:
                raise ValueError("choice tag %d unknown" % v)
            idx = ty["values"].index(v)
        return (v, decode(b, ty["types"][idx], depth + 1))
    if k == "struct":
        nm = members(t)[1:]
        out = {}
        for i, ft in enumerate(ty["types"]):
            out[nm[i] or "f%d" % i] = decode(b, ft, depth + 1)
        return out
    raise ValueError("kind %s" % k)


def find_named(name):
    return [t for t in range(count) if members(t)[0] == name] if False else [
        t for t in range(count) if cstr(struct.unpack(">I", blob[doffs[t]:doffs[t] + 4])[0]) == name]


if __name__ == "__main__":
    ids = find_named("NNet::Game::EEventId::Enum")
    print("EEventId enum types:", ids)
    name, pairs = enum_pairs(ids[0])
    print(name, "bits", TS[ids[0]]["tag"]["bits"], "fixed", TS[ids[0]]["fixed"], "range", TS[ids[0]]["tag"]["min"], TS[ids[0]]["tag"]["max"])
    for v, n in pairs:
        print(v, n)
