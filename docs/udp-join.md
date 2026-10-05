# UDP join

The 4.10 game-port datagrams are one `bsnc` bitstream, not a type byte plus a
fixed body. `CLocalNetImpl::SendTo` builds a bit buffer and
`NNet::Local` events are written through the protocol object at `0x606ac50`.
That object is filled at startup from the bytecode at `0x319b140` (version 7,
1842 types). The first field of every event is `NNet::Local::EEventId`, 4
bits:

```text
0 e_doesAnyoneHaveAGame
1 e_IHaveThisGame
2 e_showMeThisGame
3 e_IDontHaveThisGame
4 e_hereIsThisGame
```

The captured hello is event 1, `SIHaveThisGameEvent`, 184 bits (23 bytes).
Re-encoding the fields below reproduces both captured datagrams. The shared
prefix is `SHeader` plus `m_sessionId`. The last 20 bits are four 5-bit
counters, which is why the fixture ends `09 60` and the live hello ends
`05 70` while every earlier byte matches.

```text
event            4 bits   1
m_programId     32 bits   0x5332 ('S2')
m_flags          8        1
m_major          8        4
m_minor          8        10
m_revision       8        0
m_build         32        75689
m_baseBuild     32        75689
m_sessionId     32        1
m_numUsers       5        1
m_maxUsers       5        fixture 4, live 2
m_numObservers   5        0
m_maxObservers   5        fixture 12, live 14
```

One fill of that object is at `0x1b6ae60`. It sets the tag to `0x60001`,
copies endpoint `+0x08` to message `+0x24` (`m_programId`), the qword at
endpoint `+0x0c` to message `+0x28` (flags through `m_build`), endpoint
`+0x14` to message `+0x30` (`m_baseBuild`), and endpoint `+0x18` to message
`+0x34` (`m_sessionId`). The dword at endpoint `+0x19e0` is split into the
four bytes at message `+0x38`, and those bytes are the 5-bit counters.
Integers are offset from the schema minimum and packed MSB-first into the
buffer, with bit 0 of the first byte as the first bit. Other sites in this
function also allocate a `0x60001` object.

Events 2 and 3 are `SShowMeThisGameEvent` and `SIDontHaveThisGameEvent`:
the same header and session id, no counters. That is 164 bits, padded to 21
bytes. With event 2 and event 3 they are the fixture's type `02` and type
`03`. Event 4 is `SHereIsThisGameEvent`, a kilobyte or more, so it only
encodes into a larger buffer than the 64 bytes the join uses for events 0
through 3.

A capture that reached "Game has started" is the byte fixture:

```text
01 00 05 33 02 01 04 0a 00 00 12 7a 09 00 12 7a 09 00 00 00 01 09 60
02 00 05 33 02 01 04 0a 00 00 12 7a 09 00 12 7a 09 00 00 00 01
03 00 05 33 02 01 04 0a 00 00 12 7a 09 00 12 7a 09 00 00 00 01
```

The guest stays silent until `IHaveThisGame`, then answers with
`ShowMeThisGame`. That answer keeps `SHeader` and `m_sessionId` and drops
the four counters. The host's answer to a matching show-me is
`HereIsThisGame`, not `IDontHaveThisGame`. The two names are different
exchanges. `IDontHaveThisGame` is a separate datagram the host emits from
its own peer list. A capture that reached "Game has started" does contain
all three short datagrams, in the order hello, show-me, then
`IDontHaveThisGame`.

Sockets are Asio IPv4 UDP. The Linux client passes domain 10 to `socket()`.
That number is `AF_INET6` on Linux and the wrong value on Windows, so it
stays in `DatagramOpen` as evidence and is not passed through. The join's
host address is IPv4, and Windows does not deliver those datagrams to a
dual-stack IPv6 socket on loopback.

`tests/net/udp_join_test.cpp` runs both sides on the IPv4 loopback and checks
the three payloads. `ctest` runs that test.

`tests/live/sc2_udp_join.cpp` is the live check. The argument is `guest` or
`host` (the default). Default `ctest` skips the binary (`DISABLED`, label
`live`). A playable game is not part of the result. The same binary covers
both directions; one official client is only one of the two roles per run.

`guest` sends `RequestCreateGame` and `RequestJoinGame`, so Base75689 hosts.
Kitsune binds `27152`, answers `IHaveThisGame` with `ShowMeThisGame`, and
saves the last here-is (preferring a copy whose `m_sessionAddress` is filled)
to `/tmp/kitsune-hereis.bin`. `host` sends only `RequestJoinGame`, so
Base75689 is the guest. Kitsune binds `27150`, advertises the session from
that saved datagram, and answers show-me with those exact bytes. The in-process
loopback still sends an empty description unless `useHereIsDatagram` supplies
one. Neither role writes on the base port.

