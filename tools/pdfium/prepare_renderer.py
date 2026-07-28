#!/usr/bin/env python3
"""Prepare the pinned pdfium-native source with bundled fallback fonts."""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import subprocess
import tempfile
import urllib.parse
import urllib.request
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
LOCK_PATH = ROOT / "tools" / "pdfium" / "fonts.lock.json"
HEADER_PATH = ROOT / "tools" / "pdfium" / "light_ocr_font_config.h"
PDFIUM_NATIVE_VERSION = "0.6.1"
PDFIUM_ADDON_SHA256 = "b52e6a4d0b22579a8f650696e0519b89d101412d57258b3e2550333f8da2c5ef"
ALLOWED_FONT_ORIGIN = "https://raw.githubusercontent.com"


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def verified_resources(cache_dir: Path, *, download: bool) -> list[Path]:
    lock = json.loads(LOCK_PATH.read_text("utf-8"))
    if lock.get("schemaVersion") != "1.0":
        raise RuntimeError("unsupported PDF fallback font lock schema")
    records = lock.get("resources")
    if not isinstance(records, list):
        raise RuntimeError("PDF fallback font lock resources must be an array")
    names = [record.get("name") for record in records if isinstance(record, dict)]
    if set(names) != {"NotoSansCJKsc-Regular.otf", "OFL.txt"} or len(names) != 2:
        raise RuntimeError("PDF fallback font lock inventory is incomplete")
    cache_dir.mkdir(parents=True, exist_ok=True)
    resolved: list[Path] = []
    for record in records:
        if Path(record["name"]).name != record["name"]:
            raise RuntimeError("unsafe PDF fallback font resource name")
        destination = cache_dir / record["name"]
        if not destination.is_file() and download:
            parsed = urllib.parse.urlparse(record["url"])
            origin = f"{parsed.scheme}://{parsed.netloc}"
            if origin != ALLOWED_FONT_ORIGIN:
                raise RuntimeError(f"font origin is not allowed: {origin}")
            with tempfile.NamedTemporaryFile(dir=cache_dir, delete=False) as temporary:
                temporary_path = Path(temporary.name)
            try:
                request = urllib.request.Request(
                    record["url"], headers={"User-Agent": "light-ocr-release"}
                )
                with urllib.request.urlopen(request, timeout=60) as response:
                    with temporary_path.open("wb") as output:
                        shutil.copyfileobj(response, output)
                temporary_path.replace(destination)
            finally:
                temporary_path.unlink(missing_ok=True)
        if not destination.is_file():
            raise RuntimeError(f"font resource is missing: {destination}")
        if destination.stat().st_size != record["bytes"]:
            raise RuntimeError(f"font resource size mismatch: {destination}")
        if sha256(destination) != record["sha256"]:
            raise RuntimeError(f"font resource hash mismatch: {destination}")
        resolved.append(destination)
    return resolved


def patch_source(pdfium_dir: Path) -> None:
    package = json.loads((pdfium_dir / "package.json").read_text("utf-8"))
    if package.get("name") != "pdfium-native" or package.get("version") != PDFIUM_NATIVE_VERSION:
        raise RuntimeError("expected the pinned pdfium-native@0.6.1 source package")

    addon = pdfium_dir / "src" / "pdfium_addon.cc"
    current_hash = sha256(addon)
    patched_include = '#include "light_ocr_font_config.h"'
    patched_call = "light_ocr_pdfium::InitializeWithBundledFonts();"
    source = addon.read_text("utf-8")
    if patched_include in source and patched_call in source:
        if not (pdfium_dir / "src" / HEADER_PATH.name).is_file():
            shutil.copy2(HEADER_PATH, pdfium_dir / "src" / HEADER_PATH.name)
        return
    if current_hash != PDFIUM_ADDON_SHA256:
        raise RuntimeError(
            "pdfium-native source changed; review the font patch before updating its pin"
        )
    include_anchor = '#include "fpdf_signature.h"'
    call_anchor = "    FPDF_InitLibrary();"
    if source.count(include_anchor) != 1 or source.count(call_anchor) != 1:
        raise RuntimeError("pdfium-native patch anchors are not unique")
    source = source.replace(
        include_anchor, f'{include_anchor}\n#include "light_ocr_font_config.h"', 1
    )
    source = source.replace(call_anchor, f"    {patched_call}", 1)
    addon.write_text(source, "utf-8")
    shutil.copy2(HEADER_PATH, pdfium_dir / "src" / HEADER_PATH.name)


def prepare(arguments: argparse.Namespace) -> None:
    pdfium_dir = arguments.pdfium_dir.resolve()
    resources = verified_resources(arguments.font_cache.resolve(), download=arguments.download)
    patch_source(pdfium_dir)
    fonts = pdfium_dir / "fonts"
    if fonts.exists():
        shutil.rmtree(fonts)
    fonts.mkdir()
    for resource in resources:
        shutil.copy2(resource, fonts / resource.name)

    if arguments.build:
        subprocess.run(
            ["node", "scripts/download-pdfium.mjs"], cwd=pdfium_dir, check=True
        )
        subprocess.run(
            ["npx", "--yes", "node-gyp@11.4.2", "rebuild"],
            cwd=pdfium_dir,
            check=True,
        )
        subprocess.run(
            ["node", "scripts/bundle-lib.mjs"], cwd=pdfium_dir, check=True
        )


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--pdfium-dir", type=Path, required=True)
    parser.add_argument("--font-cache", type=Path, required=True)
    parser.add_argument("--download", action="store_true")
    parser.add_argument("--build", action="store_true")
    prepare(parser.parse_args())


if __name__ == "__main__":
    main()
