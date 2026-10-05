# Kitsune

Private C++23 library. A functional reimplementation of the StarCraft II **4.10**
client (Linux **Base75689**, and the Windows executable of that same build),
clear enough to compile on whatever host we are on and to speak to a real 4.10
Linux client.

Ghidra output from `E:\Github\Helix` is evidence. It is not the source that
lands in this tree. Byte-identical machine code is a side project under
[`research/byte-identical`](research/byte-identical/README.md) and is off unless
you pass `-DKITSUNE_BYTE_IDENTICAL=ON`. Nothing in the main library waits on it.

The official clients stay on disk, outside git. This repo builds without them.

## What "done" means for a piece of code

A routine is done when a test shows the same observable result as 4.10:

- a map archive opens and yields the same files,
- a lockstep datagram is accepted by a real 4.10 peer,
- a join reaches the same phase the official client reaches,
- a replay or protocol field decodes to the same values.

The C++ can use `std::vector`, real enums, and RAII. It does not have to keep
the binary's stack frame, register choices, or object size.

## Layout

| Path | Role |
|---|---|
| `include/Kitsune/` | Public headers. What another project (Helix, a tool) includes. |
| `src/` | Library sources. Rewritten C++23. |
| `tests/` | CTest, split by module (`api/`, `net/`, `version/`). Fast tests run with no game installed. |
| `docs/decompile.md` | How to turn a Ghidra session on Linux and Windows into that C++. |
| `docs/architecture.md` | Modules, and which bytes must match 4.10. |
| `research/byte-identical/` | Optional. Off by default. |
| `local/` | Gitignored. Your client paths, Ghidra notes, pcaps, dumps. |

Planned namespaces, created when there is code for them: `archive`, `net`,
`api`, `sim`. Networking and archive reading come before simulation. The order
and the reason are in [docs/architecture.md](docs/architecture.md).

## Build

CMake 3.28 or newer, a C++23 compiler, Ninja. CLion is the intended IDE: open
this folder and enable the CMake preset that matches the toolchain.

Windows, from a Visual Studio x64 developer prompt (so `cl` is on `PATH`):

```bat
cmake --preset windows-msvc-debug
cmake --build --preset windows-msvc-debug
ctest --preset windows-msvc-debug
```

In CLion, pick the Visual Studio toolchain and the preset `windows-msvc-debug`.
The preset sets the compiler to `cl`. CLion's toolchain supplies the
environment.

Linux, or a WSL toolchain pointed at this tree:

```sh
cmake --preset linux-debug
cmake --build --preset linux-debug
ctest --preset linux-debug
```

Release presets are `windows-msvc-release` and `linux-release`.

The Windows presets set `VCPKG_ROOT` to `C:\Users\Shadow\.vcpkg-clion\vcpkg`
and pass that tree's toolchain file. The Linux presets use a bootstrap of
that same baseline at `/home/josh/vcpkg`, with the installed tree at
`/home/josh/kitsune-vcpkg-installed`. Two packages are linked. Protobuf is the
current vcpkg port, not the client's protobuf_blz 2.6.0.19. Message
definitions are Blizzard's s2client-proto at `6c1eedc` (the tree that
documents the 4.10 client) under `third_party/s2client-proto`. Later commits
add 5.0 fields and stay out. Asio owns the UDP sockets. The 4.10 client calls
the OS for those, and Linux domain 10 is not a Windows argument. Civetweb,
zlib, OpenSSL, and StormLib are not linked until a call site needs them.

## Relationship to Helix

Helix (sibling checkout `E:\Github\Helix`) is the bot.
Its notes on the Linux 4.10 image — labels, peer session, ports, the
CreateGame agent cap — are the current evidence notebook:

- `E:\Github\Helix\docs\sc2-linux-410-reverse.md`
- `E:\Github\Helix\scripts\ghidra\`

Kitsune does not copy those patches and does not vendor the ELF. When a
behavior moves here, the C++ and its tests become the source of truth, and the
Helix note can point at the Kitsune type instead of growing another address.

## Private remote

The game clients, Ghidra databases, and decompiler dumps do not go in the
remote. `local/` is gitignored for that reason.

```bat
gh repo create kitsune --private --source=. --remote=origin
```

Run that when you want the remote. Confirm the repo is private before the
first push.

## Reading order

1. [docs/architecture.md](docs/architecture.md) — what we are building.
2. [docs/decompile.md](docs/decompile.md) — how a Ghidra function becomes a reviewable change.
3. [CONTRIBUTING.md](CONTRIBUTING.md) — branches, commits, tests.
4. [AGENTS.md](AGENTS.md) — short rules for an assistant editing this tree.
