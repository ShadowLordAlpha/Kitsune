# Base-port TCP

The base port carries `NNet::System` events. `SendOnly` (`0x1b864d0`) packs
them with the same bitstream as the game-port events, through the protocol
object at `0x606ac50`. The event id is `NNet::System::EEventId`, 6 bits.
The cursor rules (bit 0 first, `value - minimum`, string length `schemaBits
+ 2`, blob alignment, fixed 40-byte handles) are the same ones in
[docs/udp-join.md](udp-join.md). C++ members drop the schema `m_` prefix.
Error, phase, and leave reason are enums with the client's names
(`include/Kitsune/Net/Enums.h`, generated; see [docs/game-events.md](game-events.md)).

The C++ is generated from the client's table (`tools/schema/gen_cpp.py`,
[docs/game-events.md](game-events.md)): `SystemEvent` is a `std::variant` of one struct per
event id, each starting with a `header` member. Ids 20 through 29, 31 through 39, and 55
and above are not events, and `decodeSystemEvent` and `decodeSystemStream` reject them.
`systemEventId` and `systemHeader` read the id and header of any event.

`describeSystemEvent` names a decoded record. `describeTcpPayload` uses
it when a read is one complete record, and otherwise reports
`tcp undecoded bytes=N`. `BaseConn` still tracks connection state only and
does not read or write these events. The live base probe follows the UDP
role. As host it accepts on `27151` and does not dial. As guest it dials
`27151` and accepts on `27153`. It reads with `decodeSystemStream`, runs
`HostHandshake` or `GuestHandshake` (`Kitsune/Net/BaseHandshake.h`), and
writes the replies. Every record read or sent is logged as
`describeTcpPayload` plus the full hex. A decoded line looks like:

```text
tcp e_ackTurn seq=N ack=N peer=N
```

Connect adds `build` and `port`. Turn adds `sync` and `events`. Message adds
`bytes`. Join-succeeded adds `users`.

`CNetConnectionTCP` is the socket object. Its send method is `0x2653a80`.
It writes immediately when the state is 4 and the queue is empty, and
otherwise queues a 0x5d0-byte node.

## Stream framing

TCP carries no length prefix and no delimiter. A record ends where its schema
ends: `ceil(bits read / 8)` bytes, and the next record starts at the next
byte. The unused bits of the last byte are zero. `decodeSystemStream` returns
one of three results for the front of a byte buffer:

```text
Complete    one record decoded; drop `consumed` bytes and decode again
Truncated   the data ran out inside a record; keep the bytes and wait
Bad         unknown id, a field out of range, or non-zero pad bits
```

The official receive path does the same. It reads into a buffer of 2920
bytes (`0xb68`, `kStreamBufferBytes`), decodes one event, drops the bytes it
used, and moves the remainder to the front. A decode that runs out of data
consumes nothing and waits. Any other failure is error 13 and the event is
dropped. A full buffer that yields no record closes the connection. The
receive pump is `0x2653e60` and the decoder wrapper is `0x1b80cb0`, which logs
`OnReceive: Event Valid`, `Event Truncated.` or `bad event`.

On loopback every segment in the capture was exactly one record, but a real
network may split or merge them. Read with `decodeSystemStream` in a loop and
do not assume a segment boundary is a record boundary.

## Session order

Captured from an official Base75689 Linux host and guest on loopback (house
bots, one 1v1, 120 game seconds). The guest dials the host's base port from
an ephemeral port. `m_listenPort` inside `e_connect` is the guest's own base
port, not the source port of the connection.

```text
guest -> host   e_connect         seq 0  ack 0   build 75689, listen port
host  -> guest  e_connectReply    seq 1  ack 1
guest -> host   e_join            seq 1  ack 2
host  -> guest  e_joinSucceeded   seq 2  ack 2   lobby state, one user
both            e_turn            sequences continue from about 18
```

