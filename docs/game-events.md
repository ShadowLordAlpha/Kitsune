# Game events

`m_gameEventData` in an `e_turn` (`docs/tcp-base.md`) is a stream of `NNet::Game`
events. This is the layer a peer needs to get a game started and to keep it
running, and it is not the replay format: a replay file keeps only the events
that matter after the game began, and leaves out the lobby, loading, and sync
events that a live peer has to send. Everything here comes from the client's
own type table and from captures of two official clients, not from a replay
tool.

## Where the schema is

The 4.10 clients carry one protocol object, filled at startup from a type table
(`docs/udp-join.md`, "UDP join"). The Linux and Windows images hold the same
table byte for byte (76,607 bytes, checked): version 7, 1842 types, big-endian.
Layout:

```text
header         version 7, 0, type count, code size, dump size   (5 x u32)
offsets        one u32 per type: where its code starts, from the start of the table
code           the type definitions
dump offsets   one u32 per type: where its names start
dump           the names: a string pool, and per type a list of string offsets
               (the type's own name, then one per enumerator, field, or choice arm)
```

The dump does carry enumerator names, so `e_connect` and `e_setLobbySlot` are
the client's own spellings. A second table in the image holds C++ member
offsets for each type; those are not wire data and are not used here.

A type is a first byte and its operands. The low six bits of the first byte are
the kind and the top bit means "fixed":

```text
 1 array      length, element type            9 int       bits, min, max
 2 blob       length (byte aligned)          10 null      nothing on the wire
 3 bit array  length                         11 optional  one presence bit, then the type
 4 blob       length (byte aligned)          12 u32       32 bits
 5 bool       one bit                        13 u64       64 bits
 6 choice     tag, then the arm's type       14 struct    fields in order
 7 enum       value, checked against members 15 string    like blob, length bits + 2
 8 u32        32 bits                        16 alias     another type
```

Bounds are `[width code][bits][min][max]`, or just `[width code][value]` when the
type is fixed. The width code gives the size of `min` and `max` (1, 2, 4, or 8
bytes, signed when the code is even). A value is stored as `value - min` in
`bits` bits, with the same bit order as the base port (`docs/udp-join.md`,
"Packing"). A choice, enum, or struct lists its tag values and then one type
id per entry. All 1842 types parse, and each one ends where the next begins
(the last is an alias followed by four zero bytes of padding).

## Framing

An event is a 7-bit `EEventId`, then the event's struct, then padding to the next
byte. The next event starts there. The blob ends when the bytes do. Without the
padding step 307 of 308 captured blobs fail to decode; with it all 308 decode
and consume every byte. `e_turn` (8) is an event like the others: it has an id and an empty struct.

## The setup exchange

Seen between an official host and guest once the base-port join is done
(`docs/tcp-base.md`, "Session order"). Turn numbers are the capture's.

```text
turn 21  guest  e_setLobbySlot   slot 1, race (choice arm 4)
turn 23  host   e_startGame
turn 25  host   e_peerSetSyncLoadingTime  syncTime
         both   e_unpauseGame            pauseTypeIndex 14
         both   e_triggerSoundLengthSync two tables of sound hashes and lengths
         both   e_userOptions            build 75689, flags
         both   e_userFinishedLoading
turn 27  both   e_userFinishedLoading
turn 29  host   e_peerSetSyncPlayingTime  syncTime
         both   commands begin (e_selectionDelta, e_cmd, ...)
```