The two processes have to share a network namespace. Base75689 here is the
Linux build under `C:\Users\Shadow\Downloads\SC2.4.10`. `127.0.0.1` in that
guest is not the Windows host. Run the Linux binary from WSL:

```sh
./build/linux-debug/tests/kitsune_sc2_udp_join
```

The Windows 4.10 client works the same way when it is started natively. Set
`KITSUNE_SC2_WINDOWS` to its install root and run the Windows build of the
same binary (`build/windows-msvc-debug/tests/kitsune_sc2_udp_join.exe`); the
here-is file and the client log then go in the system temp directory.

The binary stops about fifteen seconds after the UDP exchange. Set
`KITSUNE_LIVE_SECONDS` to a number of seconds to keep it (and the base-port
session, `docs/tcp-base.md`) running longer. `KITSUNE_REPLAY_OUT` names a file to receive the
replay the official client writes at the end.

An earlier run with the roles reversed received the hello above and answered
with show-me. That official host retransmitted the hello and, with each copy,
a 1323-byte here-is. Later copies contain `7f 00 00 01` where the first copy
had zeros. That pattern sits in the here-is body and has not been split into
`m_sessionAddress`. The same run dialed `27151` and listened on `27153`, wrote
nothing, and neither socket carried a payload.

With Kitsune hosting, the official guest answered the advertisement with one
21-byte show-me:

```text
02 00 05 33 02 01 04 0a 00 00 12 7a 09 00 12 7a 09 00 00 00 01
```

That is event 2, program `0x5332`, version 1.4.10.0 build 75689, base build
75689, session 1. Nothing else arrived on the game port. The host base port
`27151` accepted no connection. The client log says it tried to join a game
whose map name was empty, then "Client unable to join a hosted game." A later
host run sends the here-is saved by `guest` instead of that empty description.
With the here-is in place the official guest dials `27151`, and the base-port
handshake that follows is in `docs/tcp-base.md`.

## Packing

Every local event is one bitstream. Bit 0 of the first byte is the first bit.
An integer is stored as `value - minimum` in the schema width. A 0-bit field
writes nothing. When a field crosses a byte, the high slice of the remaining
value is written at the earlier bit. Unused bits of the last byte stay 0, and
a decode requires those pad bits to be 0 and the cursor to end on the
datagram size.

`SHeader` is `m_programId` (FourCC, 32 bits) then `NNet::SVersion`: flags,
major, minor, and revision are Int8, and build and baseBuild are Int32. All
of those minima are 0. C++ members drop the schema `m_` prefix.

Strings and blobs are the only fields that insert alignment. Fog, speed, type,
and observe are enums with the client's names (`include/Kitsune/Net/Enums.h`,
generated; see [docs/game-events.md](game-events.md), "Where the schema is").

```text
optional          1 presence bit, then the inner value only when set
array             count integer, then elements, no alignment between them
bit array         length integer, then that many raw 0/1 bits, no alignment
string            length uses schemaBits + 2, max bytes is schemaMax << 2,
                  then zero-pad to a byte, then that many bytes, no extra NUL
ordinary blob     length in the schema width, then zero-pad, then the bytes
fixed blob        no length; zero-pad, then exactly 40 bytes
```

String widths used by these datagrams:

```text
String6  0..32    8-bit length,  at most 128 bytes   names, clan tag, creator
String6  0..50    8-bit length,  at most 200 bytes   map author
String5  0..19    7-bit length,  at most 76 bytes    toon handle
String7  0..127   9-bit length,  at most 508 bytes   hero, skin, mount
String8  0..255   10-bit length, at most 1020 bytes  game cache name
String9  0..260   11-bit length, at most 1040 bytes  map file name
```

A length of 2 does not show the extra two bits, because those bits are zero.
A 512-byte String9 path does: the length width is 11, so byte 14 of a
zero-header `e_sessionUserTransfer` is `0x50`. That check lives on the TCP
codec (`docs/tcp-base.md`). 1041 bytes is rejected.

The cache-handle and clan-logo type byte is `84 01 28`. The high bit forces
a fixed size of 40 and no length prefix. An older dump printed `Blob40 15..1`
by reading the next type's bytes. That range is wrong. The value must be
exactly 40 bytes.

`SNetAddress` is `m_version` Int8, `m_address` as 0..8 Int16 words (4-bit
count), then `m_port` Int16. It is not an IPv4 tuple. The captured `7f 00 00
01` still sits inside a here-is body and has not been split into version,
words, and port.

`TRacePreference` and `TTeamPreference` add no bits of their own. The wire
field is the optional race (Int8 0..254) or team (Int8 0..255). 255 is not a
legal race.