Each side puts its own random 32-bit peer id in `m_originatingPeer` and
keeps it for the whole session. The system events, the turns, and the
messages each count on their own: `e_message` and `e_ackMessage` run 0, 1, 2
while turns are in the twenties. On the turn channel the sequence is the turn
number. Host and guest each send turn N with `seq N`, and `ack` is the next
turn expected from the other side (the highest received plus one). After the
join the two sides exchange `e_turn` in lockstep, one record per side per
turn. A turn with no game events is 15 bytes. About four seconds in, both
sides send an `e_message` and a turn of about 1.1 KB, then the stream settles into
`e_ping` and `e_pingReply` keepalives and acks when no turn is flowing. In
the one full game, 5,415 of 5,431 records were turns.

### The sync value

`m_syncValue` is a game-state checksum, not a counter and not the game loop.
At the same turn sequence the host and guest carry the same value (2,698 of
2,699 sequences in the full-game capture; the exception is the guest's first
turn). The sender (`0x1b18f60`, run once per turn) does this:

```text
first turn after a join        sync = 0
every other turn               crc = 0xffffffff
                               fold the game state for the current loop into crc
                               sync = ((crc >> 16) ^ crc) & 0xffff
```

The fold (`0x1b14690`) also writes the checksum into a per-loop history
record, which is what the client keeps for desync analysis. The CRC is the
standard reflected CRC32 (`0x324f2c0` table, inverted start and end). The
game loop number is passed to the send call as a separate argument and is not
the sync value.

The receiver (`0x1b1a5d0`) gathers each present user's sync value for the
turn. A value of 0 is skipped. If two different non-zero values remain, it
raises "Multiplayer Desync". So a peer that does not simulate the game is not
a desync as long as it sends 0, and that is what the first turn of any joiner
already does. Producing the real value needs the simulation (units, RNG, and
the rest of the hashed state) and is a `kitsune::sim` task. In the lobby the
hashed state is small and the same on both sides, which is why the values
match even with no game events.

### Handshake

`GuestHandshake` and `HostHandshake` are byte-in, byte-out state machines for
the four records above and the guest's `e_ackSystem`. Given the values an
official pair used, they produce the official records byte for byte, which
`tests/net/base_handshake_test.cpp` checks in both roles. The rules they
follow, all from the capture:

- `e_connect` carries the secret the host advertised in its here-is, not the
  guest's own. The guest's own secret appears first in `e_join`.
- `e_connectReply` carries the address the host saw the guest connect from,
  with the guest's `m_listenPort`, and error 0.
- `e_join` puts that same address as the external address and leaves the
  internal one empty. `m_ready` is set.
- On the system channel, an ack record carries the next expected sequence in
  both fields, and any other record's `ack` is the sequence just received
  plus one. The turn channel counts differently (see above) and is not
  covered here.
- The host's first system sequence is 1 and the guest's is 0.

A mismatched secret, session, or program is refused by closing, with no
reply. The 4.10 error ids a host sends for a refusal were not captured, so
none is invented.

The host's lobby in the live runs is the captured one with this session's
identity put in, so it fits a 1v1 melee map only.

### Turn channel

`TurnChannel` (`Kitsune/Net/TurnChannel.h`) runs the turns after the join for
a peer that does not simulate. The numbers come from `e_joinSucceeded`:
`m_syncSequence` is the host's first turn (18 in the capture) and
`m_joinSequence` is the guest's first turn (20).

```text
host   sends turn S (empty), then turn S+1 with m_joiners = the guest
          (tag from m_joinerTag, its UserInfo, syncSequence S, ready)
guest  after host turn N (N >= J-1) sends turn N+1, starting at J
host   answers each guest turn N with its own turn N
every turn: ack = next turn expected from the peer, sync 0
            m_gameEventData = whatever was queued, up to 1452 bytes
```

Before the guest's first turn the host's acks name S. A turn that is not the
next expected one is ignored: a repeat means our ack has not arrived, and the
next turn we send carries it. The official host repeats turns until it is
acknowledged. `tests/net/turn_channel_test.cpp` feeds it the captured turns
and checks that the first guest turn matches the official one byte for byte,
and that two of our own channels keep each other going for a hundred turns.

