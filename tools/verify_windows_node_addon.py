#!/usr/bin/env python3
"""Verify that a Windows Node addon delay-loads node.exe.

Electron exports Node-API symbols from its renamed executable. A direct
node.exe import works in Node itself but cannot be redirected to Electron's
host image, so release builds must keep node.exe out of the normal PE import
directory and include it in the delay-import directory.
"""

from __future__ import annotations

import argparse
import json
import struct
from dataclasses import dataclass
from pathlib import Path


class PeFormatError(RuntimeError):
    """Raised when the input is not a supported, well-formed PE image."""


@dataclass(frozen=True)
class PeImports:
    normal: tuple[str, ...]
    delayed: tuple[str, ...]


def _unpack_from(fmt: str, data: bytes, offset: int, field: str) -> tuple[int, ...]:
    size = struct.calcsize(fmt)
    if offset < 0 or offset + size > len(data):
        raise PeFormatError(f"{field} exceeds the PE image")
    return struct.unpack_from(fmt, data, offset)


def _read_ascii(data: bytes, offset: int, field: str) -> str:
    if offset < 0 or offset >= len(data):
        raise PeFormatError(f"{field} points outside the PE image")
    end = data.find(b"\0", offset, min(len(data), offset + 4096))
    if end == -1:
        raise PeFormatError(f"{field} is not null terminated")
    try:
        return data[offset:end].decode("ascii")
    except UnicodeDecodeError as error:
        raise PeFormatError(f"{field} is not ASCII") from error


def read_pe_imports(path: Path) -> PeImports:
    data = path.read_bytes()
    if len(data) < 0x40 or data[:2] != b"MZ":
        raise PeFormatError("missing DOS header")

    (pe_offset,) = _unpack_from("<I", data, 0x3C, "PE header offset")
    if data[pe_offset : pe_offset + 4] != b"PE\0\0":
        raise PeFormatError("missing PE signature")

    coff_offset = pe_offset + 4
    _, section_count, _, _, _, optional_size, _ = _unpack_from(
        "<HHIIIHH", data, coff_offset, "COFF header"
    )
    optional_offset = coff_offset + 20
    (magic,) = _unpack_from("<H", data, optional_offset, "optional header magic")
    if magic == 0x20B:
        image_base = _unpack_from(
            "<Q", data, optional_offset + 24, "PE32+ image base"
        )[0]
        directory_count_offset = optional_offset + 108
        directory_offset = optional_offset + 112
    elif magic == 0x10B:
        image_base = _unpack_from(
            "<I", data, optional_offset + 28, "PE32 image base"
        )[0]
        directory_count_offset = optional_offset + 92
        directory_offset = optional_offset + 96
    else:
        raise PeFormatError(f"unsupported optional header magic: 0x{magic:04x}")

    optional_end = optional_offset + optional_size
    if optional_end > len(data):
        raise PeFormatError("optional header exceeds the PE image")
    (directory_count,) = _unpack_from(
        "<I", data, directory_count_offset, "data directory count"
    )
    (size_of_headers,) = _unpack_from(
        "<I", data, optional_offset + 60, "size of headers"
    )

    sections: list[tuple[int, int, int, int]] = []
    section_offset = optional_end
    for index in range(section_count):
        current = section_offset + index * 40
        virtual_size, virtual_address, raw_size, raw_offset = _unpack_from(
            "<IIII", data, current + 8, f"section {index}"
        )
        sections.append((virtual_address, virtual_size, raw_offset, raw_size))

    def rva_to_offset(rva: int, field: str) -> int:
        if rva < size_of_headers:
            if rva >= len(data):
                raise PeFormatError(f"{field} header RVA exceeds the PE image")
            return rva
        for virtual_address, virtual_size, raw_offset, raw_size in sections:
            extent = max(virtual_size, raw_size)
            if virtual_address <= rva < virtual_address + extent:
                offset = raw_offset + (rva - virtual_address)
                if offset >= len(data):
                    break
                return offset
        raise PeFormatError(f"{field} RVA 0x{rva:x} is not mapped by a section")

    def directory(index: int) -> tuple[int, int]:
        if directory_count <= index:
            return 0, 0
        entry_offset = directory_offset + index * 8
        if entry_offset + 8 > optional_end:
            raise PeFormatError(f"data directory {index} exceeds the optional header")
        return _unpack_from("<II", data, entry_offset, f"data directory {index}")

    def normal_imports() -> tuple[str, ...]:
        rva, size = directory(1)
        if rva == 0 or size == 0:
            return ()
        offset = rva_to_offset(rva, "import directory")
        names: list[str] = []
        for position in range(offset, min(len(data), offset + size), 20):
            descriptor = _unpack_from("<IIIII", data, position, "import descriptor")
            if not any(descriptor):
                return tuple(names)
            name_rva = descriptor[3]
            names.append(
                _read_ascii(
                    data,
                    rva_to_offset(name_rva, "import name"),
                    "import name",
                )
            )
        raise PeFormatError("import directory has no terminating descriptor")

    def delayed_imports() -> tuple[str, ...]:
        rva, size = directory(13)
        if rva == 0 or size == 0:
            return ()
        offset = rva_to_offset(rva, "delay-import directory")
        names: list[str] = []
        for position in range(offset, min(len(data), offset + size), 32):
            descriptor = _unpack_from(
                "<IIIIIIII", data, position, "delay-import descriptor"
            )
            if not any(descriptor):
                return tuple(names)
            attributes, name_value = descriptor[:2]
            if attributes & 1:
                name_rva = name_value
            else:
                if name_value < image_base:
                    raise PeFormatError("delay-import name VA precedes the image base")
                name_rva = name_value - image_base
            names.append(
                _read_ascii(
                    data,
                    rva_to_offset(name_rva, "delay-import name"),
                    "delay-import name",
                )
            )
        raise PeFormatError("delay-import directory has no terminating descriptor")

    return PeImports(normal=normal_imports(), delayed=delayed_imports())


def verify_node_delay_load(path: Path) -> PeImports:
    imports = read_pe_imports(path)
    normal = {name.casefold() for name in imports.normal}
    delayed = {name.casefold() for name in imports.delayed}
    if "node.exe" in normal:
        raise PeFormatError("node.exe is a normal import and cannot be redirected to Electron")
    if "node.exe" not in delayed:
        raise PeFormatError("node.exe is absent from the delay-import directory")
    return imports


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("addon", type=Path)
    arguments = parser.parse_args()
    try:
        imports = verify_node_delay_load(arguments.addon)
    except (OSError, PeFormatError) as error:
        parser.error(str(error))
    print(
        json.dumps(
            {
                "addon": str(arguments.addon),
                "normalImports": imports.normal,
                "delayImports": imports.delayed,
            },
            sort_keys=True,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