## Local events

`include/Kitsune/Net/LocalEvent.h` is generated from the client's table
(`tools/schema/gen_cpp.py`, [docs/game-events.md](game-events.md)), the same way as
`GameEvent.h`: one struct per event id (`DoesAnyoneHaveAGameEvent`,
`IHaveThisGameEvent`, `ShowMeThisGameEvent`, `IDontHaveThisGameEvent`,
`HereIsThisGameEvent`) and `LocalEvent`, a `std::variant` over them in id order,
so `localEventId(event)` is the variant's index. `encodeLocalEvent` and
`decodeLocalEvent` take every id; ids above 4 are not events and are rejected.
`encodeHereIs` / `decodeHereIs` are the same codec for a caller that already
holds a here-is. Each struct starts with a `header` member (`LocalHeader`).
`Lockstep.h` is written by hand: `sameSession`, `describeLocalEvent`, the
here-is wrappers, and `GuestLockstep` and `HostLockstep`, the two small state machines. The join machine ignores a here-is until it has
answered an `IHaveThisGame`.

`UdpJoin::setLog` is optional and empty by default. On each send of
`IHaveThisGame`, `ShowMeThisGame`, or `IDontHaveThisGame`, and on each
receive, it gets one line from `describeLocalEvent` or `describeHereIs`.
A datagram that is neither is `udp undecoded bytes=N`. The live guest prints
that sink. Unit tests leave it unset.

```text
udp e_IHaveThisGame session=N build=B users=n/m observers=n/m
udp e_hereIsThisGame session=N users=n/m observers=n/m map=file
```

`sameSession` compares program id, the six version fields, and session id.
It does not compare the four counters.

### 0 `e_doesAnyoneHaveAGame`

Slow broadcast asking which host has a game. The body is `SHeader` only.
That is 4 + 128 = 132 bits, 17 bytes. No capture is in the notes. The codec
round-trips a constructed record.

### 1 `e_IHaveThisGame`

Rapid host announcement of one session. Header, `m_sessionId`, then the four
Int5 counters in the hello table above (each 0..16). 184 bits, 23 bytes. A
count above 16 is rejected. While the host's peer count is nonzero, a tick
retransmits this for each peer whose timer has passed `0x7d1`. The fill at
`0x1b6ae60` and the retransmit at `0x1b6a8b4` tag the object `0x60001`.
That tag is not on the wire.

### 2 `e_showMeThisGame`

The guest asks that host to show the matching session. Header and
`m_sessionId` only. 164 bits, 21 bytes. The guest stays silent until an
`IHaveThisGame` arrives, then answers with this event. The builder at
`0x1b6bfce` copies program id, version, and session id off the datagram it
just received, and tags the object `0x60002`.

### 3 `e_IDontHaveThisGame`

A notice that this peer does not have the named session. Same shape as
show-me: header and `m_sessionId`, 21 bytes. It is not the reply to
show-me. The sender at `0x1b6b7f0` (vtable `0x31ae4c8`) runs only while the
peer count at session `+0xf0` is nonzero. It copies program id, version,
and session id from the saved peer, sends the datagram, then decrements
that count. The four counters are left off. A stub game that reached
"Game has started" did capture this datagram from the host, after the
base-port handshake had settled. The live probe that only opened the base
sockets, and wrote nothing, saw hello and here-is, and did not see this
event.

The Kitsune loopback answers show-me with `HereIsThisGame` for that
session. With no captured datagram the description is empty apart from the
counts copied off the advertisement. `useHereIsDatagram` replaces that body
with bytes already captured from a 4.10 host. The loopback does not emit
`IDontHaveThisGame`.

### 4 `e_hereIsThisGame`

The map datagram, and the host's answer when an incoming datagram names
its program and one of its sessions. The handler at `0x1b6c110` compares
the incoming program id with the local one and the incoming session id
with each saved peer, then calls the builder at `0x1b6c190` (object tag
`0x60004`). The live run showed the host sending these beside the hello
after our guest had sent show-me. Observed copies were 1323 bytes. The
size follows the description, so the codec does not force 1323. With
program id `0x5332` the first byte is `0x04`, because the next four bits
are the top of that FourCC and those bits are 0. A different program id
changes that byte.

After `SHeader`:

```text
m_sessionId            Int32
m_sessionSecret        Int32
m_sessionAddress       SNetAddress
m_creatorName          String6 0..32
m_gameDescription      SGameDescription
m_numUsers             Int5 0..16
m_maxUsers             Int5 0..16
m_numObservers         Int5 0..16
m_maxObservers         Int5 0..16
m_userInitialData      array5 0..16 of SUserInitialData
```

