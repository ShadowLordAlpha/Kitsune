"""The NNet::Game event table: id, enumerator name, and the struct each id carries."""
import struct
from gameschema import *

# Struct names that do not follow e_fooBar -> SFooBarEvent. Checked by hand against the table.
# Id 113 has no struct of its own; SMuteChatEvent is the nearest name and is not confirmed.
OVERRIDE = {
    33: "NNet::Game::SAICommunicateEvent",
    63: "NNet::Game::STriggerPlanetPanelReplayEvent",
    68: "NNet::Game::STriggerPlanetPanelBirthCompleteEvent",
    69: "NNet::Game::STriggerPlanetPanelDeathCompleteEvent",
    84: "NNet::Game::STriggerBattleReportPanelSelectionChangedEvent",
    90: "NNet::Game::STriggerCustomDialogDismissedEvent",
    113: "NNet::Game::SMuteChatEvent",
}


def raw_name(t):
    return cstr(struct.unpack(">I", blob[doffs[t]:doffs[t] + 4])[0])


def up(s):
    return s[:1].upper() + s[1:]


_by_name = {}
for _t in range(count):
    _by_name.setdefault(raw_name(_t).lower(), _t)

_ids = find_named("NNet::Game::EEventId::Enum")[0]
_, _pairs = enum_pairs(_ids)
EVENT_NAMES = dict(_pairs)  # id -> e_name
EVENT_TYPE = {}  # id -> struct type
for _i, _e in EVENT_NAMES.items():
    _full = OVERRIDE.get(_i, "NNet::Game::S" + up(_e[2:]) + "Event")
    _t = _by_name.get(_full.lower())
    if _t is None:
        raise SystemExit("no struct for event %d %s (%s)" % (_i, _e, _full))
    EVENT_TYPE[_i] = _t


def net_family(ns):
    """(id, e_name, struct type) for NNet::System or NNet::Local. The struct is NNet::<ns>::S<Name>Event."""
    ids_t = find_named("NNet::%s::EEventId::Enum" % ns)[0]
    _, pairs = enum_pairs(ids_t)
    out = []
    for i, e in pairs:
        base = up(e[2:])
        if base.endswith("Event"):
            base = base[:-5]
        t = _by_name.get(("NNet::%s::S%sEvent" % (ns, base)).lower())
        if t is None:
            raise SystemExit("no struct for %s %s" % (ns, e))
        out.append((i, e, t))
    return out


def decode_stream(data):
    """Decode a game-event blob: 7-bit id, struct, pad to a byte, repeat. Raises if it does not fit."""
    b = Bits(data)
    out = []
    while b.c < len(data) * 8:
        eid = b.read(7)
        t = EVENT_TYPE.get(eid)
        if t is None:
            raise ValueError("no struct for id %d" % eid)
        out.append((eid, EVENT_NAMES[eid], decode(b, t)))
        b.align()
    return out
