"""A reference encoder written from the type table, random values for any type, and a check against
Blizzard's s2protocol. Run it to check the encoder against the decoder and, when S2PROTOCOL is set to a
directory holding the `s2protocol` package, against s2protocol for every event replays also contain."""
import os, sys, random, struct
from events import *


class W:
    def __init__(self):
        self.bits = []

    def write(self, v, n):
        # Same order as the reader: fill each byte from its low bit, a field that crosses a
        # byte puts its high slice first, and the bits of one slice keep their numeric order.
        rem = n
        while rem:
            room = 8 - (len(self.bits) & 7)
            take = min(room, rem)
            chunk = (v >> (rem - take)) & ((1 << take) - 1)
            for j in range(take):
                self.bits.append((chunk >> j) & 1)
            rem -= take

    def align(self):
        while len(self.bits) & 7:
            self.bits.append(0)

    def data(self):
        out = bytearray()
        for i in range(0, len(self.bits), 8):
            byte = 0
            for j, bit in enumerate(self.bits[i:i + 8]):
                byte |= bit << j
            out.append(byte)
        return bytes(out)

    def raw_bytes(self, bs):
        self.align()
        for x in bs:
            self.write(x, 8)


def encode(w, t, v):
    ty = TS[t]
    k = ty["kind"]
    if k == "alias":
        return encode(w, ty["target"], v)
    if k == "int":
        ln = ty["len"]
        w.write(v - ln["min"], ln["bits"])
    elif k in ("u32", "u32b"):
        w.write(v, 32)
    elif k == "u64":
        w.write(v, 64)
    elif k == "bool":
        w.write(1 if v else 0, 1)
    elif k == "null":
        pass
    elif k == "enum":
        tg = ty["tag"]
        w.write(v - tg["min"], tg["bits"])
    elif k == "optional":
        w.write(0 if v is None else 1, 1)
        if v is not None:
            encode(w, ty["inner"], v)
    elif k == "array":
        ln = ty["len"]
        if not ln["fixed"]:
            w.write(len(v) - ln["min"], ln["bits"])
        for x in v:
            encode(w, ty["elem"], x)
    elif k in ("blob", "blobv", "string"):
        ln = ty["len"]
        raw = v.encode() if isinstance(v, str) else v
        if not ln["fixed"]:
            w.write(len(raw) - ln["min"], ln["bits"])
        w.raw_bytes(raw)
    elif k == "bitarray":
        ln = ty["len"]
        w.write(len(v) - ln["min"], ln["bits"])
        for x in v:
            w.write(x, 1)
    elif k == "choice":
        tag, val = v
        tg = ty["tag"]
        w.write(tag - tg["min"], tg["bits"])
        encode(w, ty["types"][ty["values"].index(tag)], val)
    elif k == "struct":
        nm = members(t)[1:]
        for i, ft in enumerate(ty["types"]):
            encode(w, ft, v[nm[i] or "f%d" % i])
    else:
        raise ValueError(k)


def rnd(t, r, depth=0):
    ty = TS[t]
    k = ty["kind"]
    if k == "alias":
        return rnd(ty["target"], r, depth)
    if k == "int":
        ln = ty["len"]
        return r.choice([ln["min"], ln["max"], r.randint(ln["min"], ln["max"])])
    if k in ("u32", "u32b"):
        return r.choice([0, 0xFFFFFFFF, r.getrandbits(32)])
    if k == "u64":
        return r.choice([0, 2**64 - 1, r.getrandbits(64)])
    if k == "bool":
        return bool(r.getrandbits(1))
    if k == "null":
        return None
    if k == "enum":
        return r.choice(ty["values"])
    if k == "optional":
        return None if r.getrandbits(1) else rnd(ty["inner"], r, depth + 1)
    if k == "array":
        ln = ty["len"]
        n = ln["min"] if ln["fixed"] else r.choice([ln["min"], min(ln["max"], ln["min"] + 2), min(ln["max"], ln["min"] + r.randint(0, 3))])
        return [rnd(ty["elem"], r, depth + 1) for _ in range(n)]
    if k in ("blob", "blobv"):
        ln = ty["len"]
        n = ln["min"] if ln["fixed"] else min(ln["max"], ln["min"] + r.randint(0, 6))
        return bytes(r.getrandbits(8) for _ in range(n))
    if k == "string":
        ln = ty["len"]
        n = min(ln["max"], r.randint(0, 9))
        return "".join(r.choice("abcXYZ019 ") for _ in range(n))
    if k == "bitarray":
        ln = ty["len"]
        n = min(ln["max"], ln["min"] + r.randint(0, 9))
        return [r.getrandbits(1) for _ in range(n)]
    if k == "choice":
        i = r.randrange(len(ty["values"]))
        return (ty["values"][i], rnd(ty["types"][i], r, depth + 1))
    if k == "struct":
        nm = members(t)[1:]
        return {(nm[i] or "f%d" % i): rnd(ft, r, depth + 1) for i, ft in enumerate(ty["types"])}
    raise ValueError(k)