The four counters here are the event's own counts. The description below has
a second `m_maxUsers`, `m_maxObservers`, and `m_maxPlayers`. Those are nested.

A guest run against Base75689 saved one 1323-byte here-is for
`Melee/Simple64.SC2Map`. `host` sends those bytes unchanged. Decoded:

```text
session 1
secret 220483981
creator foo8167
sessionAddress version 4, eight zero words, port 27151
map checksum 3529290087
author empty
game cache Dflt
map size 88 by 96
speed 4, type 0
description users 2, observers 14, players 2
teams 2, colors 16, races 3, controls 1
random 2010689789
mod checksum 1941099290
16 slot descriptions, each bit-array length colors 16, races 3,
difficulty 32, controls 255, observe 3, ai builds 255
default difficulty 3, default AI build 0
no cache handles
extension, non-blizzard, blizzard, premade FFA, coop, realtime all clear
options: only heroDuplicatesAllowed is set; fog 0, observers 0,
user difficulty 0, clientDebugFlags 0, build coach off
userInitialData empty
event counts users 1/2, observers 0/14
```

The eight address words are zero in this copy. Port `27151` is the host base
port. The earlier `7f 00 00 01` pattern was not in these address words.

## Game description

`SGameDescription`, in order. `m_maxTeams` is 1..16 (4 bits, wire is
`value - 1`; 0 is rejected). `m_maxRaces` is 1..255 (8 bits, wire is
`value - 1`).

```text
m_randomValue                  Int32
m_gameCacheName                String8 0..255
m_gameOptions                  SGameOptions
m_gameSpeed                    enum 0..4, 3 bits
m_gameType                     enum 0..6, 3 bits
m_maxUsers                     Int5 0..16
m_maxObservers                 Int5 0..16
m_maxPlayers                   Int5 0..16
m_maxTeams                     Int4 1..16
m_maxColors                    Int6 0..32
m_maxRaces                     Int8 1..255
m_maxControls                  Int8 0..255
m_mapSizeX                     Int8 0..255
m_mapSizeY                     Int8 0..255
m_mapFileSyncChecksum          Int32
m_mapFileName                  String9 0..260
m_mapAuthorName                String6 0..50
m_modFileSyncChecksum          Int32
m_slotDescriptions             array5 0..16 of SSlotDescription
m_defaultDifficulty            Int6 0..32
m_defaultAIBuild               Int8 0..255
m_cacheHandles                 array6 0..32 of fixed 40-byte blobs
m_hasExtensionMod              bool
m_hasNonBlizzardExtensionMod   bool
m_isBlizzardMap                bool
m_isPremadeFFA                 bool
m_isCoopMode                   bool
m_isRealtimeMode               bool
```

`SGameOptions`, in order. The eleven bools are one bit each.

```text
m_lockTeams
m_teamsTogether
m_advancedSharedControl
m_randomRaces
m_battleNet
m_amm
m_competitive
m_practice
m_cooperative
m_noVictoryOrDefeat
m_heroDuplicatesAllowed
m_fog                 enum 0..3, 2 bits
m_observers           enum 0..3, 2 bits
m_userDifficulty      enum 0..2, 2 bits
m_clientDebugFlags    Int64, 64 bits, minimum 0
m_buildCoachEnabled   bool
```

`SSlotDescription` is six bit arrays. Each is a length, then that many 0/1
bits, with no byte pad.

```text
m_allowedColors         BitArray6 0..32
m_allowedRaces          BitArray8 0..255
m_allowedDifficulty     BitArray6 0..32
m_allowedControls       BitArray8 0..255
m_allowedObserveTypes   BitArray2 0..3
m_allowedAIBuilds       BitArray8 0..255
```

## User initial data

`SUserInitialData` is one entry of the here-is user list. The same record is
the user payload on the base port (`docs/tcp-base.md`). Lobby slots are not
repeated here.

```text
m_name                 String6 0..32
m_clanTag              optional String6 0..32
m_clanLogo             optional fixed 40-byte blob
m_highestLeague        optional Int8 0..255
m_combinedRaceLevels   optional Int32
m_randomSeed           Int32
m_racePreference       optional Int8 0..254
m_teamPreference       optional Int8 0..255
m_testMap              bool
m_testAuto             bool
m_examine              bool
m_customInterface      bool
m_testType             Int32
m_observe              enum 0..2, 2 bits
m_hero                 String7 0..127
m_skin                 String7 0..127
m_mount                String7 0..127
m_toonHandle           String5 0..19
m_scaledRating         optional Int32, full signed range, 32 bits
```

`m_scaledRating` is signed. The wire value is the unsigned difference from
`INT32_MIN`. A rating of -1 or -2 round-trips. An empty optional writes only
the presence bit.
