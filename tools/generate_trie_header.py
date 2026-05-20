#!/usr/bin/env python3
"""Convert Typst hypher binary .bin trie files to C++ constexpr headers."""

import pathlib
import sys


def write_header(src_path: str, out_path: str, symbol: str) -> None:
    blob = pathlib.Path(src_path).read_bytes()
    if len(blob) < 4:
        raise ValueError(f"Blob too small: {len(blob)} bytes")

    # First 4 bytes are big-endian root address into the full blob.
    root_addr = (blob[0] << 24) | (blob[1] << 16) | (blob[2] << 8) | blob[3]
    # Strip the 4-byte header; adjust root address accordingly.
    data = blob[4:]
    root_addr_adj = root_addr - 4

    lines = []
    for i in range(0, len(data), 16):
        chunk = ", ".join(f"0x{b:02X}" for b in data[i : i + 16])
        lines.append(f"    {chunk},")
    bytes_literal = "\n".join(lines)

    header = (
        "#pragma once\n"
        "#include <cstddef>\n"
        "#include <cstdint>\n"
        '#include "liang_hyphenation_patterns.h"\n'
        "// Auto-generated from Typst hypher binary trie. Do not edit manually.\n"
        f"alignas(4) static constexpr std::uint8_t {symbol}_trie_data[] = {{\n"
        f"{bytes_literal}\n"
        "};\n"
        f"static constexpr HyphenationTrieData {symbol}_trie = {{\n"
        f"    0x{root_addr_adj:04X}u,\n"
        f"    {symbol}_trie_data,\n"
        f"    sizeof({symbol}_trie_data),\n"
        "};\n"
    )

    pathlib.Path(out_path).write_text(header, encoding="utf-8")
    print(f"wrote {out_path}: {len(data)} bytes payload, root=0x{root_addr_adj:04X}")


if __name__ == "__main__":
    if len(sys.argv) != 4:
        print("Usage: generate_trie_header.py <input.bin> <output.h> <symbol>")
        sys.exit(1)
    write_header(sys.argv[1], sys.argv[2], sys.argv[3])
