#!/usr/bin/env python3
import argparse
import struct
import sys
from pathlib import Path


UF2_BLOCK_SIZE = 512
UF2_MAGIC0 = 0x0A324655
UF2_MAGIC1 = 0x9E5D5157
UF2_MAGIC_END = 0x0AB16F30
UF2_MAGIC_END_OFFSET = 508
UF2_ADDR_OFFSET = 12
UF2_BLOCKNO_OFFSET = 20


def read_uf2_blocks(path):
    data = path.read_bytes()
    if len(data) % UF2_BLOCK_SIZE:
        raise ValueError(f"{path} size is not a multiple of 512 bytes")

    blocks = []
    for offset in range(0, len(data), UF2_BLOCK_SIZE):
        block = data[offset:offset + UF2_BLOCK_SIZE]
        magic0, magic1 = struct.unpack_from("<II", block, 0)
        (magic_end,) = struct.unpack_from("<I", block, UF2_MAGIC_END_OFFSET)
        if magic0 != UF2_MAGIC0 or magic1 != UF2_MAGIC1 or magic_end != UF2_MAGIC_END:
            raise ValueError(f"{path} has invalid UF2 magic at block offset {offset}")

        (addr,) = struct.unpack_from("<I", block, UF2_ADDR_OFFSET)
        blocks.append((addr, block))

    if not blocks:
        raise ValueError(f"{path} contains no UF2 blocks")
    return blocks


def write_uf2_blocks(path, blocks):
    if not blocks:
        raise ValueError(f"refusing to write empty UF2: {path}")

    blocks = sorted(blocks, key=lambda item: item[0])
    count = len(blocks)
    output = bytearray()
    for index, (_addr, block) in enumerate(blocks):
        block = bytearray(block)
        struct.pack_into("<II", block, UF2_BLOCKNO_OFFSET, index, count)
        output += block
    path.write_bytes(output)


def summarize(name, blocks):
    blocks = sorted(blocks, key=lambda item: item[0])
    first = blocks[0][0]
    last = blocks[-1][0] + 256

    runs = []
    start = prev = blocks[0][0]
    for addr, _block in blocks[1:]:
        if addr == prev + 256:
            prev = addr
        else:
            runs.append((start, prev))
            start = prev = addr
    runs.append((start, prev))

    run_text = ", ".join(f"0x{s:08x}..0x{e + 256:08x}" for s, e in runs)
    print(f"{name}: {len(blocks)} blocks, 0x{first:08x}..0x{last:08x}, runs: {run_text}")


def main(argv):
    parser = argparse.ArgumentParser(description="Split one RP2040 UF2 into code and PIC/resource UF2 files.")
    parser.add_argument("input", type=Path)
    parser.add_argument("--pic-base", type=lambda value: int(value, 0), default=0x10200000)
    parser.add_argument("--code-out", type=Path, required=True)
    parser.add_argument("--pic-out", type=Path, required=True)
    args = parser.parse_args(argv)

    blocks = read_uf2_blocks(args.input)
    code_blocks = [(addr, block) for addr, block in blocks if addr < args.pic_base]
    pic_blocks = [(addr, block) for addr, block in blocks if addr >= args.pic_base]

    write_uf2_blocks(args.code_out, code_blocks)
    write_uf2_blocks(args.pic_out, pic_blocks)

    print(f"UF2 split at 0x{args.pic_base:08x}")
    summarize("code", code_blocks)
    summarize("pic", pic_blocks)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main(sys.argv[1:]))
    except Exception as exc:
        print(f"uf2_split.py: {exc}", file=sys.stderr)
        raise SystemExit(1)
