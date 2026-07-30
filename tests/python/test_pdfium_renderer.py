from __future__ import annotations

import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock

from tools.pdfium import prepare_renderer


class PdfiumRendererTests(unittest.TestCase):
    def test_prepare_resolves_the_npx_platform_shim(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            pdfium = root / "pdfium"
            pdfium.mkdir()
            cache = root / "cache"
            resources = [root / "font.otf", root / "OFL.txt"]
            for resource in resources:
                resource.write_bytes(b"fixture")

            arguments = mock.Mock(
                pdfium_dir=pdfium,
                font_cache=cache,
                download=True,
                build=True,
            )
            windows_npx = r"C:\hostedtoolcache\windows\node\npx.CMD"

            with (
                mock.patch.object(
                    prepare_renderer,
                    "verified_resources",
                    return_value=resources,
                ),
                mock.patch.object(prepare_renderer, "patch_source"),
                mock.patch.object(
                    prepare_renderer,
                    "required_tool",
                    return_value=windows_npx,
                ) as resolve_tool,
                mock.patch.object(
                    prepare_renderer.subprocess,
                    "run",
                ) as run,
            ):
                prepare_renderer.prepare(arguments)

            resolve_tool.assert_called_once_with("npx")
            self.assertEqual(run.call_args_list[1].args[0][0], windows_npx)

    def test_patch_source_is_pinned_and_idempotent(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            pdfium = Path(temporary)
            source = pdfium / "src"
            source.mkdir()
            (pdfium / "package.json").write_text(
                json.dumps(
                    {
                        "name": "pdfium-native",
                        "version": prepare_renderer.PDFIUM_NATIVE_VERSION,
                    }
                ),
                "utf-8",
            )
            addon = source / "pdfium_addon.cc"
            addon.write_text(
                '#include "fpdf_signature.h"\n'
                "void initialize() {\n"
                "    FPDF_InitLibrary();\n"
                "}\n",
                "utf-8",
            )

            original_sha256 = prepare_renderer.sha256

            def pinned_sha256(path: Path) -> str:
                if path == addon:
                    return prepare_renderer.PDFIUM_ADDON_SHA256
                return original_sha256(path)

            with mock.patch.object(
                prepare_renderer, "sha256", side_effect=pinned_sha256
            ):
                prepare_renderer.patch_source(pdfium)

            patched = addon.read_text("utf-8")
            self.assertEqual(patched.count('#include "light_ocr_font_config.h"'), 1)
            self.assertEqual(
                patched.count("light_ocr_pdfium::InitializeWithBundledFonts();"), 1
            )
            self.assertTrue(
                (source / prepare_renderer.HEADER_PATH.name).is_file()
            )

            prepare_renderer.patch_source(pdfium)
            self.assertEqual(addon.read_text("utf-8"), patched)

    def test_patch_source_rejects_unreviewed_upstream_bytes(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            pdfium = Path(temporary)
            source = pdfium / "src"
            source.mkdir()
            (pdfium / "package.json").write_text(
                json.dumps(
                    {
                        "name": "pdfium-native",
                        "version": prepare_renderer.PDFIUM_NATIVE_VERSION,
                    }
                ),
                "utf-8",
            )
            (source / "pdfium_addon.cc").write_text("changed upstream\n", "utf-8")

            with self.assertRaisesRegex(
                RuntimeError, "source changed; review the font patch"
            ):
                prepare_renderer.patch_source(pdfium)


if __name__ == "__main__":
    unittest.main()
