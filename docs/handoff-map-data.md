# Handoff: map data with StormLib

Written 2026-10-05 for whoever builds `kitsune::archive` next. Delete this file when the work
is done and the facts in it have moved into `docs/` proper.

## Where things stand

Kitsune joins an official 4.10 Windows host as a guest, runs the lobby and loading exchange,
and the official client then plays a game and writes a valid replay
(`docs/tcp-base.md`, "Live results"). That is the milestone the work is aiming at: play a game
as a peer against the official client and have the official client generate the replay. The
user's order is UDP and TCP (done), then map data (this), then simulation, then websockets.
Kitsune will not hold patches for the official client. A 3-player FFA host is a later,
Kitsune-only feature.

Read `AGENTS.md`, `docs/architecture.md`, and `docs/decompile.md` first. The ones that bite:
Ghidra output is evidence and is not committed; bytes on the wire and in an MPQ match 4.10
and need a test vector; no new dependency without a stated reason in the repo; say which
preset `ctest` ran on.

Practical facts:

- The tree is **not a git repository**. There is no history to lean on; keep changes small and
  say what you touched.
- Presets: `windows-msvc-debug` (run from a shell that has the Visual Studio environment:
  `C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat`) and
  `linux-debug` (WSL Ubuntu, repo at `/mnt/e/Github/Kitsune`). Both pass 20 of 20 today. The
  live test is `DISABLED` by default.
- The protocol code is **generated**: `tools/schema/gen_cpp.py` writes `Enums.h`, `Records.h`,
  `SystemEvent.*`, `LocalEvent.*`, `GameEvent.*`, `src/Net/Records.cpp`, and
  `src/Net/RecordCodec.h`. Edit the tool, never those files. See `docs/game-events.md`,
  "Regenerating".
- In this harness a shell heredoc that contains quotes can fail to parse. Write scripts and
  files with the file tools instead.
