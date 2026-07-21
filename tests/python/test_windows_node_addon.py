from __future__ import annotations

import struct
import tempfile
import unittest
from pathlib import Path

from tools.verify_windows_node_addon import PeFormatError, read_pe_imports, verify_node_delay_load


def pe_fixture(*, direct_node: bool, delayed_node: bool) -> bytes:
    data = bytearray(0x800)
    data[:2] = b"MZ"
    struct.pack_into("<I", data, 0x3C, 0x80)
    data[0x80:0x84] = b"PE\0\0"

    coff = 0x84
    struct.pack_into("<HHIIIHH", data, coff, 0x8664, 1, 0, 0, 0, 0xF0, 0x2022)
    optional = coff + 20
    struct.pack_into("<H", data, optional, 0x20B)
    struct.pack_into("<Q", data, optional + 24, 0x140000000)
    struct.pack_into("<I", data, optional + 60, 0x200)
    struct.pack_into("<I", data, optional + 108, 16)

    directories = optional + 112
    struct.pack_into("<II", data, directories + 8, 0x1000, 60)
    struct.pack_into("<II", data, directories + 13 * 8, 0x1100, 64)

    section = optional + 0xF0
    data[section : section + 8] = b".rdata\0\0"
    struct.pack_into("<IIII", data, section + 8, 0x600, 0x1000, 0x600, 0x200)

    normal_names = ["onnxruntime.dll"]
    if direct_node:
        normal_names.append("node.exe")
    for index, name in enumerate(normal_names):
        descriptor = 0x200 + index * 20
        name_rva = 0x1300 + index * 0x40
        struct.pack_into("<IIIII", data, descriptor, 1, 0, 0, name_rva, 1)
        name_offset = 0x200 + (name_rva - 0x1000)
        data[name_offset : name_offset + len(name) + 1] = name.encode("ascii") + b"\0"

    if delayed_node:
        struct.pack_into("<IIIIIIII", data, 0x300, 1, 0x1380, 0, 0, 0, 0, 0, 0)
        data[0x580:0x589] = b"node.exe\0"

    return bytes(data)


class WindowsNodeAddonTests(unittest.TestCase):
    def write_fixture(self, data: bytes) -> Path:
        temporary = tempfile.NamedTemporaryFile(suffix=".node", delete=False)
        temporary.write(data)
        temporary.close()
        self.addCleanup(Path(temporary.name).unlink, missing_ok=True)
        return Path(temporary.name)

    def test_accepts_node_as_delay_import(self) -> None:
        path = self.write_fixture(pe_fixture(direct_node=False, delayed_node=True))
        imports = verify_node_delay_load(path)
        self.assertEqual(imports.normal, ("onnxruntime.dll",))
        self.assertEqual(imports.delayed, ("node.exe",))

    def test_rejects_direct_node_import(self) -> None:
        path = self.write_fixture(pe_fixture(direct_node=True, delayed_node=False))
        with self.assertRaisesRegex(PeFormatError, "normal import"):
            verify_node_delay_load(path)

    def test_reports_missing_delay_import(self) -> None:
        path = self.write_fixture(pe_fixture(direct_node=False, delayed_node=False))
        imports = read_pe_imports(path)
        self.assertEqual(imports.delayed, ())
        with self.assertRaisesRegex(PeFormatError, "absent"):
            verify_node_delay_load(path)


if __name__ == "__main__":
    unittest.main()
