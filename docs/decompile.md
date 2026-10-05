# From Ghidra to C++23

How to turn the StarCraft II 4.10 binaries into code that belongs in this
repository. The audience is someone who already has both clients on disk and
a Ghidra project open. The Helix notebook
(`E:\Github\Helix\docs\sc2-linux-410-reverse.md`,
`E:\Github\Helix\scripts\ghidra\`) is the address-level record for Linux
Base75689. This file is the method for writing the library.

Ghidra on this machine has been `C:\Users\Shadow\Downloads\ghidra_12.1.3_PUBLIC_20260817`.
Any current 12.x build is fine. The Linux project already imported there is
`C:\Users\Shadow\ghidra-projects\Helix-SC2-410`. Do not re-analyze it to start
a Kitsune task. Open it.

## 1. The bar

A function is ready to commit when three things are true.

1. You can say, in a sentence, what it does to data another program can see.
   "Broadcasts the join datagram to each guest game port in the port set" is
   a sentence. "Matches the decompiler" is not.
2. The C++23 uses real types. Containers, enums, spans, RAII. A reader who
   has never opened Ghidra can review it.
3. A test locks the sentence. A fixture for pure code. A noted interop run
   when the sentence is "a real 4.10 client accepts this."

The decompiler is a disassembly with guesses about types. Both compilers emit
code that decompiles into lies. The section on those lies is below. The fix
is a second binary and a run of the client, not a more aggressive analysis.

Byte-identical output is out of scope. If you need it for one routine, it
goes under `research/byte-identical` and stays out of the library.

## 2. The two images, and the optional third

Write the paths into `local/binaries.md` (gitignored). Record file size and
the version string you can actually prove.

| Image | What it is | How you use it |
|---|---|---|
| Linux 4.10 `SC2_x64` | ELF64, not PIE, load address `0x400000`, GCC, symbols stripped. Base **75689**. File offset of a VA is `VA - 0x400000`. | Primary control flow. Strings, PLT imports, and `.eh_frame` are intact. |
| Windows 4.10 `SC2_x64.exe` | PE32+, MSVC, preferred image base usually `0x140000000`. Same **build** as the Linux file. | Second view of the same C++. RTTI, `.pdata`, and (if present) mangled names fill gaps the stripped ELF left. |
| Windows 5.0 or newer `SC2_x64.exe` | A later base number. Retail builds after 4.10 are often obfuscated. The one we care about first is the client that introduced the newer MPQ parser. | Reference for behavior 4.10 does not contain. Own Ghidra project. Never the oracle for 4.10 interop. |

Confirm the Windows file is 4.10 before you explain a difference as "the
compiler." Compare the base directory name, the version resource, and a
string you already trust from Linux (a log line, a protobuf name). A hit in
Base97563 is a different program. Helix already checked that later retail
image: the 1v1 error string is present and unreferenced, and the Linux byte
signature is absent. That note does not describe the 4.10 Windows exe. Open
the 4.10 one and see.

File offsets differ. On the ELF, `VA - 0x400000` works because the image is
not relocated and the load bias is `0x400000`. On the PE, use
**Window → Memory Map**: section RVA plus raw pointer. ASLR rebases the
Windows process at run time. Ghidra still shows the preferred base. Static
notes use that base. A debugger note must say "module base + RVA".

## 3. One Ghidra project per image

Three programs, three projects. Analysis of a 64 MB image is half an hour to
an hour and a half. Do it once.

Linux (already done if the Helix project is on disk):

1. Language `x86:LE:64:default`, compiler **gcc**.
2. Auto Analyze with **Decompiler Parameter ID** on.
3. **Window → Defined Strings**, minimum length about 8.
4. Run `E:\Github\Helix\scripts\ghidra\LabelSc2Linux410.py` if the labels are
   missing. Headless scripts need `# @runtime Jython`.

Windows 4.10:

1. Language `x86:LE:64:default`, compiler **windows** (Visual Studio).
2. Auto Analyze. Turn **Aggressive Instruction Finder** off if `.rdata` fills
   with junk instructions.
3. Leave **Windows x86 PE Exception Handling** and **Decompiler Parameter ID**
   on. `.pdata` is the function list the ELF makes you recover by hand.
4. Defined Strings, same minimum.

Windows 5.0 (only when an archive question needs it):

1. Same PE settings, separate project, separate program name that includes
   the base number.
2. If the decompiler shows computed jumps and the string you want has no
   references, stop treating that function as source. Use the image for
   strings, RTTI names, and imports. Recover control flow from Linux when
   the function exists there, or from a dynamic trace when it does not.

Bookmark the clusters you return to. For Linux those are the SC2API range
and the net code already labeled in Helix. Do not export the whole program
to C. A full export is unbuildable and will be mistaken for a starting point.

## 4. What each compiler leaves behind

Same C++, two ABIs. A reconstruction that only fits one listing is unfinished.

| Leftover | Linux GCC (Base75689) | Windows MSVC (same 4.10) |
|---|---|---|
| Function bounds | `.eh_frame`. Ghidra often misses functions whose prologue is not the one it expects. Create Function at the first real instruction after the previous `ret`. | `.pdata` / `RUNTIME_FUNCTION`. Trust these extents over a prologue scan. |
| `this` and arguments | System V: `rdi`, `rsi`, `rdx`, `rcx`, `r8`, `r9`. `this` is `rdi`. | Microsoft x64: `rcx`, `rdx`, `r8`, `r9`. `this` is `rcx`. Stack args start at `[rsp+0x28]`. |
| Return | `rax`, or `xmm0` for a float. A struct larger than 16 bytes is returned through a hidden pointer (GCC: `rdi` and `this` shifts right; MSVC: hidden pointer in `rcx` and `this` shifts right). | Same idea, different register. A "first argument" that is never stored is often the return slot. |
| vtable | Pointer at offset 0. Typeinfo is an Itanium name (`_ZTV`, `_ZTI`) if it survived the strip. Many survive in `.rodata` even with an empty symbol table. | Pointer at offset 0. The slot before the vtable is a complete-object locator. That locator points at a type descriptor whose name is `.?AVClassName@@`. This is the best class name you will get. |
| Strings as pointers | Non-PIE, image under 4 GB, so GCC loads a `.rodata` address with `mov edi, imm32`. **Search → For Scalars** of the string VA. Show References is often empty. | RIP-relative `lea`. Show References usually works when the string is live. An empty xref list on a *later obfuscated* retail build is expected; on 4.10, check scalars and `.pdata` callers before you assume obfuscation. |
| Imports | PLT. `bind`, `sendto`, `recvfrom` are direct evidence of what a function does to the network. Helix already used this on `PeerConnect` (it calls `bind`, domain IPv6, type datagram). | IAT. The same libc-level calls show up as `ws2_32` imports (`bind`, `sendto`, `recvfrom`). Hash-obfuscated imports belong to later retail notes, not to a reason to ignore a normal IAT on 4.10. |
| C++ runtime | libstdc++ `std::string` / `std::vector` layouts (three words, SSO buffer inline). A "vector" in the decompiler is pointer, pointer, pointer. | MSVC `std::string` SSO (16-byte buffer, size, capacity) and `std::vector` (begin, end, cap). Different field order. The container is still a vector. |
| Failure / security | No stack cookie in the System V leaf functions you will read first. | `/GS` cookie checked in the epilogue. The cookie is not a field of your type. Delete it from the mental model. |
| Exceptions | `.gcc_except_table`, landing pads that look like extra control flow. | `CxxFrameHandler`. Filter the pads out before you invent an error enum. |
| Register waste | `r12`–`r15` saved and used as temps. A store to the stack that is reloaded once is a local, not a member. | `rsi`, `rdi`, `rbx` saved. Same rule. |

When Linux says a field is at `+0xd8` and Windows says a field is at `+0xd8`,
the field is real and the offset happens to match. When the offsets differ,
the field can still be real and the allocators just packed it differently.
Record both offsets in `local/`. Give the field a name from its use. In
`src/`, place it where a C++ reader expects it.

## 5. Walk one behavior

Do this every time. Skip a step only when you can point at the note that
already did it.

### 5.1 Name the observable

Write one sentence before opening the decompiler. Examples that match work
already done on Linux:

- CreateGame rejects more than two participant agents, and says so.
- Join applies a port set of four `uint16`s.
- `SendTo` walks a half-open port range and sends one datagram per port.

If you cannot state the sentence, you are not ready to write C++. Stay in
Ghidra or run the client.

### 5.2 Find it on Linux

The reliable order on Base75689:

1. Defined Strings for a log line or a protobuf name you saw at runtime.
2. If Show References is empty, **Search → For Scalars** with the string VA,
   or Memory Search for the immediate in little endian.
3. Require the hit to be in executable code.
4. Create Function if needed. Decompile (F5). Rename locals as you learn them
   (`nAgents`, `gamePort`). Renames live in the Ghidra project, which is
   fine and local.
5. Confirm with a PLT call or a compare you can relate to the sentence.

Helix's CreateGame note is the worked example of this loop. The comparable C
is a handful of branches on a count. That C is the *behavior*. The instruction
bytes are the patcher's problem, and Kitsune is not the patcher.

### 5.3 Find the same behavior on Windows 4.10

Search for structure, not for the Linux bytes. MSVC will not emit
`83 f8 02` followed by the GCC branch layout just because the C matched.

In order:

1. The same log string, then xrefs. If the string is absent, the log was
   compiled out. Try the protobuf type name (`SC2APIProtocol`, `CreateGame`)
   and the MSVC RTTI string `.?AV...`.
2. A unique constant from the Linux side that is not an address: error code
   `7`, record stride `64` (`shl reg, 6`), a port, a vtable-sized object.
3. **Search → For Instruction Patterns** only after the constant is in hand.
   Patterns alone have a high false-positive rate.
4. Callers. `.pdata` plus xrefs to the RTTI/vtable should put you in the
   same function the Linux scalar search found.
5. Decompile. You are looking for the same sentence, written with `rcx` as
   `this`.

Stop when both listings support the sentence. A third afternoon of renaming
locals does not raise confidence as much as one disagreeing constant does.

### 5.4 If Windows 4.10 is obfuscated in this function

Some retail builds rewrite control flow and hide pointers. If you hit that
on the 4.10 Windows file:

- Keep the Linux listing as the control flow.
- Keep Windows for names (RTTI), imports, and any clean data section.
- If you need a runtime confirmation, break on the observable effect
  (the `sendto`, the string read, the error field write), not on a guessed
  instruction pattern from Linux.
- Do not spend the week deobfuscating the whole `.text`. Find the island
  (websocket, civetweb, `sc2api`, the MPQ open path) and leave the rest.

Dynamic work is for confirming a sentence. It is not a step that produces
source.

### 5.5 Write the type, then the function

Order matters.

1. **Type.** Name it from RTTI when you have it (`PeerSession`, not
   `struct_a1`). Members are what both binaries actually load and store.
   Drop stack slots, cookies, and saved registers.
2. **Ownership.** If the binary refcounts, say so with a type, not with
   `int ref` copied from the decompiler and a manual `inc`. If the object
   is owned by a parent and never freed alone, a reference or a
   `std::unique_ptr` at the parent is enough.
3. **Function.** Straight-line C++ that a reviewer can read. Early returns
   are fine. A state machine becomes an `enum class`, not a `goto` ladder
   copied from the blocks.
4. **Boundary.** Encode and decode functions own the bytes. The rest of the
   program sees structs. A port set on the wire may be four `uint16`s in a
   fixed order. The in-memory `PortSet` can be four named fields in any
   order that reads well, and the encode function is what the test locks.

Worked sketch, from the Linux port globals Helix already named. The binary
keeps four words and the virgin `SendTo` walks `[cx, cx+1)`. The library
version is the behavior of that walk, not the registers:

```cpp
struct PortPair {
    std::uint16_t game = 0; // low 16 of the packed dword
    std::uint16_t base = 0; // high 16
};

// Host of the recorded 1v1: local = server (si/di), remote = client_ports[0].
// SendTo aims at the remote game port. That word is g_cx, not g_dx.
// g_dx is the remote base port. Memory order at PortGlobals is si, di, dx, cx,
// so the struct above is not that layout.
```

The comments cite the evidence names once. They do not reproduce the
function at `0x1b6ab70`. The library types are `kitsune::net::PortPair` and
`guestBroadcastRange`. When Windows 4.10 shows the same four words in the
same order inside the datagram, promote that order into the encode function
and add a fixture. Until then the names stay tentative and the comment says so.

### 5.6 Translate the decompiler's dialect

| Decompiler text | C++23 |
|---|---|
| `undefined4`, `undefined8`, `ulong`, `long` | The real type, once known. Until then a named alias next to the function (`using AgentCount = std::int32_t`), not `auto` everywhere. |
| `param_1`, `param_2` | A name. If you still have `param_2` in the commit, the function is not done. |
| pointer plus index times constant | `std::span<T>` or `std::vector<T>`. The constant is `sizeof` only when you are describing a byte buffer. |
| three pointers walking together | `std::vector`. GNU and MSVC both lower vectors this way. |
| inline SSO buffer plus size | `std::string` or `std::string_view`. Do not reimplement SSO. |
| `if ((type \| 2) == 3)` | `type == Participant \|\| type == Observer`, once the enum is known. GCC emitted the bitwise form. Write the predicate. |
| `switch` on a byte with magic numbers | `enum class` with the values from the protocol or from both binaries. |
| refcount `inc` / `dec` around every call | Express ownership once at the type. |
| `operator.new` and a matching delete on every path | `std::unique_ptr` or an object in a vector. |
| a loop that returns on the first success | A function that returns `std::optional` or `bool`, with the "first success" in the name or in a one-line comment. |
| stack array of size `0xD28` | A local container, or a scratch buffer with a named cap. Do not copy the stack size. That number is the frame, including every spilled local. |

Keep a Ghidra rename when it is already good (`PeerTick`, `SendTo`). Replace
it when it is a hedge (`Session_maybeStartGame`). The hedge is a sign you do
not have the sentence yet.

## 6. Decompiler lies, both compilers

Check these before you believe a branch.

- **Merged compares.** `x < 2 || x > 5` becomes a subtract and an unsigned
  compare. Write the range.
- **Boolean as arithmetic.** A `cmp` / `setcc` / `and` chain is an `if`.
- **Sign-extended loads.** A `uint16` port loaded into `rax` via `movzx` is
  not a 64-bit field.
- **Hidden return pointer.** See the ABI table. Do not add a fake out-parameter
  to the public API if callers just receive a struct.
- **Tail merge.** Two different failures share an epilogue. They are still two
  failures. Keep both reasons in the C++ if the client distinguishes them
  (different error enum, different log line).
- **Speculative stack fields.** The decompiler will turn `*(this + 0x40)` into
  a member. If the other binary never touches `+0x40` in this function, it may
  be a different object's field or a bug in the stack view. Cross-check.
- **Constructor as a function that returns `this`.** GCC often returns `rdi`.
  The C++ constructor returns nothing.
- **Destructor as a function that clears the vtable.** That store is the
  inlined destructor prologue. It is not a method you need to call `setVtable` for.
- **Virtual call as `(*(code **)(*obj + 0x70))(obj)`.** Name the method from
  the vtable slot. Recover the slot's target from both binaries. One override
  per concrete type, not a function pointer in the public header.
- **Exception pads and cookies.** Drop them.
- **`int` that is a pointer-sized quantity on one listing.** Re-read the load.
  Ghidra's default integer size is wrong often enough that a second binary is
  the cheaper check.

If Linux and Windows decompilations tell two different stories, the bug is
in one of the stories. Run the client, or read the listing, before you average
them.

## 7. Tests

### Pure

Put a fixture in `tests/fixtures/` and assert exact outputs.

Good fixtures are small and were produced by you:

- a hand-built or officially written MPQ containing one file, plus the
  expected payload bytes,
- a datagram captured from a 4.10 join (pcap or a log), plus the decoded
  port set,
- a table of error enum in, error enum out.

Commit the fixture. Do not commit the client. If a fixture is a slice of a
Blizzard map, keep it to the minimum bytes the test needs and do not add
whole maps "for later."

### Against a live 4.10 client

When the sentence is about interoperability, run it. One official Linux
client, one Kitsune process, a local map, no ladder.

Record in the commit or PR body:

- base build of the official side (75689),
- which side hosted,
- the map name,
- the result (joined, failed, and the error string).

Automate that run only after it is reliable. Until then a CTest that needs
`SC2_x64` should be labeled and excluded from the default preset, so a
machine without the game still goes green. The default `ctest` stays pure.

### What not to assert

- `offsetof` a member equals a Ghidra offset. That freezes the binary layout
  inside the library. The byte-identical tree is the place for an experiment
  like that, and it is expected to be deleted.
- Instruction bytes, register names, or "this function is 0x1b6ab70."

## 8. Where the code goes

| You recovered | It lives in |
|---|---|
| A type another module constructs | `include/Kitsune/<Module>/` |
| The function body | `src/<Module>/` |
| Encode/decode of bytes | next to the type, with the fixture in `tests/` |
| A Ghidra script that only labels the official binary | stays in Helix `scripts/ghidra/`, or in `local/` if it is a one-off |
| An address table | Helix doc or `local/`. Not a header. |
| A note that Windows 5.0 parses an archive differently | `docs/` only after you have confirmed it, plus the `archive` code that implements the behavior you actually need |

One type per header once the type is real. A grab bag of free functions
decompiled in address order becomes a file nobody can navigate. Group by the
sentence, not by the VA.

Public headers include what a caller needs. A wire opcode can be public.
A comment that says "see Linux 0x..." belongs on the `.cpp` if it has to
exist at all.

## 9. Newer Windows builds, including the 5.0 MPQ parser

Use a later client when 4.10 cannot answer the question.

Known case: a newer MPQ parser shows up in the 5.0 Windows client and is
absent from 4.10. When you reach archive work that needs it:

1. Import that exact base in its own Ghidra project. Record the base number
   in `local/binaries.md`.
2. Find the open/parse path from strings (`MPQ`, table names, the error text
   the 5.0 client prints) and from MSVC RTTI.
3. Write down how an archive from that client differs from one 4.10 reads
   (header fields, hash/block table, extra tables). Differences you have not
   seen in a hex dump of a real archive are still guesses.
4. Implement the extra behavior as its own type in `kitsune::archive`.
   Tests use an archive produced by that client, trimmed to a fixture.
5. Leave the 4.10 read path on its own tests. A 5.0 improvement must not
   change the bytes Kitsune accepts from a 4.10 map.

If that 5.0 image is obfuscated around the parser, prefer a clean data
comparison (two archives, a hex dump, the tables) over fighting the control
flow. The format is the spec. The decompiler is one way to read it.

Other later-only behavior waits for a sentence of the same kind. "It would
be nice to have the new client" is not a task.

## 10. Session checklist

Copy this into the top of a `local/` note when you start a function. Delete
the note or fold one line into the commit when you are done.

```text
Sentence:
Linux image / VA:
Windows 4.10 image / VA:     (or "log stripped; matched via RTTI <name>")
Both listings agree on:
They disagree on:
Observable check (fixture or live run):
Public type name:
Test:
```

Commit when the last three lines are filled in and `ctest` is green on the
preset you built. The listing itself does not go in the commit.

## 11. Tools, short list

- Ghidra 12.x for both images. Projects live under
  `C:\Users\Shadow\ghidra-projects\`. Not in this repo.
- A debugger you already run: `gdb` on the Linux ELF (Helix has
  `scripts/debug-sc2-peer.gdb` as a prior art for breakpoints on the virgin
  image), `x64dbg` or WinDbg on Windows. Hardware breakpoint on a string or
  on `sendto` when xrefs are gone.
- `llvm-readobj -S` / `readelf -h` for ELF load address. CFF Explorer or
  `dumpbin /HEADERS` for the PE image base and `.pdata`.
- Python for a signature scan or a fixture generator. A generator script can
  live in `local/` or, if it builds a fixture the tests need, in `tests/`
  with no dependency on the client being present at test time.

CLion is for Kitsune. Ghidra is for the binaries. Do not point CLion's
project at the decompiler export directory.