Game events ride the same turns. `queue` takes events (`kitsune::net::game::Event`,
[game-events.md](game-events.md)) and each turn we send carries as many as fit in
1452 bytes, oldest first; the rest wait for the next turn. An event that cannot be
encoded is dropped. Every accepted turn of the peer is decoded, and an event handler,
if one is set, is called with the turn number and its events before our answer is
built, so what it returns goes out in that answer. A blob that does not decode in
full is counted (`undecodedTurns`) and the events before the bad one are still
delivered.

### Game setup

`GameSetup` (`Kitsune/Net/GameSetup.h`) is the handler that runs the lobby and
loading exchange of [game-events.md](game-events.md), "The setup exchange". It
reads the peer's events and returns ours:

```text
guest  our second turn      e_setLobbySlot {slot 1, race}
       on e_startGame       phase Starting
       on the host's package (ends with its first e_userFinishedLoading)
                            send e_unpauseGame 14, copies of the host's two
                            e_triggerSoundLengthSync tables, e_userOptions, e_userFinishedLoading
       two turns later      e_userFinishedLoading again; phase Playing
host   the guest's slot     two guest turns later e_startGame
       two turns later      e_peerSetSyncLoadingTime (now), the package
       two turns later      e_userFinishedLoading
       two turns later      e_peerSetSyncPlayingTime (loading time + 4)
```

`tests/net/game_setup_test.cpp` checks both roles against the capture: the
slot event, the guest's package, the host's package, and `e_startGame` all come out
byte for byte as the official peers sent them. The sound tables are the map's, so a
guest must copy the host's. A host has no tables of its own and copies the guest's.

### Message channel

`e_message` carries an opaque blob on its own sequence, and each one is answered
with `e_ackMessage` whose sequence and ack both name the next message expected.
`MessageChannel` (`Kitsune/Net/MessageChannel.h`) does both: `onMessage` keeps the
blob and returns the ack (a repeat is acked again, a gap is ignored) and `send`
builds a message of our own, numbered with our sequence and acking theirs. The ack
and our first message are checked against the official guest's bytes in
`tests/net/message_channel_test.cpp`.

The blobs are five bytes, `82 00 00 aa bb`. In the one capture each side sent two
(`05 01`, then `06 03`) and the two sides' blobs were equal; the Windows host in the
live runs sent five (`04 xx`, `05 xx`, `05 xx`, `05 xx`, `06 03`). What they mean is
not known. They come at the moments the game changes state: the first with the
loading package, the second with the second `e_userFinishedLoading`.

### Keepalive

When no turn flows the official peers send `e_ping` and expect `e_pingReply`. The
reply is numbered with the ping's `ack` and acks the ping's `sequence` plus one
(`answerPing`, `Kitsune/Net/KeepAlive.h`, checked against a captured pair in
`tests/net/keep_alive_test.cpp`).

### Live results

Live, from `tests/live/sc2_udp_join.cpp`, against an official 4.10.0.75689
client in both roles, joins and turns only (no game events):

| Client | Kitsune as | Result |
|---|---|---|
| Linux, from WSL | guest | joined, 237 turns in lockstep, host stopped repeating turns |
| Linux, from WSL | host | joined, 222 turns, official guest's game events seen |
| Windows, native | guest | joined, 238 turns |
| Windows, native | host | joined, 230 turns |

The official peers sent non-zero sync values and we sent 0. No desync was
raised, as the receiver rules above predict.

With `GameSetup`, a Kitsune guest against the official Windows host:

- The host accepted `e_setLobbySlot`, sent `e_startGame` two turns later, then its
  1103-byte package. Ours went back as a 1098-byte package, as the official guest's
  does, and both sides sent their second `e_userFinishedLoading`. Every event the host
  sent decoded (`undecodedTurns` 0).
- **The game runs.** In a 40 second run the turns went past 585 in lockstep, the host
  sent `e_cameraUpdate` events, and `save_replay` on the host's websocket returned a
  valid 4.10.0.75689 replay of 811 game loops (8923 bytes, read with s2protocol's
  header decoder). Kitsune sent no commands and a sync value of 0 throughout, and no
  desync or drop followed. The replay is the goal's first milestone: an official client
  wrote it with Kitsune as the other peer.
