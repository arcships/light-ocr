#!/usr/bin/env python3
"""Create an immutable WebGPU FP16 superset of the locked FP32 model bundle."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import tempfile


ROOT = Path(__file__).resolve().parents[2]
LOCK_PATH = ROOT / "models" / "bundles.lock.json"
DEFAULT_BASE = ROOT / "models" / "generated" / "ppocrv6-small-onnx-20260714.2"
DEFAULT_OUTPUT = ROOT / "models" / "generated" / "ppocrv6-small-webgpu-20260719.1"
WEBGPU_BUNDLE_ID = "ppocrv6-small-webgpu-20260719.1"


def sha256_bytes(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def canonical_json(value: object) -> bytes:
    return (
        json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
        + "\n"
    ).encode("utf-8")


def verify_file(path: Path, record: dict[str, object], context: str) -> bytes:
    if path.is_symlink() or not path.is_file():
        raise RuntimeError(f"{context} is missing or is not a regular file: {path}")
    data = path.read_bytes()
    if len(data) != record["bytes"] or sha256_bytes(data) != record["sha256"]:
        raise RuntimeError(f"{context} does not match its locked bytes and SHA-256")
    return data


def inventory(root: Path) -> dict[str, dict[str, object]]:
    result: dict[str, dict[str, object]] = {}
    for path in sorted(item for item in root.rglob("*") if item.is_file()):
        relative = path.relative_to(root).as_posix()
        if relative in {"manifest.json", "SHA256SUMS"}:
            continue
        data = path.read_bytes()
        result[relative] = {"bytes": len(data), "sha256": sha256_bytes(data)}
    return result


def checksum_inventory(root: Path) -> dict[str, str]:
    result: dict[str, str] = {}
    for path in sorted(item for item in root.rglob("*") if item.is_file()):
        relative = path.relative_to(root).as_posix()
        if relative == "SHA256SUMS":
            continue
        result[relative] = sha256_bytes(path.read_bytes())
    return result


def locked_artifact() -> tuple[dict[str, object], dict[str, object]]:
    lock = json.loads(LOCK_PATH.read_text("utf-8"))
    bundle = next(
        record
        for record in lock["bundles"]
        if record["bundleId"] == "ppocrv6-small-onnx-20260714.2"
    )
    return bundle, bundle["providerArtifacts"]["webgpuFp16"]


def package_bundle(base: Path, output: Path) -> None:
    bundle_lock, artifact = locked_artifact()
    derived = ROOT / str(artifact["directory"])
    provenance_bytes = verify_file(
        derived / str(artifact["provenance"]["path"]),
        artifact["provenance"],
        "WebGPU FP16 provenance",
    )
    provenance = json.loads(provenance_bytes)
    if (
        provenance.get("artifactId") != artifact["artifactId"]
        or provenance.get("conversionId") != artifact["conversionId"]
        or provenance.get("runtimeContract")
        != {
            "precision": "fp16",
            "graphOptimizationLevel": "extended",
            "cpuPartition": "allow-required",
            "requiredCpuOperators": ["Concat", "Gather", "Slice"],
        }
    ):
        raise RuntimeError("WebGPU FP16 provenance contract differs from its lock")

    model_bytes = {
        kind: verify_file(
            derived / str(artifact[kind]["path"]),
            artifact[kind],
            f"WebGPU FP16 {kind} model",
        )
        for kind in ("detection", "recognition")
    }
    base_manifest = json.loads((base / "manifest.json").read_text("utf-8"))
    if (
        base_manifest.get("bundleId") != bundle_lock["bundleId"]
        or base_manifest.get("schemaVersion") != "1.0"
    ):
        raise RuntimeError("base bundle identity is not the locked FP32 bundle")
    for kind in ("detection", "recognition"):
        source = provenance["models"][kind]["source"]
        source_path = base_manifest["models"][kind]["modelPath"]
        source_record = base_manifest["files"][source_path]
        if source["path"] != source_path or {
            "bytes": source["bytes"],
            "sha256": source["sha256"],
        } != source_record:
            raise RuntimeError(f"WebGPU FP16 {kind} source model is not the base model")

    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = Path(tempfile.mkdtemp(prefix=f".{output.name}.", dir=output.parent))
    try:
        shutil.rmtree(temporary)
        shutil.copytree(base, temporary)
        (temporary / "SHA256SUMS").unlink()
        webgpu = temporary / "webgpu"
        (webgpu / "det").mkdir(parents=True)
        (webgpu / "rec").mkdir(parents=True)
        (webgpu / "det" / "inference.onnx").write_bytes(model_bytes["detection"])
        (webgpu / "rec" / "inference.onnx").write_bytes(model_bytes["recognition"])
        (webgpu / "provenance.json").write_bytes(provenance_bytes)

        normalized_path = temporary / base_manifest["normalizedConfigPath"]
        normalized = json.loads(normalized_path.read_text("utf-8"))
        normalized["bundleId"] = output.name
        normalized_path.write_bytes(canonical_json(normalized))

        base_manifest["schemaVersion"] = "1.2"
        base_manifest["bundleId"] = output.name
        base_manifest["coreCompatibility"]["minimum"] = "0.3.0"
        provider_models: dict[str, object] = {}
        for kind, short in (("detection", "det"), ("recognition", "rec")):
            source_model = base_manifest["models"][kind]
            source_path = source_model["modelPath"]
            provider_models[kind] = {
                "modelId": f"PP-OCRv6_small_{short}_onnx_webgpu_fp16",
                "modelPath": f"webgpu/{short}/inference.onnx",
                "modelSha256": artifact[kind]["sha256"],
                "sourceModelId": source_model["id"],
                "sourceModelSha256": base_manifest["files"][source_path]["sha256"],
                "tensorType": "float16",
            }
        base_manifest["providers"] = {
            "webgpu": {
                "schemaVersion": "1.0",
                "conversionId": artifact["conversionId"],
                "precision": "fp16",
                "graphOptimizationLevel": "extended",
                "cpuPartition": "allow-required",
                "requiredCpuOperators": ["Concat", "Gather", "Slice"],
                "provenancePath": "webgpu/provenance.json",
                "provenanceSha256": artifact["provenance"]["sha256"],
                **provider_models,
            }
        }
        base_manifest["files"] = inventory(temporary)
        (temporary / "manifest.json").write_bytes(canonical_json(base_manifest))
        checksums = checksum_inventory(temporary)
        (temporary / "SHA256SUMS").write_text(
            "".join(f"{digest}  {path}\n" for path, digest in checksums.items()),
            encoding="ascii",
            newline="\n",
        )
        if output.exists():
            shutil.rmtree(output)
        os.replace(temporary, output)
    except Exception:
        shutil.rmtree(temporary, ignore_errors=True)
        raise


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base", type=Path, default=DEFAULT_BASE)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    arguments = parser.parse_args()
    output = arguments.output.resolve()
    if output.name != WEBGPU_BUNDLE_ID:
        parser.error(f"--output basename must be {WEBGPU_BUNDLE_ID}")
    package_bundle(arguments.base.resolve(), output)
    print(output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
