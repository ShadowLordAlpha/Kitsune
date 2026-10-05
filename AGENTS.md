# Kitsune — notes for an assistant

Read [docs/decompile.md](docs/decompile.md) and [docs/architecture.md](docs/architecture.md)
before editing `src/` or `include/`.

- The library matches observable StarCraft II 4.10 behavior (Linux Base75689
  and the Windows executable of that same build). It is ordinary C++23.
- Ghidra pseudocode is evidence. Do not commit it, and do not paste it into
  `src/` with the names changed.
- Cite a binary in a one-line comment (image, virtual address) only when the
  field or the branch is otherwise unexplained. Keep dumps in `local/`.
- On-wire bytes, MPQ bytes, and replay fields match 4.10 and have a test
  vector. In-memory object layout uses the standard library.
- `research/byte-identical` stays out of the default build. Do not flip
  `KITSUNE_BYTE_IDENTICAL` on to make progress.
- A newer Windows client (5.0 and later) is a reference for code 4.10 lacks,
  such as the newer MPQ parser. It is not a reason to change 4.10 interop.
- Helix (`E:\Github\Helix`) holds the current Linux 4.10 address notebook.
  Do not duplicate that address table into this repo.
- Official clients are not vendored. Do not add patches whose purpose is to
  modify a retail online client.
- Presets: `windows-msvc-debug`, `linux-debug`. Run `ctest` on the preset you
  configured before calling a change done. Say which one you ran.
- Match the style in `.clang-format`. Small comments. No new dependency
  without a stated reason.