- **An early stall was the API, not the protocol.** The first runs stopped after turn 21
  with the host sending only `e_ping`. The probe had created a stepped game, which only
  advances when the API client calls `step`, and nothing called it. `create_game` with
  `realtime` set fixed it. Neither the unanswered pings nor the message acks were the
  cause, but both are needed: before the ping reply was added the host sent 222 pings
  in a minute, with it 6, and before the message acks the host repeated `e_message` 0
  every half second.
- With the message channel the host's five messages were each acked, and the probe
  sent back each blob it received (the official guest sends blobs equal to the
  host's). Whether sending them is needed has not been tested.
- Not yet: a Kitsune host against an official guest, the Linux client with
  `GameSetup`, and any commands. The replay holds no player actions.

The Windows runs start the client directly: set `KITSUNE_SC2_WINDOWS` to the
install root (`C:\Program Files (x86)\StarCraft II`) and run the live binary
on Windows. Without it the live binary uses the Linux client under WSL. The live
binary stops about fifteen seconds after the exchange; `KITSUNE_LIVE_SECONDS=60`
keeps it going longer (it was too short for the first runs above to tell a stalled
host from a finished probe). `KITSUNE_REPLAY_OUT=<path>` asks the client for a replay
(`save_replay`) at the end and writes it there.

The fixtures in `tests/fixtures/base_port_capture.h` are real records from the
capture. `tests/net/system_stream_test.cpp` decodes and re-encodes each one
byte for byte, feeds them back to back and in small chunks, and checks that
every strict prefix waits.

## Header

Every event starts with the 6-bit id and then `SHeader`.

```text
m_sequence            Int16 0..65535
m_ackSequence         Int16 0..65535
m_originalSenderTag   SUserTag
m_originatingPeer     Int32
```

`SUserTag` is one presence bit, then `m_userId` Int4 0..15 only when that
bit is set, then `m_recycle` Int12 0..4095. User id 16 and recycle 4096 are
rejected. With no user id the header is 77 bits after the event id: 83 bits,
11 bytes. A present user id is still 11 bytes (87 bits). The bytes after a
captured prefix are the tag and the peer. They are not a separate token.

The captured prefix `03 00 1a 00 1a 00` is `e_ackTurn`, sequence 26, ack
sequence 26. Zero peer and zero recycle:

```text
03 00 1a 00 1a 00 00 00 00 00 00
```

Peer `0xA1B2C3D4`:

```text
03 00 1a 00 1a 00 a0 36 58 7a 04
```

User id 7, recycle 0, peer 0:

```text
03 00 1a 00 5a 07 00 00 00 00 00
```

A zero header's first byte is the event id, because the following bits are
0. `e_resendMessage` is `11 00 00 00 00 00 00 00 00 00 00`. `e_sessionSetHost`
is `31 00 00 00 00 00 00 00 00 00 00`. The notes' `0b 00 1a` prefix is
`e_turn` (id 11), not another kind.

## Shared records

`SNetAddress` is `m_version` Int8, `m_address` as 0..8 Int16 words (4-bit
count), and `m_port` Int16. The words are not a decoded IPv4 address.

`SUserInfo` is `m_sessionId`, `m_peerId`, and `m_sessionSecret` (each Int32),
then external and internal `SNetAddress`, then `SUserInitialData`.

`SUserInitialData` is the same record as a here-is user. Field order and
string widths are in [docs/udp-join.md](udp-join.md). Short form: name
String6, optional clan tag String6, optional 40-byte clan logo, optional
league and combined race levels, random seed, optional race 0..254, optional
team 0..255, four bools, test type, observe 0..2 in 2 bits, hero, skin, and
mount String7, toon handle String5, optional signed `m_scaledRating`.

`SLobbyState`, used by join-succeeded and session-join-syncing:

```text
m_phase               enum 0..5, 3 bits
m_maxUsers            Int5 0..16
m_maxObservers        Int5 0..16
m_slots               array5 0..16 of SLobbySlot
m_randomSeed          Int32
m_hostUserId          optional Int4 0..15
m_isSinglePlayer      bool
m_pickedMapTag        Int8 0..255
m_gameDuration        Int32
m_defaultDifficulty   Int6 0..32
m_defaultAIBuild      Int8 0..255
```

`SLobbySlot`, in order. Handicap 101 and control 255 are rejected. Color is
an optional Int5 0..31 (`TColorPreference` adds no bits). Race is an optional
Int8 0..254. Artifacts are a 4-bit count, maximum 10, of String7. Reward
values are Int32. The dump's `#307` marker was a depth cap, not a recursive
type.

```text
m_control                    Int8 0..254
m_userId                     optional Int4 0..15
m_teamId                     Int4 0..15
m_colorPref                  optional Int5 0..31
m_racePref                   optional Int8 0..254
m_difficulty                 Int6 0..32
m_aiBuild                    Int8 0..255
m_handicap                   Int7 0..100
m_observe                    enum 0..2, 2 bits
m_logoIndex                  Int32
m_hero, m_skin, m_mount      String7 0..127
m_artifacts                  array, count 0..10, of String7
m_workingSetSlotId           optional Int8 0..255
m_rewards                    array, count 0..65536, of Int32
m_toonHandle                 String5 0..19
m_licenses                   array, count 0..4096, of Int32
m_tandemLeaderId             optional Int4 0..15
m_commander                  String7 0..127
m_commanderLevel             Int32
m_hasSilencePenalty          bool
m_tandemId                   optional Int4 0..15
m_commanderMasteryLevel      Int32
m_commanderMasteryTalents    array, count 0..6, of Int32
m_trophyId                   Int32
m_rewardOverrides            array, count 0..65536, of CRewardOverride
```

`CRewardOverride` is `m_key` Int32, then `m_rewards` as another array of
Int32 with the same 0..65536 count. An array count is present on all of
these (the type byte's high bit is clear). Elements are packed with no
alignment between them. An optional array is one presence bit, then the
array only when set. A present empty array still writes a count of 0.

An ordinary blob writes its length, zero-pads to a byte, then the bytes.
Game-event data and message data are Blob11, 0..1452 (11-bit length).
Transfer data is Blob11, 0..1448. A one-byte message `0xA5` on a zero header
is 13 bytes:

```text
0e 00 00 00 00 00 00 00 00 00 00 01 a5
```

## Events

Header-only events are the id plus `SHeader` and nothing else: ack system,
ack turn, ack message, ping, ping reply, resend system, resend turn, resend
message, ready, session set host, session user ready, and session user
detach. Each is an acknowledgement, a keepalive, a retransmission request,
or a session flag that carries no extra fields.

### 0 `e_connect`

Identifies this program, version, session, and the port it listens on.
After the header: `m_programId` FourCC, `SVersion`, `m_sessionId` Int32,
`m_sessionSecret` Int32, `m_listenPort` Int16. With no user id that is 291
bits, 37 bytes. A real one is in `tests/fixtures/base_port_capture.h`, and
the codec round-trips it. The live probe does not send one yet.

### 1 `e_connectReply`

Accepts or rejects the connect and returns the peer address. After the
header: `SNetAddress`, then `m_errorId` enum 0..10 in 4 bits.

### 2 `e_ackSystem`

Acknowledges the system channel. Header only.

### 3 `e_ackTurn`

Acknowledges the turn channel. Header only. This is the captured 11-byte
prefix.

### 4 `e_ackMessage`

Acknowledges the message channel. Header only.

### 5 `e_ping`

Keepalive. Header only.

### 6 `e_pingReply`

Reply to a ping. Header only.

### 7 `e_join`

Asks to enter the session. After the header: `SUserInfo`, then `m_ready`.

### 8 `e_joinFailed`

The join was refused. After the header: `m_errorId` enum 0..10 in 4 bits.

### 9 `e_joinSucceeded`

The join was accepted. Carries who joined, the user list, who is ready, and
the lobby. After the header:

```text
m_joineeTag        SUserTag
m_joinerTag        SUserTag
m_syncRecycle      Int12 0..4095
m_users            array5 0..16 of { m_tag, m_info SUserInfo, m_incomingSequence Int16 }
m_readyMask        BitArray5 0..16
m_lobbyState       SLobbyState
m_syncSequence     Int16
m_joinSequence     Int16
```

### 10 `e_joining`

Tells the other peers that someone is joining, with the sequences they
should sync to. After the header: `m_joinerTag`, `SUserInfo`, `m_ready`,
`m_syncSequence` Int16, `m_joinSequence` Int16.

### 11 `e_turn`

One lockstep turn. After the header:

```text
m_syncValue        Int16
m_joiners          optional array5 0..16 of STurnJoiner
m_readies          optional BitArray5 0..16
m_requestRecycle   bool
m_gameEventData    Blob11 0..1452
```

`STurnJoiner` has no header. It is `m_tag`, `m_info` (`SUserInfo`),
`m_syncSequence` Int16, and `m_ready`. The game-event bytes are opaque here.
Map simulation is still a later module.

### 12 `e_transferStart`

Opens a blob transfer. `m_type` is a 0-bit enum whose only value is 0, so
it is not stored. After the header: `m_size` Int32, `m_id` Int32, `m_last`.

### 13 `e_transfer`

One chunk of that transfer. After the header: `m_id` Int32, `m_data` Blob11
0..1448.

### 14 `e_message`

An opaque message blob. After the header: `m_messageData` Blob11 0..1452.
The one-byte golden above locks the blob's byte alignment.

### 15 `e_resendSystem`

Asks for a system-channel retransmission. Header only.

### 16 `e_resendTurn`

Asks for a turn retransmission. Header only.

### 17 `e_resendMessage`

Asks for a message retransmission. Header only. A zero header starts with
`0x11`.

### 18 `e_dropUser`

Removes a user. After the header: `m_userTag`, `m_finalTurnSequence` Int16,
`m_leaveReason` enum 0..15 in 4 bits.

### 19 `e_ready`

Marks ready. Header only.

### 30 `e_sessionSequenceInc`

Advances the session sequence. After the header: `m_catchingUp`.

### 40 `e_sessionSetup`

The initial user list. After the header: `m_users`, an array of 0..16
entries. Each entry is `m_id` Int4 0..15, then `SUserInitialData`.

### 41 `e_sessionJoinFailed`

The session join failed. After the header: `m_errorId` enum 0..10 in 4 bits.

### 42 `e_sessionJoinSyncing`

The session join is copying lobby state. After the header: `SLobbyState`.

### 43 `e_sessionJoinComplete`

The session join finished. After the header: `m_selfUserId` Int4 0..15.
This is this peer's own user id.

### 44 `e_sessionUserJoin`

A user entered the lobby. After the header: `SUserInitialData`,
`m_userHandle` Int32, then optional `m_transfer` Int4 0..15.

### 45 `e_sessionUserLeave`

A user left the lobby. After the header: `m_leaveReason` enum 0..15 in 4
bits.

### 46 `e_sessionUserJoinReject`

A join was rejected. After the header: `m_userHandle` Int32.

### 47 `e_sessionUserLeaveReject`

A leave was rejected. After the header: `m_userHandle` Int32. This is a
separate event from the join reject so the two cannot be confused.

### 48 `e_sessionOver`

The session ended. After the header: `m_reason` enum 0..15 in 4 bits.

### 49 `e_sessionSetHost`

Names a new host. Header only. A zero header starts with `0x31`.

### 50 `e_sessionUserTransfer`

A file transfer inside the session. `m_type` is the same 0-bit enum as
transfer-start and is not stored. After the header: `m_id` Int32, `m_last`,
optional `m_path` String9 0..260 (11-bit length, at most 1040 bytes). A
present path of 512 `x` bytes on a zero header puts `0x50` at byte 14,
`0x00` at byte 15, and `x` at byte 16. A 1041-byte path is rejected.

### 51 `e_sessionUserReady`

The user is ready. Header only.

### 52 `e_sessionUserDetach`

The user detached. Header only.

### 53 `e_sessionSetSyncLoadingTime`

Sets the loading sync time. After the header: `m_syncTime` Int32.

### 54 `e_sessionSetSyncPlayingTime`

Sets the playing sync time. After the header: `m_syncTime` Int32.