The host sends `e_startGame` two turns after the guest takes its slot. The
sound-length tables list the map's sounds (110 and 23 hashes in the capture) and
are the same on both sides; every length is 0 when the client has no audio.
`syncTime` is a Unix time in seconds: the loading time in the capture is
1791188297 and the playing time 1791188301, four seconds later, though the two
events are about a second and a half apart in real time. The second
`e_userFinishedLoading` and the second `e_message` (`docs/tcp-base.md`, "Message
channel") go out together on both sides, a second after the package, so they
look like the real end of loading. The package is the only part that depends on the map.

`GameSetup` (`docs/tcp-base.md`, "Game setup") produces this exchange. In
`tests/net/game_setup_test.cpp` its output equals the captured events byte for
byte, in both roles. Live, a Kitsune guest against the official Windows host completes the exchange and
the game runs to a saved replay (`docs/tcp-base.md`, "Live results"). A Kitsune host
has not been run against an official guest. From turn 29 on the events are player commands, and the captured
blobs hold only `e_selectionDelta`, `e_cmd`, `e_cmdUpdateTargetPoint`,
`e_cmdUpdateTargetUnit`, `e_commandManagerState`, and `e_cameraUpdate`.

## The C++

All of the NNet protocol C++ is generated from the table by `tools/schema/gen_cpp.py`, in
three families that share their enums and records:

| Family | Files | Variant | Ids |
|---|---|---|---|
| `NNet::System`, base-port TCP | `SystemEvent.h/.cpp` | `SystemEvent` | 36 (6 bits) |
| `NNet::Local`, game-port UDP | `LocalEvent.h/.cpp` | `LocalEvent` | 5 (4 bits) |
| `NNet::Game`, inside `e_turn` | `GameEvent.h/.cpp` | `game::Event` | 120 (7 bits) |
| shared | `Enums.h`, `Records.h`, `src/Net/Records.cpp`, `src/Net/RecordCodec.h` | | |

Every id has a struct and a codec, and each variant has one alternative per id in id order
(`eventId`, `systemEventId`, and `localEventId` say which). `Records.h` holds the structs
more than one family uses: user info, the lobby, the game description, and the like.
`RecordCodec.h` is internal and declares their field codecs for the three `.cpp` files.
Each System and Local event struct starts with the client's `SHeader` as a member named
`header`; the client has one in each namespace, so they are `SystemHeader` and `LocalHeader`.
The System family also has the stream decoder, `decodeSystemStream`, which says Complete,
Truncated, or Bad (`docs/tcp-base.md`). What is written by hand sits beside them: the base
handshake, turn, message, and ping channels, `describeSystemEvent` (`Describe.h`), and the
local helpers in `Lockstep.h`.

Names are the client's. A type drops its `S`, `T`, or `E` prefix, a member drops `m_`
(`m_slotChange` is `slotChange`), an enumerator drops `e_` (`e_userLeft` is `UserLeft`),
and the full client name sits in a comment above each type. A choice becomes a variant
with one struct per arm, named `<Choice>Arm<Arm>`, holding a `value` unless the arm is
`null`. Nested structs stay nested; none is flattened.

Seven ids do not follow `e_fooBar` to `SFooBarEvent`, and were matched by hand
(`OVERRIDE` in `tools/schema/events.py`): 33 to `SAICommunicateEvent`, 63, 68, and 69 to
the planet-panel structs (the client's id names say `PanelPanel`), 84 to
`STriggerBattleReportPanelSelectionChangedEvent`, 90 to
`STriggerCustomDialogDismissedEvent`, and 113 (`e_muteUserChanged`) to `SMuteChatEvent`.
Id 113 is the one match that rests on the name alone.

Not every small integer is an enum. The table names a type only where the client has
one. A race is `NNet::TRaceId`, an integer type (not an enum), so `racePreference` and
`racePref` are `uint8_t`; `TRaceCount` and `CAllowedRaces` sit beside it. Bit flags
(`m_cmdFlags`, `m_targetUnitFlags`, `m_flags`, `m_versionFlags`) are plain integers with
no bit names in the table. The one exception is `EClientDebugFlags`, which exists as an
integer type (`EClientDebugFlags::Ints`) and whose bit names, if any, are not in the
table. The booleans of `e_userOptions` are separate named members. The table also
holds older copies of these types under `NNet_B<build>_B<build>`; they are not used.

`decodeEvents` stops at the first event that does not decode and says so through
`complete`. There is no way to skip an event whose id is known but whose body fails,
because the next event starts where the body ends.

## Regenerating

`tools/schema` (Python, no dependencies; see its README) reads the client image, so the
image is never in the repository:

```text
KITSUNE_SC2_IMAGE=<SC2_x64 or SC2_x64.exe> python tools/schema/gen_cpp.py
clang-format -i include/Kitsune/Net/{Enums,Records,SystemEvent,LocalEvent,GameEvent}.h \
    src/Net/{RecordCodec.h,Records,SystemEvent,LocalEvent,GameEvent}.cpp
KITSUNE_SC2_IMAGE=... [KITSUNE_CAPTURE=<pcap>] python tools/schema/make_fixtures.py
```

A re-run must give no diff. If it does, the table or the tool changed. The steps behind
the tool, in the order they were worked out:

1. Find the table. `_INIT_1847` calls the loader with the bytecode at `0x319b140` (Linux
   image), `ProtocolMeta.cpp` asserts its header (version 7, 1842 types), and the second
   blob it is given holds member offsets. The table is found by header in both images and
   is identical in them.
2. Read the type opcodes from the decoder (`FUN_020cb780`) and its operand readers. The
   kinds and the bounds format above came from there. Two details were easy to miss:
   choices, enums, and structs always use the full `[bits][min][max]` form for their first
   operand, and the fixed flag only changes how their entries are listed.
3. Check the reader before using it: all 1842 types parse and each ends at the next one's
   start. Then decode every captured blob with it; 308 of 308 decode and use every byte.
4. Read names from the dump: one record per type, a list of string offsets.
5. Generate the C++ and a reference encoder from the same reader, and check them against
   each other and against the sources below.

## Checking the codecs

Four checks:

- Real traffic. Ten blobs from two official clients in one game
  (`tests/fixtures/game_event_capture.h`) decode completely and encode back to the same
  bytes. The full capture decodes 308 of 308.
- Vectors from a separate encoder. `tools/schema/refcodec.py` writes events from the table
  with boundary and random values: three per id, so all 120 ids, in
  `tests/fixtures/game_event_vectors.h`. `tests/net/game_event_test.cpp` decodes each and
  encodes it back. The same encoder makes 108 System and 15 Local datagrams
  (`net_event_vectors.h`); `tests/net/net_event_vectors_test.cpp` runs the generated TCP and
  UDP codecs over them, and every one round-trips. These codecs were hand-written first;
  the tests did not change when they were replaced by generated ones, which is the point
  of keeping them separate from the generator.
- Blizzard's s2protocol, only for the events replays also contain. Its decoder, given the
  same bytes, returns the same values for 102 of the 120 ids (306 vectors, no difference).
  It is a replay tool, so it says nothing about the other 18 ids, which are the lobby,
  loading, and sync events a live peer sends. s2protocol is not used by the code.
- Names. The System, Local, and record types are generated, so their member names are the
  client's by construction. (Before they were generated, four hand-written names differed
  and were fixed: the header's sender, the race and team preferences, the lobby slot's
  color and race, and the here-is's game and users.)

A few names differ from the client's on purpose. The header member is `header`, not the
client's `SHeader`. `SVersion`, `SNetAddress`, and the other `S`-prefixed types lose the
prefix like every other (`Version`, `NetAddress`). A nested array element, which the table
names `__subtypeof_m_users`, is the parent plus the member (`JoinSucceededEventUsers`). The
0-bit `m_type` of `e_transferStart` and `e_sessionUserTransfer` is now stored as an enum
with one value. Fourteen enums have the client's names (`ErrorId`, `LeaveReason`,
`Observe`, `Phase`, `GameSpeed`, `GameType`, and the game options), and the codecs refuse a
value that is not a member.

What the checks cannot cover: an event no capture contains is only as right as the reader
and encoder, which were both written from the same decompile of the client's decoder.
Their agreement with s2protocol and with real traffic on the events that overlap is the
evidence that the reading is right.

## Event ids

`NNet::Game::EEventId` is 7 bits, 0 to 119. Gaps are ids with no enumerator.

```text
  0 e_setLobbySlot
  1 e_dropUser
  2 e_startGame
  3 e_dropOurselves
  4 e_userFinishedLoading
  5 e_userFinishedLoadingSync
  6 e_setGameDuration
  7 e_userOptions
  8 e_turn
  9 e_bankFile
 10 e_bankSection
 11 e_bankKey
 12 e_bankValue
 13 e_bankSignature
 14 e_cameraSave
 15 e_pauseGame
 16 e_unpauseGame
 17 e_singleStepGame
 18 e_setGameSpeed
 19 e_addGameSpeed
 20 e_replayJump
 21 e_saveGame
 22 e_saveGameDone
 23 e_loadGameDone
 24 e_sessionCheat
 25 e_commandManagerReset
 26 e_gameCheat
 27 e_cmd
 28 e_selectionDelta
 29 e_controlGroupUpdate
 30 e_selectionSyncCheck
 31 e_resourceTrade
 32 e_triggerChatMessage
 33 e_aiCommunicate
 34 e_setAbsoluteGameSpeed
 35 e_addAbsoluteGameSpeed
 36 e_triggerPing
 37 e_broadcastCheat
 38 e_alliance
 39 e_unitClick
 40 e_unitHighlight
 41 e_triggerReplySelected
 42 e_hijackReplaySession
 43 e_hijackReplayGame
 44 e_triggerSkipped
 45 e_triggerSoundLengthQuery
 46 e_triggerSoundOffset
 47 e_triggerTransmissionOffset
 48 e_triggerTransmissionComplete
 49 e_cameraUpdate
 50 e_triggerAbortMission
 51 e_triggerPurchaseMade
 52 e_triggerPurchaseExit
 53 e_triggerPlanetMissionLaunched
 54 e_triggerPlanetPanelCanceled
 55 e_triggerDialogControl
 56 e_triggerSoundLengthSync
 57 e_triggerConversationSkipped
 58 e_triggerMouseClicked
 59 e_triggerMouseMoved
 60 e_achievementAwarded
 61 e_triggerHotkeyPressed
 62 e_triggerTargetModeUpdate
 63 e_triggerPlanetPanelPanelReplay
 64 e_triggerSoundtrackDone
 65 e_triggerPlanetMissionSelected
 66 e_triggerKeyPressed
 67 e_triggerMovieFunction
 68 e_triggerPlanetPanelPanelBirthComplete
 69 e_triggerPlanetPanelPanelDeathComplete
 70 e_resourceRequest
 71 e_resourceRequestFulfill
 72 e_resourceRequestCancel
 73 e_triggerResearchPanelExit
 74 e_triggerResearchPanelPurchase
 75 e_triggerResearchPanelSelectionChanged
 76 e_triggerCommandError
 77 e_triggerMercenaryPanelExit
 78 e_triggerMercenaryPanelPurchase
 79 e_triggerMercenaryPanelSelectionChanged
 80 e_triggerVictoryPanelExit
 81 e_triggerBattleReportPanelExit
 82 e_triggerBattleReportPanelPlayMission
 83 e_triggerBattleReportPanelPlayScene
 84 e_triggerBattleReportSelectionChanged
 85 e_triggerVictoryPanelPlayMissionAgain
 86 e_triggerMovieStarted
 87 e_triggerMovieFinished
 88 e_decrementGameTimeRemaining
 89 e_triggerPortraitLoaded
 90 e_triggerQueryDialogDismissed
 91 e_triggerGameMenuItemSelected
 92 e_triggerMouseWheel
 93 e_triggerPurchasePanelSelectedPurchaseItemChanged
 94 e_triggerPurchasePanelSelectedPurchaseCategoryChanged
 95 e_triggerButtonPressed
 96 e_triggerGameCreditsFinished
 97 e_triggerCutsceneBookmarkFired
 98 e_triggerCutsceneEndSceneFired
 99 e_triggerCutsceneConversationLine
100 e_triggerCutsceneConversationLineMissing
101 e_gameUserLeave
102 e_gameUserJoin
103 e_commandManagerState
104 e_cmdUpdateTargetPoint
105 e_cmdUpdateTargetUnit
106 e_triggerAnimLengthQueryByName
107 e_triggerAnimLengthQueryByProps
108 e_triggerAnimOffset
109 e_catalogModify
110 e_heroTalentTreeSelected
111 e_triggerProfilerLoggingFinished
112 e_heroTalentTreeSelectionPanelToggled
113 e_muteUserChanged
114 e_pickMapTag
115 e_convertToReplaySession
116 e_setSyncLoadingTime
117 e_setSyncPlayingTime
118 e_peerSetSyncLoadingTime
119 e_peerSetSyncPlayingTime
```

## Field layouts

One entry per event, from the client's table (`python tools/schema/catalog.py layouts`).
`Int7 0..75` is an integer stored in 7 bits with minimum 0 and maximum 75. A `choice`
lists `tag name: type` arms. `blob` and `string` are both byte aligned after their
length, and a string's length bits and maximum already include the +2 and x4 of
`docs/udp-join.md`, "Packing". Names are the client's own `m_` fields.

```text
0 e_setLobbySlot
    m_slotId: Int4 0..15;
    m_slotChange: choice tag Int5 {0 m_control: Int8 0..254; 1 m_userId: optional Int4 0..15; 2 m_teamId: Int4 0..15; 3 m_colorPref: {m_color: optional Int5 0..31}; 4 m_racePref: {m_race: optional Int8 0..254}; 5 m_difficulty: Int6 0..32; 6 m_aiBuild: Int8 0..255; 7 m_handicap: Int7 0..100; 8 m_observe: enum EObserve (2 bits, 0..2); 9 m_logoIndex: Int32 0..4294967295; 10 m_hero: string(len Int9 0..508); 11 m_skin: string(len Int9 0..508); 12 m_mount: string(len Int9 0..508); 13 m_licenses: array(count Int13 0..4096) of Int32 0..4294967295; 14 m_tandemLeaderId: optional Int4 0..15; 15 m_commander: string(len Int9 0..508); 16 m_commanderLevel: Int32 0..4294967295; 17 m_hasSilencePenalty: bool; 18 m_tandemId: optional Int4 0..15; 19 m_commanderMasteryLevel: Int32 0..4294967295}
1 e_dropUser
    m_dropSessionUserId: Int4 0..15;
    m_reason: enum ELeaveReason (4 bits, 0..15)
2 e_startGame
    (empty)
3 e_dropOurselves
    (empty)
4 e_userFinishedLoading
    (empty)
5 e_userFinishedLoadingSync
    (empty)
6 e_setGameDuration
    m_gameDuration: Int32 0..4294967295
7 e_userOptions
    m_gameFullyDownloaded: bool;
    m_developmentCheatsEnabled: bool;
    m_testCheatsEnabled: bool;
    m_multiplayerCheatsEnabled: bool;
    m_syncChecksummingEnabled: bool;
    m_isMapToMapTransition: bool;
    m_debugPauseEnabled: bool;
    m_useGalaxyAsserts: bool;
    m_platformMac: bool;
    m_cameraFollow: bool;
    m_baseBuildNum: Int32 0..4294967295;
    m_buildNum: Int32 0..4294967295;
    m_versionFlags: Int32 0..4294967295;
    m_hotkeyProfile: string(len Int9 0..316)
8 e_turn
    (empty)
9 e_bankFile
    m_name: blob(len Int7 0..75)
10 e_bankSection
    m_name: blob(len Int6 0..59)
11 e_bankKey
    m_name: blob(len Int6 0..59);
    m_type: Int32 0..4294967295;
    m_data: string(len Int7 0..76)
12 e_bankValue
    m_type: Int32 0..4294967295;
    m_name: blob(len Int6 0..59);
    m_data: string(len Int12 0..3124)
13 e_bankSignature
    m_signature: array(count Int5 0..20) of Int8 0..255;
    m_toonHandle: string(len Int7 0..76)
14 e_cameraSave
    m_which: Int3 0..7;
    m_target: {x: Int16 0..65535; y: Int16 0..65535}
15 e_pauseGame
    m_pauseTypeIndex: Int8 0..255
16 e_unpauseGame
    m_pauseTypeIndex: Int8 0..255
17 e_singleStepGame
    (empty)
18 e_setGameSpeed
    m_speed: enum Game::EGameSpeed (3 bits, 0..4)
19 e_addGameSpeed
    m_delta: Int8 -128..127
20 e_replayJump
    m_replayJumpGameLoop: optional Int32 0..4294967295
21 e_saveGame
    m_fileName: string(len Int11 0..1040);
    m_automatic: bool;
    m_overwrite: bool;
    m_name: string(len Int8 0..156);
    m_description: string(len Int10 0..1020)
22 e_saveGameDone
    (empty)
23 e_loadGameDone
    (empty)
24 e_sessionCheat
    m_data: {m_point: {x: Int32 -2147483648..2147483647; y: Int32 -2147483648..2147483647}; m_time: Int32 -2147483648..2147483647; m_verb: string(len Int10 0..1020); m_arguments: string(len Int10 0..1020)}
25 e_commandManagerReset
    m_sequence: Int32 0..4294967295
26 e_gameCheat
    m_data: {m_point: {x: Int32 -2147483648..2147483647; y: Int32 -2147483648..2147483647}; m_time: Int32 -2147483648..2147483647; m_verb: string(len Int10 0..1020); m_arguments: string(len Int10 0..1020)}
27 e_cmd
    m_cmdFlags: Int26 0..67108863;
    m_abil: optional {m_abilLink: Int16 0..65535; m_abilCmdIndex: Int5 0..31; m_abilCmdData: optional Int8 0..255};
    m_data: choice tag Int2 {0 None: null; 1 TargetPoint: {x: Int20 0..1048575; y: Int20 0..1048575; z: Int32 -2147483648..2147483647}; 2 TargetUnit: {m_targetUnitFlags: Int16 0..65535; m_timer: Int8 0..255; m_tag: Int32 0..4294967295; m_snapshotUnitLink: Int16 0..65535; m_snapshotControlPlayerId: optional Int4 0..15; m_snapshotUpkeepPlayerId: optional Int4 0..15; m_snapshotPoint: {x: Int20 0..1048575; y: Int20 0..1048575; z: Int32 -2147483648..2147483647}}; 3 Data: Int32 0..4294967295};
    m_sequence: Int32 1..4294967295;
    m_otherUnit: optional Int32 0..4294967295;
    m_unitGroup: optional Int32 0..4294967295
28 e_selectionDelta
    m_controlGroupId: Int4 0..10;
    m_delta: {m_subgroupIndex: Int9 0..499; m_removeMask: choice tag Int2 {0 None: null; 1 Mask: bitarray(len Int9 0..500); 2 OneIndices: array(count Int9 0..500) of Int9 0..499; 3 ZeroIndices: array(count Int9 0..500) of Int9 0..499}; m_addSubgroups: array(count Int9 0..500) of {m_unitLink: Int16 0..65535; m_subgroupPriority: Int8 0..255; m_intraSubgroupPriority: Int8 0..255; m_count: Int9 0..500}; m_addUnitTags: array(count Int9 0..500) of Int32 0..4294967295}
29 e_controlGroupUpdate
    m_controlGroupIndex: Int4 0..9;
    m_controlGroupUpdate: enum Game::EControlGroupUpdate (3 bits, 0..5);
    m_mask: choice tag Int2 {0 None: null; 1 Mask: bitarray(len Int9 0..500); 2 OneIndices: array(count Int9 0..500) of Int9 0..499; 3 ZeroIndices: array(count Int9 0..500) of Int9 0..499}
30 e_selectionSyncCheck
    m_controlGroupId: Int4 0..10;
    m_selectionSyncData: {m_count: Int9 0..500; m_subgroupCount: Int9 0..500; m_activeSubgroupIndex: Int9 0..499; m_unitTagsChecksum: Int32 0..4294967295; m_subgroupIndicesChecksum: Int32 0..4294967295; m_subgroupsChecksum: Int32 0..4294967295}
31 e_resourceTrade
    m_recipientId: Int4 0..15;
    m_resources: array(count Int3 0..4) of Int32 -2147483648..2147483647
32 e_triggerChatMessage
    m_chatMessage: string(len Int10 0..1020)
33 e_aiCommunicate
    m_beacon: Int8 -128..127;
    m_ally: Int8 -128..127;
    m_flags: Int8 -128..127;
    m_build: Int8 -128..127;
    m_targetUnitTag: Int32 0..4294967295;
    m_targetUnitSnapshotUnitLink: Int16 0..65535;
    m_targetUnitSnapshotUpkeepPlayerId: Int8 -128..127;
    m_targetUnitSnapshotControlPlayerId: Int8 -128..127;
    m_targetPoint: {x: Int32 -2147483648..2147483647; y: Int32 -2147483648..2147483647; z: Int32 -2147483648..2147483647}
34 e_setAbsoluteGameSpeed
    m_speed: enum Game::EGameSpeed (3 bits, 0..4)
35 e_addAbsoluteGameSpeed
    m_delta: Int8 -128..127
36 e_triggerPing
    m_point: {x: Int32 -2147483648..2147483647; y: Int32 -2147483648..2147483647};
    m_unit: Int32 0..4294967295;
    m_unitLink: Int16 0..65535;
    m_unitControlPlayerId: optional Int4 0..15;
    m_unitUpkeepPlayerId: optional Int4 0..15;
    m_unitPosition: {x: Int20 0..1048575; y: Int20 0..1048575; z: Int32 -2147483648..2147483647};
    m_unitIsUnderConstruction: bool;
    m_pingedMinimap: bool;
    m_option: Int32 -2147483648..2147483647
37 e_broadcastCheat
    m_verb: string(len Int10 0..1020);
    m_arguments: string(len Int10 0..1020)
38 e_alliance
    m_alliance: Int32 0..4294967295;
    m_control: Int32 0..4294967295
39 e_unitClick
    m_unitTag: Int32 0..4294967295
40 e_unitHighlight
    m_unitTag: Int32 0..4294967295;
    m_flags: Int8 0..255
41 e_triggerReplySelected
    m_conversationId: Int32 -2147483648..2147483647;
    m_replyId: Int32 -2147483648..2147483647
42 e_hijackReplaySession
    m_userInfos: array(count Int5 0..16) of {m_sessionUserId: Int4 0..15; m_addNewGameUser: bool; m_gameUserId: Int4 0..15};
    m_method: enum Game::EHijackMethod (1 bits, 0..1)
43 e_hijackReplayGame
    m_userInfos: array(count Int5 0..16) of {m_gameUserId: Int4 0..15; m_observe: enum EObserve (2 bits, 0..2); m_name: string(len Int8 0..128); m_toonHandle: optional string(len Int7 0..76); m_clanTag: optional string(len Int8 0..128); m_clanLogo: optional blob(40 bytes)};
    m_method: enum Game::EHijackMethod (1 bits, 0..1)
44 e_triggerSkipped
    (empty)
45 e_triggerSoundLengthQuery
    m_soundHash: Int32 0..4294967295;
    m_length: Int32 0..4294967295
46 e_triggerSoundOffset
    m_sound: Int32 0..4294967295
47 e_triggerTransmissionOffset
    m_transmissionId: Int32 -2147483648..2147483647;
    m_thread: Int32 0..4294967295
48 e_triggerTransmissionComplete
    m_transmissionId: Int32 -2147483648..2147483647
49 e_cameraUpdate
    m_target: optional {x: Int16 0..65535; y: Int16 0..65535};
    m_distance: optional Int16 0..65535;
    m_pitch: optional Int16 0..65535;
    m_yaw: optional Int16 0..65535;
    m_reason: optional Int8 -128..127;
    m_follow: bool
50 e_triggerAbortMission
    (empty)
51 e_triggerPurchaseMade
    m_purchaseItemId: Int32 -2147483648..2147483647
52 e_triggerPurchaseExit
    (empty)
53 e_triggerPlanetMissionLaunched
    m_difficultyLevel: Int32 -2147483648..2147483647
54 e_triggerPlanetPanelCanceled
    (empty)
55 e_triggerDialogControl
    m_controlId: Int32 -2147483648..2147483647;
    m_eventType: Int32 -2147483648..2147483647;
    m_eventData: choice tag Int3 {0 None: null; 1 Checked: bool; 2 ValueChanged: Int32 0..4294967295; 3 SelectionChanged: Int32 -2147483648..2147483647; 4 TextChanged: string(len Int11 0..1024); 5 MouseButton: Int32 0..4294967295}
56 e_triggerSoundLengthSync
    m_syncInfo: {m_soundHash: array(count Int7 0..110) of Int32 0..4294967295; m_length: array(count Int7 0..110) of Int32 0..4294967295}
57 e_triggerConversationSkipped
    m_skipType: enum Game::EConversationSkip (1 bits, 0..1)
58 e_triggerMouseClicked
    m_button: Int32 0..4294967295;
    m_down: bool;
    m_posUI: {x: Int11 0..1599; y: Int11 0..1199};
    m_posWorld: {x: Int20 0..1048575; y: Int20 0..1048575; z: Int32 -2147483648..2147483647};
    m_flags: Int8 -128..127
59 e_triggerMouseMoved
    m_posUI: {x: Int11 0..1599; y: Int11 0..1199};
    m_posWorld: {x: Int20 0..1048575; y: Int20 0..1048575; z: Int32 -2147483648..2147483647};
    m_flags: Int8 -128..127
60 e_achievementAwarded
    m_achievementLink: Int16 0..65535
61 e_triggerHotkeyPressed
    m_hotkey: Int32 0..4294967295;
    m_down: bool
62 e_triggerTargetModeUpdate
    m_abilLink: Int16 0..65535;
    m_abilCmdIndex: Int5 0..31;
    m_state: Int8 -128..127
63 e_triggerPlanetPanelPanelReplay
    (empty)
64 e_triggerSoundtrackDone
    m_soundtrack: Int32 0..4294967295
65 e_triggerPlanetMissionSelected
    m_planetId: Int32 -2147483648..2147483647
66 e_triggerKeyPressed
    m_key: Int8 -128..127;
    m_flags: Int8 -128..127
67 e_triggerMovieFunction
    m_functionName: blob(len Int7 0..64)
68 e_triggerPlanetPanelPanelBirthComplete
    (empty)
69 e_triggerPlanetPanelPanelDeathComplete
    (empty)
70 e_resourceRequest
    m_resources: array(count Int3 0..4) of Int32 -2147483648..2147483647
71 e_resourceRequestFulfill
    m_fulfillRequestId: Int32 -2147483648..2147483647
72 e_resourceRequestCancel
    m_cancelRequestId: Int32 -2147483648..2147483647
73 e_triggerResearchPanelExit
    (empty)
74 e_triggerResearchPanelPurchase
    (empty)
75 e_triggerResearchPanelSelectionChanged
    m_researchItemId: Int32 -2147483648..2147483647
76 e_triggerCommandError
    m_error: Int32 -2147483648..2147483647;
    m_abil: optional {m_abilLink: Int16 0..65535; m_abilCmdIndex: Int5 0..31; m_abilCmdData: optional Int8 0..255}
77 e_triggerMercenaryPanelExit
    (empty)
78 e_triggerMercenaryPanelPurchase
    (empty)
79 e_triggerMercenaryPanelSelectionChanged
    m_mercenaryId: Int32 -2147483648..2147483647
80 e_triggerVictoryPanelExit
    (empty)
81 e_triggerBattleReportPanelExit
    (empty)
82 e_triggerBattleReportPanelPlayMission
    m_battleReportId: Int32 -2147483648..2147483647;
    m_difficultyLevel: Int32 -2147483648..2147483647
83 e_triggerBattleReportPanelPlayScene
    m_battleReportId: Int32 -2147483648..2147483647
84 e_triggerBattleReportSelectionChanged
    m_battleReportId: Int32 -2147483648..2147483647
85 e_triggerVictoryPanelPlayMissionAgain
    m_difficultyLevel: Int32 -2147483648..2147483647
86 e_triggerMovieStarted
    (empty)
87 e_triggerMovieFinished
    (empty)
88 e_decrementGameTimeRemaining
    m_decrementSeconds: Int32 -2147483648..2147483647
89 e_triggerPortraitLoaded
    m_portraitId: Int32 -2147483648..2147483647
90 e_triggerQueryDialogDismissed
    m_result: Int32 -2147483648..2147483647
91 e_triggerGameMenuItemSelected
    m_gameMenuItemIndex: Int32 -2147483648..2147483647
92 e_triggerMouseWheel
    m_wheelSpin: Int16 -32768..32767;
    m_flags: Int8 -128..127
93 e_triggerPurchasePanelSelectedPurchaseItemChanged
    m_purchaseItemId: Int32 -2147483648..2147483647
94 e_triggerPurchasePanelSelectedPurchaseCategoryChanged
    m_purchaseCategoryId: Int32 -2147483648..2147483647
95 e_triggerButtonPressed
    m_button: Int16 0..65535
96 e_triggerGameCreditsFinished
    (empty)
97 e_triggerCutsceneBookmarkFired
    m_cutsceneId: Int32 -2147483648..2147483647;
    m_bookmarkName: blob(len Int7 0..79)
98 e_triggerCutsceneEndSceneFired
    m_cutsceneId: Int32 -2147483648..2147483647
99 e_triggerCutsceneConversationLine
    m_cutsceneId: Int32 -2147483648..2147483647;
    m_conversationLine: blob(len Int7 0..79);
    m_altConversationLine: blob(len Int7 0..79)
100 e_triggerCutsceneConversationLineMissing
    m_cutsceneId: Int32 -2147483648..2147483647;
    m_conversationLine: blob(len Int7 0..79)
101 e_gameUserLeave
    m_leaveReason: enum ELeaveReason (4 bits, 0..15)
102 e_gameUserJoin
    m_observe: enum EObserve (2 bits, 0..2);
    m_name: string(len Int8 0..128);
    m_toonHandle: optional string(len Int7 0..76);
    m_clanTag: optional string(len Int8 0..128);
    m_clanLogo: optional blob(40 bytes);
    m_hijack: bool;
    m_hijackCloneGameUserId: optional Int4 0..15
103 e_commandManagerState
    m_state: enum Game::ECommandManagerState (2 bits, 0..2);
    m_sequence: optional Int32 1..4294967295
104 e_cmdUpdateTargetPoint
    m_target: {x: Int20 0..1048575; y: Int20 0..1048575; z: Int32 -2147483648..2147483647}
105 e_cmdUpdateTargetUnit
    m_target: {m_targetUnitFlags: Int16 0..65535; m_timer: Int8 0..255; m_tag: Int32 0..4294967295; m_snapshotUnitLink: Int16 0..65535; m_snapshotControlPlayerId: optional Int4 0..15; m_snapshotUpkeepPlayerId: optional Int4 0..15; m_snapshotPoint: {x: Int20 0..1048575; y: Int20 0..1048575; z: Int32 -2147483648..2147483647}}
106 e_triggerAnimLengthQueryByName
    m_queryId: Int16 0..65535;
    m_lengthMs: Int32 0..4294967295;
    m_finishGameLoop: Int32 0..4294967295
107 e_triggerAnimLengthQueryByProps
    m_queryId: Int16 0..65535;
    m_lengthMs: Int32 0..4294967295
108 e_triggerAnimOffset
    m_animWaitQueryId: Int16 0..65535
109 e_catalogModify
    m_catalog: Int8 0..255;
    m_entry: Int16 0..65535;
    m_field: blob(len Int8 0..255);
    m_value: blob(len Int8 0..255)
110 e_heroTalentTreeSelected
    m_index: Int32 0..4294967295
111 e_triggerProfilerLoggingFinished
    (empty)
112 e_heroTalentTreeSelectionPanelToggled
    m_shown: bool
113 e_muteUserChanged
    m_targetUserId: Int4 0..15;
    m_muted: bool
114 e_pickMapTag
    m_pickedMapTag: Int8 0..255
115 e_convertToReplaySession
    m_replayJumpGameLoop: optional Int32 -2147483648..2147483647
116 e_setSyncLoadingTime
    m_syncTime: Int32 0..4294967295
117 e_setSyncPlayingTime
    m_syncTime: Int32 0..4294967295
118 e_peerSetSyncLoadingTime
    m_syncTime: Int32 0..4294967295
119 e_peerSetSyncPlayingTime
    m_syncTime: Int32 0..4294967295
```