- Memory notes for the project are in
  `C:\Users\Shadow\.claude\projects\E--Github-Kitsune\memory\`.

## Why the map matters now

What Kitsune does today with a map is nothing. As a guest it takes whatever the official host
advertises. As a host it replays a **captured** here-is and a captured lobby, so it can only
host 1v1 melee on the map that capture was made on (`hostParamsFrom` in
`tests/live/sc2_udp_join.cpp`; `docs/tcp-base.md`, "Handshake"). To host anything else, or to
advertise a map honestly, these fields have to come from the map file:

| Where | Fields | Source today |
|---|---|---|
| here-is `gameDescription` (`GameDescription` in `Records.h`) | `mapFileName`, `mapAuthorName`, `mapSizeX`, `mapSizeY`, `mapFileSyncChecksum`, `modFileSyncChecksum`, `maxUsers`, `maxObservers`, `maxPlayers`, `maxTeams`, `maxColors`, `maxRaces`, `maxControls`, `slotDescriptions`, `cacheHandles`, `defaultDifficulty`, the `is*` flags | captured |
| `e_joinSucceeded` `lobbyState` | `slots`, `maxUsers`, `maxObservers`, `defaultDifficulty` | captured |
| `e_triggerSoundLengthSync` (two tables of sound hashes, all lengths 0 without audio) | the hash lists | a guest copies the host's; a host sends the guest's or nothing |

Later, the simulation needs the map's terrain, objects, and triggers. That is out of scope
here, but the archive layer should not make it hard.

What is known and what is not:

- Known: the field names and widths (they are in the generated `Records.h`), and one real value
  set for Simple64, in the saved here-is described below.
- Not known: how the client derives `mapFileSyncChecksum`, the 40-byte `cacheHandles`, and the
  per-slot `slotDescriptions` from the archive. **Do not guess.** Recover them by comparing
  against the ground truth below, and by decompiling the 4.10 client where comparison is not
  enough. The user said StormLib does the archive reading; the *meaning* of the contents is
  still evidence work.
- Not known: whether an official guest rejects a here-is for a map it has, with a wrong
  checksum or handle. Test it live rather than assume.

## Ground truth you already have

- `%TEMP%\kitsune-hereis.bin`: a 1323-byte here-is written by the official Windows 4.10 host
  hosting `Melee/Simple64.SC2Map` (the live probe saves it). It decodes with `decodeHereIs`
  (`Lockstep.h`). Its `gameDescription` is the expected output of the map reader for
  `Simple64`. Copy what you need into `tests/fixtures/` as a small generated fixture.
- `local/capture/house-*.pcap`: base-port captures of two official clients (AutomatonLE), with
  `kHostLoadPackage` and `kGuestLoadPackage` in `tests/fixtures/game_event_capture.h` showing
  the sound tables (110 and 23 hashes). `local/` is gitignored evidence.
- `local/capture/kitsune-guest-game.SC2Replay` (8923 bytes): a real MPQ the official client
  wrote. A replay is an MPQ, so it is also the smallest fixture for "open an archive the
  official client wrote". The file names seen in the client's replay temp directory are
  `replay.attributes.events`, `replay.game.events`, `replay.load.info`, `replay.message.events`,
  `replay.resumable.events`, `replay.smartcam.events`, `replay.sync.events`,
  `replay.sync.history`, and `replay.tracker.events`; check what the saved archive actually
  lists before relying on any of them.
- Maps on this machine: `C:\Program Files (x86)\StarCraft II\Maps\` (`Melee\Simple64.SC2Map`,
  58,710 bytes, and `Melee\Flat32.SC2Map` through `Flat128`, `Simple64` through `Simple128`,
  `AutomatonAIE`, `AbyssalReefAIE`, `AcropolisAIE`, and dated season folders). Note this
  `Maps` folder belongs to the whole install, not to Base75689; the 4.10 client opened
  `Simple64` fine in every live run.

## The dependency, and the reason to state

`README.md` already says StormLib is "not linked until a call site needs them", and
`vcpkg.json` lists only `asio` and `protobuf`. The call site is `kitsune::archive`. The stated
reason to write in the commit and in `docs/`:

> Maps and replays are MPQ archives. Kitsune has to read a map to advertise it (`GameDescription`,
> lobby slots, sync checksums) and later to simulate it. StormLib is the established open-source
> MPQ reader. It handles the hash and block tables, sector compression, and encryption, which
> would otherwise be a second, unverified parser to maintain. Kitsune only reads for now.

Things to check before committing to it (I did not verify any of these):

- That vcpkg has a `stormlib` port at baseline `ee6a47dac031930715bea2e6dfb9ad60caaa5d83`
  (`vcpkg search stormlib`), on Windows (`C:\Users\Shadow\.vcpkg-clion\vcpkg`) and on the Linux
  bootstrap the presets use (`/home/josh/vcpkg`). If not, `FetchContent` or a pinned
  `third_party/` checkout are the alternatives, each needing its own reason.
- Its license (believed permissive; confirm and record it).
- That it opens a 4.10 map and an official replay. If a map uses something StormLib cannot
  read, that is the case the `architecture.md` note about the newer 5.0 parser is for; it is
  not a reason to change 4.10 behavior.
- `MPQ bytes ... match 4.10` (`AGENTS.md`): reading with StormLib is fine; if writing archives
  ever starts, the bytes need a test vector from an official archive.

## Suggested shape

Keep it small and in the existing style (ordinary C++23, no vendored client files):

1. `include/Kitsune/Archive/Archive.h`, `src/Archive/Archive.cpp`: an RAII wrapper over the
   StormLib handle. `open(path)`, `contains(name)`, `list()`, `read(name)` returning bytes.
   Errors as `std::optional` or a small status enum, like the rest of the library. Hide the
   StormLib header from the public include so `Kitsune.h` consumers do not need it.
2. `kitsune::archive::MapInfo` (or similar): what Kitsune needs from a map, read through the
   wrapper. Start with the fields in the table above. Name members as the client does.
3. A function that fills `GameDescription` and the lobby slots from a map, replacing the
   captured values in the probe.
4. Tests, each with a vector: open the checked-in replay and list its files; open
   `Simple64.SC2Map` if it is available (skip cleanly when the install is absent, or check in a
   small generated fixture instead of the 58 KB map); compare the derived description with
   the saved here-is field by field.
5. Docs: a `docs/` page for what was learned about the map's files and the checksum rules, a
   line in `docs/architecture.md`, and the dependency reason. Update `README.md`'s sentence
   about StormLib.

## Live check when it works

The probe covers both roles (`tests/live/sc2_udp_join.cpp`, `docs/udp-join.md`):

```text
set KITSUNE_SC2_WINDOWS=C:\Program Files (x86)\StarCraft II
set KITSUNE_LIVE_SECONDS=40
set KITSUNE_REPLAY_OUT=E:\Github\Kitsune\local\capture\out.SC2Replay
build\windows-msvc-debug\tests\kitsune_sc2_udp_join.exe guest
```

`guest` means the official client hosts and Kitsune joins. The host role (no argument)
makes Kitsune the host with an official guest; it has **not** been run live against an
official guest. It is the natural first test of a host that advertises a map from the archive.
The probe creates the official game in `realtime` mode; a stepped game stalls after loading
because nothing calls `step`.

## Not part of this task, but nearby

- A Kitsune host with more than one guest (3-player FFA) needs `e_joining` for earlier peers,
  one turn channel per peer, and a lobby from the map's slots. The map reader is the first
  piece of that. Whether an official joiner accepts a 3-slot lobby is untested.
- The meaning of the five-byte `e_message` blobs (`82 00 00 aa bb`) is unknown.
- Real sync values need the simulation. Today Kitsune sends 0 and no desync has followed.
- Command events are not sent, so the replays contain no player actions.

## Questions for the user

1. Is a vcpkg dependency acceptable, or do they prefer a pinned checkout under `third_party/`?
2. Should the checked-in fixture be a small generated archive (our replay), a trimmed copy of a
   map, or only a test that reads the install when present?
3. First target map: Simple64 only (it has ground truth), or several?
