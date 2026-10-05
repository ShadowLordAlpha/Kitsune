"""Find the NNet type table in a 4.10 client image and split it into its parts.

The Linux and Windows images carry the same table, so either works. Set KITSUNE_SC2_IMAGE to the path of
`SC2_x64` (Linux) or `SC2_x64.exe` (Windows). The table is found by its five-word header, so no
address is needed; in the Linux image it starts at 0x319b140.

Layout, all big endian:
  header         version 7, 0, type count, code size, dump size
  offsets        one u32 per type: where its code starts, counted from the start of the table
  code           the type definitions
  dump offsets   one u32 per type: where its name record starts, from the start of the table
  dump           name records (lists of string offsets) and the strings they point at
"""
import os
import struct
import sys

HEADER = struct.pack(">5I", 7, 0, 1842, 13724, 48127)
TABLE_SIZE = 76607


def _load():
    path = os.environ.get("KITSUNE_SC2_IMAGE")
    if not path:
        sys.exit("set KITSUNE_SC2_IMAGE to the client image (SC2_x64 or SC2_x64.exe)")
    data = open(path, "rb").read()
    at = data.find(HEADER)
    if at < 0:
        sys.exit("no 4.10 type table header in " + path)
    return data[at:at + TABLE_SIZE]


blob = _load()
version, _, count, code_size, dump_size = struct.unpack(">5I", blob[:20])
offs = list(struct.unpack(">%dI" % count, blob[20:20 + 4 * count]))
code_start = 20 + 4 * count
dump_tab = code_start + code_size
doffs = list(struct.unpack(">%dI" % count, blob[dump_tab:dump_tab + 4 * count]))
