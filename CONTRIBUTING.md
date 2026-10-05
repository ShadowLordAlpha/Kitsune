# Contributing to Kitsune

Private project. The workflow here is for anyone pushing to that remote,
including an assistant in this checkout.

The contract for what code is allowed to look like lives in
[docs/decompile.md](docs/decompile.md). Read it before the first function you
lift. [docs/architecture.md](docs/architecture.md) says which module the
function belongs in.

## Setup

Windows (Visual Studio x64 developer prompt, or CLion with the Visual Studio
toolchain):

```bat
cmake --preset windows-msvc-debug
cmake --build --preset windows-msvc-debug
ctest --preset windows-msvc-debug
```

Linux:

```sh
cmake --preset linux-debug && cmake --build --preset linux-debug && ctest --preset linux-debug
```

CLion: open the folder, enable the matching preset, build the `Kitsune` target,
then run the CTest configuration. The `compile_commands.json` for clangd is
under `build/<preset>/`.

Do not commit `build/`, `cmake-build-*`, `.idea` (except `vcs.xml` if you want
the VCS root pinned), or anything under `local/`.

## What a change contains

One behavior. The patch includes:

1. The C++23 that implements it, in the module from the architecture doc.
2. A test that fails if the behavior regresses. Prefer a fixture checked into
   `tests/fixtures/` (a few datagrams, a tiny MPQ, a decoded field list) over
   a test that needs the official client installed.
3. A comment at the type, only when the evidence would otherwise be invisible:
   build, image (Linux Base75689 or Windows 4.10), and the virtual address.
   One line. The decompiler listing stays in `local/` or in the Helix notes.

A change that pastes Ghidra's C into `src/` and renames `param_1` is not ready.

An interoperability run against a live 4.10 client is recorded in the PR or
commit body when you did one: map, which side was Kitsune, which side was
`SC2_x64`, and the result. The binary is not attached.

## Branches

`master` is the integration branch. Work on `<type>/<short-description>`
(`feat/peer-sendto`, `fix/mpq-hash-table`).

## Commits

[Conventional Commits 1.0](https://www.conventionalcommits.org/en/v1.0.0/).

```text
<type>(<scope>): <summary>

<one or two lines of why>
```

| Type | Use |
|---|---|
| `feat` | New behavior |
| `fix` | A behavior that disagreed with 4.10 |
| `refactor` | Same behavior, clearer C++ |
| `test` | Tests or fixtures only |
| `docs` | Docs only |
| `build` | CMake, presets, compiler flags |
| `chore` | Gitignore and other repo plumbing |

Scopes: `net`, `api`, `archive`, `sim`, `cmake`, `docs`. Omit the scope only
when the change is the whole repo.

Subject line states the behavior (`decode the MPQ hash table`), not the
Ghidra address.

## Review checklist

- Compiles as C++23 on the preset you touched. Say which preset you ran.
- `ctest` for that preset is green.
- No official client, Ghidra project, or decompiler dump in the diff.
- Public headers do not expose a binary offset as part of the API.
- Wire formats and archive bytes have a test vector. In-memory layout does not
  copy the binary's padding.
- `research/byte-identical` was left off unless the change is explicitly about
  that tree.

## Local evidence

Create `local/binaries.md` (gitignored) with absolute paths and sizes:

```text
linux-4.10:  E:\...\StarCraftII\Versions\Base75689\SC2_x64
windows-4.10: E:\...\Versions\Base75689\SC2_x64.exe
windows-5.0:  E:\...\Versions\BaseNNNNN\SC2_x64.exe
ghidra-linux: C:\Users\Shadow\ghidra-projects\...
```

Confirm the Windows 4.10 file is the same base build before you treat a
difference as a compiler difference. A newer base is a different program.
Record the PE base number next to the path. If your Windows 4.10 tree uses
another base number than 75689, write that number down and do not "correct"
it to match Linux until you have checked the file.