def event_bytes(eid, value):
    w = W()
    w.write(eid, 7)
    encode(w, EVENT_TYPE[eid], value)
    w.align()
    return w.data()


def flat(v, out):
    if isinstance(v, dict):
        for x in v.values():
            flat(x, out)
    elif isinstance(v, tuple):
        flat(v[1], out)
    elif isinstance(v, list):
        out.append(("len", len(v)))
        for x in v:
            flat(x, out)
    elif v is None:
        out.append(None)
    elif isinstance(v, (bytes, bytearray)):
        out.append(bytes(v))
    elif isinstance(v, str):
        out.append(v.encode())
    else:
        out.append(int(v) if not isinstance(v, bool) else bool(v))


def vectors(seed=75689, per=3):
    r = random.Random(seed)
    out = []
    for eid in sorted(EVENT_TYPE):
        for _ in range(per):
            val = rnd(EVENT_TYPE[eid], r)
            out.append((eid, val, event_bytes(eid, val)))
    return out


if __name__ == "__main__":
    vs = vectors()
    bad = 0
    for eid, val, data in vs:
        got = decode_stream(data)
        if len(got) != 1 or got[0][0] != eid:
            bad += 1
            print("python round trip failed for", eid)
            continue
        a, b = [], []
        flat(val, a)
        flat(got[0][2], b)
        if a != b:
            bad += 1
            print("python value mismatch for", eid, EVENT_NAMES[eid])
    print("python self-check:", len(vs), "vectors,", bad, "bad")

    # independent check: s2protocol decodes the same bytes for the events replays have
    if not os.environ.get("S2PROTOCOL"):
        print("set S2PROTOCOL to a directory with the s2protocol package for the cross-check")
        raise SystemExit(0)
    sys.path.insert(0, os.environ["S2PROTOCOL"])
    from s2protocol.versions import protocol75689 as p
    from s2protocol.decoders import BitPackedDecoder
    checked = skipped = diff = 0
    for eid, val, data in vs:
        if eid not in p.game_event_types:
            skipped += 1
            continue
        dec = BitPackedDecoder(data, p.typeinfos)
        try:
            got_id = dec.instance(p.game_eventid_typeid)
            ev = dec.instance(p.game_event_types[got_id][0])
            dec.byte_align()
            ok = got_id == eid and dec.done()
        except Exception as e:
            ok = False
            ev = str(e)
        a, b = [], []
        flat(val, a)

        def sflat(x, out):
            if isinstance(x, dict):
                for y in x.values():
                    sflat(y, out)
            elif isinstance(x, list):
                out.append(("len", len(x)))
                for y in x:
                    sflat(y, out)
            elif x is None:
                out.append(None)
            elif isinstance(x, (bytes, bytearray)):
                out.append(bytes(x))
            elif isinstance(x, str):
                out.append(x.encode())
            else:
                out.append(x if isinstance(x, bool) else int(x))

        if ok:
            sflat(ev, b)
        checked += 1
        if not ok or [x if not isinstance(x, bool) else int(x) for x in a] != [x if not isinstance(x, bool) else int(x) for x in b]:
            diff += 1
            if diff <= 8:
                print("s2protocol differs for", eid, EVENT_NAMES[eid], "ok=%s" % ok)
                print("   ours  ", a[:8])
                print("   theirs", b[:8] if ok else ev)
    print("s2protocol cross-check:", checked, "checked,", diff, "differ,", skipped, "live-only events skipped")
