from __future__ import annotations

import hashlib
import json
from pathlib import Path
import zipfile

from tools.webgpu import build_runtime


ROOT = Path(__file__).resolve().parents[2]


def locked() -> dict[str, object]:
    return json.loads(
        (ROOT / "tools" / "webgpu" / "runtime-lock.json").read_text("utf-8")
    )


def pending_lock() -> dict[str, object]:
    lock = locked()
    qualification = lock["qualification"]
    qualification["status"] = "development-pending-device-validation"
    qualification["providerGatePassed"] = False
    qualification["productionArtifactQualified"] = False
    qualification["qualifiedArtifactSetSha256"] = {
        "linux-x64": None,
        "windows-x64": None,
    }
    qualification["qualificationReportSha256"] = {
        "linux-x64": None,
        "windows-x64": None,
    }
    return lock


def package_members(lock: dict[str, object], package_name: str) -> set[str]:
    return {
        spec["sourcePath"]
        for platform_id in ("linux-x64", "windows-x64")
        for spec in build_runtime.artifact_plan(lock, platform_id)
        if spec["package"] == package_name
    }


def create_fake_packages(
    root: Path,
    lock: dict[str, object],
    *,
    omit: tuple[str, str] | None = None,
) -> dict[str, Path]:
    paths: dict[str, Path] = {}
    for package_name in ("onnxruntime", "webgpu"):
        path = root / f"{package_name}.nupkg"
        with zipfile.ZipFile(path, "w", compression=zipfile.ZIP_DEFLATED) as archive:
            for member in sorted(package_members(lock, package_name)):
                if omit != (package_name, member):
                    archive.writestr(member, f"{package_name}:{member}\n".encode())
        paths[package_name] = path
    return paths


def package_identity(path: Path, name: str) -> dict[str, object]:
    data = path.read_bytes()
    return {
        "id": name,
        "filename": path.name,
        "source": "https://example.invalid/package.nupkg",
        "bytes": len(data),
        "sha512": hashlib.sha512(data).hexdigest(),
    }
