# Architecture

Kitsune is a library that behaves like the StarCraft II 4.10 client where we
have replaced it, and that can sit next to a real 4.10 client where we have
not.

Linux Base75689 (`SC2_x64`, non-PIE ELF, load address `0x400000`, GCC, stripped)
is the primary binary. The Windows 4.10 executable of that same build is the
second view of the same source. Where the two disagree, the behavior you can
observe by running a client wins. A newer Windows build (5.0 and later) is a
textbook for code 4.10 does not have, in particular a newer MPQ parser. It is
not the compatibility target. A retail build such as Base97563 is obfuscated
in ways 4.10 Linux is not; do not assume the Windows 4.10 image matches that
later protection, and do not assume it is clean either. Check.

## What has to match

| Surface | Match |
|---|---|
| UDP lockstep payloads, ports, join sequence | Bytes and timing enough that a real 4.10 Linux client completes a game with us. |
| SC2API websocket (`s2clientprotocol` 4.10.0.75689.0) | The same requests and responses a bot already sends. |
| MPQ as 4.10 reads a map | Same files and bytes out of an archive the official client accepts. |
| Replay fields we claim to parse | Same values the official client wrote. |
| In-memory C++ objects | Whatever layout is obvious in C++23. `std::vector` is the right model of a growable array even when the binary uses three pointers and a stride of `0x9b10`. |

The stride and the offsets stay in the evidence note (Helix, or `local/`).
They justify the fields. They do not become `char pad[0x9b10]` in `src/`.

## Modules

[`include/Kitsune/Kitsune.h`](../include/Kitsune/Kitsune.h) includes the
types that exist. Add a directory when the next real type arrives.

### `kitsune::net`

Peers, port sets, the join path. This is the first module, because Helix
already has a named slice of it on Linux (peer tick, two embedded sockets,
`SendTo`, port words) and because interoperability is "a real client accepts
us." The UDP half of that join is [docs/udp-join.md](udp-join.md). The base-port
TCP events are [docs/tcp-base.md](tcp-base.md), and the game events they carry
are [docs/game-events.md](game-events.md). One
process running Kitsune, one process running Base75689, on a local map, is
the demonstration.

### `kitsune::api`

The websocket API the headless client exposes. Bots, including Helix, already
speak this. Kitsune implements the host side where we need a client we can
recompile, and the guest side where we need to sit in a lobby a real client
created.

### `kitsune::archive`

MPQ reading and writing as 4.10 does for maps. Implement from the 4.10 Linux
image and confirm against the 4.10 Windows image.

When a format or a parser shows up only in a later client, open that Windows
build in its own Ghidra project and lift the behavior. The case we already
know we want is the MPQ parser that arrived with 5.0, which 4.10 does not
have. Treat that as extra capability behind an explicit type, with tests from
archives produced by that client. Shipping it must not change how a 4.10 map
is read.

### `kitsune::sim`

Simulation: units, orders, combat, the frame loop. Last. A wrong sim that
links is worse than no sim. Each chunk needs a recorded trace from a real
4.10 game (orders in, state or checksum out) checked in as a small fixture
we generated. Full simulation is out of scope until `net` can finish a join.

## Processes

```text
Helix bot  --websocket-->  Kitsune api/net  --udp-->  official SC2_x64 (4.10 Linux)
                                |
                                +-- MPQ map --> archive
```

Any of those boxes can still be the official client. Kitsune replaces one box
at a time. Early on, the official Linux client remains the opponent and the
oracle.

## Build products

`Kitsune` is a static library. Tests are separate executables registered with
CTest. There is no client executable in the first cut: a `kitsune_client` tool
appears when `net` can bind a port, not before.

The byte-identical tree is an `INTERFACE` library excluded from the default
configure. It must not be linked into `Kitsune`.
