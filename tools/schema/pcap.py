"""Read TCP payloads and game-event blobs out of a libpcap capture of a base-port session."""
import struct

BASE_PORT = 27151  # the host's base port in the captures


def packets(path):
    """Yield (seconds, 'host' | 'guest', payload) for every TCP segment that carries data."""
    d = open(path, "rb").read()
    link = struct.unpack("<I", d[20:24])[0]
    skip = {1: 14, 113: 16, 0: 4}[link]
    off, t0 = 24, None
    while off + 16 <= len(d):
        sec, frac, cap, _ = struct.unpack("<IIII", d[off:off + 16])
        off += 16
        pkt = d[off + skip:off + cap]
        off += cap
        t = sec + frac / 1e6
        t0 = t0 if t0 is not None else t
        if not pkt or pkt[0] >> 4 != 4 or pkt[9] != 6:
            continue
        ihl = (pkt[0] & 15) * 4
        tcp = pkt[ihl:]
        pay = tcp[(tcp[12] >> 4) * 4:]
        if pay:
            yield t - t0, ("host" if struct.unpack(">H", tcp[:2])[0] == BASE_PORT else "guest"), pay


class _Bits:
    def __init__(self, b):
        self.b, self.c = b, 0

    def read(self, n):
        v, rem = 0, n
        while rem:
            i, p = self.c >> 3, self.c & 7
            take = min(8 - p, rem)
            v |= ((self.b[i] >> p) & ((1 << take) - 1)) << (rem - take)
            self.c += take
            rem -= take
        return v


def turn_blob(payload):
    """(sequence, game-event bytes) of an e_turn record, or None. Turns that announce joiners are skipped."""
    b = _Bits(payload)
    if b.read(6) != 11:
        return None
    seq = b.read(16)
    b.read(16)
    if b.read(1):
        b.read(4)
    b.read(12)
    b.read(32)
    b.read(16)
    if b.read(1):
        return None
    if b.read(1):
        for _ in range(b.read(5)):
            b.read(1)
    b.read(1)
    n = b.read(11)
    if b.c & 7:
        b.c += 8 - (b.c & 7)
    data = bytes(payload[b.c >> 3:(b.c >> 3) + n])
    return (seq, data) if data else None


def event_blobs(path):
    """(seconds, sender, turn, bytes) for every non-empty game-event blob."""
    out = []
    for t, who, pay in packets(path):
        r = turn_blob(pay)
        if r:
            out.append((t, who, r[0], r[1]))
    return out
