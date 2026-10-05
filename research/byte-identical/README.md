# Byte-identical research

This directory is a side project. The library does not depend on it, and
`-DKITSUNE_BYTE_IDENTICAL` defaults to `OFF`.

The main tree is finished when behavior matches a 4.10 client: a join
completes, a map opens, a replay field parses, a peer accepts our datagrams.
It is finished without matching instruction selection, stack layout, or
register allocation.

Turn this tree on only for a routine where the bytes themselves are the
question. Typical reasons:

- A datagram or MPQ structure is easier to check by comparing a packed
  encoding against the official client byte for byte.
- A compiler quirk (MSVC security cookie, GCC 32-bit immediate load of a
  string) has to be explained before the behavior is trusted.
- Someone wants a single function's machine code as a curiosity.

Rules if you do work here:

- Keep it out of `src/` and out of the `Kitsune` link line.
- Pin the claim to one build (Linux Base75689 or the matching Windows 4.10
  executable) and one configuration. A later Windows build is a different
  binary.
- Delete the experiment when the functional code no longer needs it. Stale
  offset asserts become lies after the next honest refactor.

No schedule. No expectation that this tree grows.
