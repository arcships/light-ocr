#!/usr/bin/env python3
"""Build the immutable PP-OCRv6 bundle from pinned official archives."""

from __future__ import annotations

import argparse
import hashlib
import io
import json
import os
from pathlib import Path
import shutil
import sys
import tarfile
import tempfile
import urllib.error
import urllib.request


ROOT = Path(__file__).resolve().parents[1]
LOCK_PATH = ROOT / "models" / "bundles.lock.json"
DEFAULT_OUTPUT = ROOT / "models" / "generated" / "ppocrv6-small-onnx-20260714.2"
TIERS = ("tiny", "small", "medium")


class OfflineCacheMiss(RuntimeError):
    """A locked model input is absent while network access is disabled."""


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def canonical_json(value: object) -> bytes:
    return (
        json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
        + "\n"
    ).encode("utf-8")


def verify(data: bytes, record: dict[str, object], label: str) -> None:
    if len(data) != int(record["bytes"]):
        raise RuntimeError(f"{label}: byte count mismatch")
    if sha256(data) != record["sha256"]:
        raise RuntimeError(f"{label}: SHA-256 mismatch")


def obtain(
    record: dict[str, object], cache_dir: Path, *, offline: bool = False
) -> bytes:
    cache_dir.mkdir(parents=True, exist_ok=True)
    destination = cache_dir / str(record["filename"])
    if destination.exists():
        data = destination.read_bytes()
        verify(data, record, destination.name)
        return data
    if offline:
        raise OfflineCacheMiss(f"offline model cache is missing {destination.name}")
    request = urllib.request.Request(
        str(record["url"]), headers={"User-Agent": "light-ocr-bootstrap/1"}
    )
    with urllib.request.urlopen(request, timeout=300) as response:
        data = response.read()
    verify(data, record, destination.name)
    temporary = destination.with_suffix(destination.suffix + ".tmp")
    temporary.write_bytes(data)
    os.replace(temporary, destination)
    return data


def read_archive_members(
    archive: bytes, record: dict[str, object], *, tier: str = "small"
) -> dict[str, bytes]:
    expected = record["members"]
    prefix = (
        f"PP-OCRv6_{tier}_"
        f"{'det' if record['name'] == 'detection' else 'rec'}_onnx_infer/"
    )
    result: dict[str, bytes] = {}
    with tarfile.open(fileobj=io.BytesIO(archive), mode="r:") as source:
        for member in source.getmembers():
            if member.issym() or member.islnk() or member.isdev():
                raise RuntimeError(f"unsafe archive member: {member.name}")
            if member.isdir():
                if member.name.rstrip("/") != prefix.rstrip("/"):
                    raise RuntimeError(f"unexpected archive member: {member.name}")
                continue
            if not member.isfile() or not member.name.startswith(prefix):
                raise RuntimeError(f"unexpected archive member: {member.name}")
            leaf = member.name[len(prefix) :]
            if leaf not in expected or "/" in leaf or leaf in result:
                raise RuntimeError(
                    f"unexpected or duplicate archive member: {member.name}"
                )
            extracted = source.extractfile(member)
            if extracted is None:
                raise RuntimeError(f"cannot read archive member: {member.name}")
            data = extracted.read()
            verify(data, expected[leaf], member.name)
            result[leaf] = data
    if set(result) != set(expected):
        raise RuntimeError(f"archive members do not match lock for {record['name']}")
    return result


def obtain_artifact_members(
    artifact: dict[str, object], cache_dir: Path, *, offline: bool = False,
    tier: str = "small",
) -> dict[str, bytes]:
    if "url" not in artifact:
        result: dict[str, bytes] = {}
        for name, member in artifact["members"].items():
            prefix = "" if tier == "small" else f"{tier}-"
            member_record = {
                **member,
                "filename": f"{prefix}{artifact['name']}-{name}",
            }
            result[name] = obtain(member_record, cache_dir, offline=offline)
        return result
    try:
        return read_archive_members(
            obtain(artifact, cache_dir, offline=offline), artifact, tier=tier
        )
    except (urllib.error.URLError, TimeoutError, OfflineCacheMiss) as error:
        members = artifact["members"]
        if not all("url" in record for record in members.values()):
            raise
        print(
            f"{artifact['name']}: official archive unavailable ({error}); "
            "using pinned official model repository members",
            file=sys.stderr,
        )
        result: dict[str, bytes] = {}
        for name, member in members.items():
            member_record = {
                **member,
                "filename": f"{artifact['name']}-{name}",
            }
            result[name] = obtain(member_record, cache_dir, offline=offline)
        return result


def parse_yaml_scalar(raw: str) -> str:
    value = raw.lstrip(" ")
    if len(value) >= 2 and value[0] == "'" and value[-1] == "'":
        return value[1:-1].replace("''", "'")
    if len(value) >= 2 and value[0] == '"' and value[-1] == '"':
        return json.loads(value)
    return value


def extract_dictionary(
    config: bytes, expected_entries: int | None = None
) -> list[str]:
    lines = config.decode("utf-8").replace("\r\n", "\n").replace("\r", "\n").split("\n")
    start = next(
        (index for index, line in enumerate(lines) if line == "  character_dict:"), None
    )
    if start is None:
        raise RuntimeError("recognition YAML has no PostProcess.character_dict")
    characters: list[str] = []
    for line in lines[start + 1 :]:
        if not line.startswith("  - "):
            if line.strip():
                break
            continue
        characters.append(parse_yaml_scalar(line[4:]))
    if expected_entries is not None and len(characters) not in {
        expected_entries,
        expected_entries - 1,
    }:
        raise RuntimeError(
            f"unexpected dictionary entry count: {len(characters)}; "
            f"expected {expected_entries} effective entries"
        )
    if not characters:
        raise RuntimeError("recognition dictionary is empty")
    if not characters or characters[-1] != " ":
        characters.append(" ")
    if expected_entries is not None and len(characters) != expected_entries:
        raise RuntimeError("effective dictionary entry count is invalid")
    if len(characters) < 2 or characters[-2] == " ":
        raise RuntimeError("effective dictionary space rule failed")
    return characters


def normalized_config(
    bundle_id: str,
    dictionary_entries: int,
    *,
    tier: str = "small",
    language_coverage: dict[str, object] | None = None,
) -> dict[str, object]:
    config: dict[str, object] = {
        "schemaVersion": "1.2",
        "bundleId": bundle_id,
        "resourceLimits": {
            "maxWidth": 10_000,
            "maxHeight": 10_000,
            "maxPixels": 40_000_000,
            "maxDetectionSide": 4_000,
            "maxDetectionCandidates": 3_000,
            "maxDetectionTiles": 100,
            "maxRecognitionBatchSize": 8,
            "maxRecognitionWidth": 3_200,
            "maxTemporaryBytes": 512 * 1024 * 1024,
            "maxConcurrentCalls": 1,
        },
        "sourceDetectionResize": {
            "limitSideLen": 64,
            "limitType": "min",
            "maxSideLimit": 4_000,
            "dimensionMultiple": 32,
            "minimumDimension": 32,
            "scaledDimensionRounding": "truncate_toward_zero",
            "multipleRounding": "half_to_even",
            "maxSideLimitOrder": "before_multiple_rounding",
            "interpolation": "linear",
        },
        "runtimeDefaults": {
            "detection": {
                "strategy": "bounded",
                "maxSide": 960,
                "minimumShortSide": 64,
                "dimensionMultipleRounding": "ceil",
            },
            "recognitionBatchSize": 1,
        },
        "runtimeProfiles": {
            "tiled": {
                "contractVersion": "tiled-v1",
                "tileSide": 1_280,
                "minimumOverlap": 128,
                "dimensionMultiple": 32,
                "dimensionMultipleRounding": "ceil_resize",
                "artificialBoundaryMargin": 32,
                "tileOrder": "row_major",
                "merge": {
                    "iouThreshold": 0.5,
                    "intersectionOverSmallerThreshold": 0.8,
                    "scope": "different_overlapping_tiles",
                    "geometry": "select_representative",
                    "selectionOrder": [
                        "not_artificial_boundary",
                        "higher_db_score",
                        "farther_from_artificial_boundary",
                        "lower_tile_ordinal",
                        "lower_candidate_ordinal",
                    ],
                },
                "recognition": "once_after_global_merge",
            }
        },
        "detection": {
            "input": {
                "colorOrder": "BGR",
                "tensorLayout": "NCHW",
                "tensorType": "float32",
            },
            "normalize": {
                "scale": 1 / 255,
                "mean": [0.485, 0.456, 0.406],
                "std": [0.229, 0.224, 0.225],
            },
            "postprocess": {
                "algorithm": "DB",
                "threshold": 0.3,
                "boxThreshold": 0.6,
                "unclipRatio": 1.5,
                "maxCandidates": 3_000,
                "useDilation": False,
                "scoreMode": "fast",
                "boxType": "quad",
                "minimumBoxSide": 3,
            },
        },
        "geometry": {
            "rowBandPixels": 10,
            "perspectiveInterpolation": "cubic",
            "borderMode": "replicate",
            "tallLineRatio": 1.5,
            "tallLineRotation": "counterclockwise90",
        },
        "recognition": {
            "input": {
                "colorOrder": "BGR",
                "tensorLayout": "NCHW",
                "tensorType": "float32",
                "shape": [3, 48, 320],
                "minimumTensorWidth": 320,
                "maximumTensorWidth": 3_200,
                "tensorWidthRounding": "truncate_toward_zero",
                "resizedContentWidthRounding": "ceil",
                "batchTensorWidth": "maximum_sample_tensor_width",
                "interpolation": "linear",
            },
            "normalize": {
                "scale": 1 / 255,
                "mean": [0.5, 0.5, 0.5],
                "std": [0.5, 0.5, 0.5],
                "paddingValue": 0.0,
            },
            "batch": {"maximumSize": 8, "sortByWidth": True},
            "decode": {
                "algorithm": "CTC",
                "blankIndex": 0,
                "collapseRepeats": True,
                "appendSpaceCharacter": True,
                "confidence": "mean_selected_argmax_probability",
                "dictionaryPath": "rec/dictionary.json",
                "dictionaryEntries": dictionary_entries,
            },
            "defaultScoreThreshold": 0.0,
        },
    }
    if tier != "small":
        if language_coverage is None:
            raise RuntimeError("non-small bundles require locked language coverage")
        config["productProfile"] = {
            "tier": tier,
            "languageCount": language_coverage["count"],
            "excludedLanguages": language_coverage["excluded"],
            "dictionaryEntries": dictionary_entries,
            "maturity": "preview",
        }
    return config


def write_bundle(
    output: Path, cache_dir: Path, force: bool, *, offline: bool = False,
    tier: str = "small",
) -> None:
    lock = json.loads(LOCK_PATH.read_text("utf-8"))
    bundle = next(
        (record for record in lock["bundles"] if record.get("tier") == tier), None
    )
    if bundle is None and tier == "small" and len(lock.get("bundles", [])) == 1:
        bundle = lock["bundles"][0]
    if bundle is None:
        raise RuntimeError(f"bundle lock does not contain tier: {tier}")
    if output.exists():
        if not force:
            raise RuntimeError(
                f"output already exists: {output}; use --force to replace it"
            )
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = Path(tempfile.mkdtemp(prefix=output.name + ".", dir=output.parent))
    try:
        payloads: dict[str, bytes] = {}
        for artifact in bundle["artifacts"]:
            members = obtain_artifact_members(
                artifact, cache_dir, offline=offline, tier=tier
            )
            target = "det" if artifact["name"] == "detection" else "rec"
            payloads[f"{target}/inference.onnx"] = members["inference.onnx"]
            payloads[f"{target}/inference.yml"] = members["inference.yml"]

        characters = extract_dictionary(
            payloads["rec/inference.yml"],
            int(bundle["languageCoverage"]["dictionaryEntries"]),
        )
        payloads["rec/dictionary.json"] = canonical_json(
            {"schemaVersion": "1.0", "characters": characters}
        )
        payloads["normalized-config.json"] = canonical_json(
            normalized_config(
                bundle["bundleId"],
                len(characters),
                tier=tier,
                language_coverage=bundle["languageCoverage"],
            )
        )
        payloads["LICENSES/PaddleOCR-Apache-2.0.txt"] = obtain(
            bundle["license"], cache_dir, offline=offline
        )
        notice = (
            f"PP-OCRv6 {tier} ONNX model bundle\n"
            f"PaddleOCR revision: {bundle['paddleOcrRevision']}\n"
            f"Detection model revision: {bundle['detectionModelRevision']}\n"
            f"Recognition model revision: {bundle['recognitionModelRevision']}\n"
            "Sources: official PaddlePaddle model ecology archives listed in bundles.lock.json\n"
        )
        payloads["LICENSES/MODEL-NOTICE.md"] = notice.encode("utf-8")

        manifest_files = {
            path: {"bytes": len(data), "sha256": sha256(data)}
            for path, data in sorted(payloads.items())
        }
        manifest: dict[str, object] = {
            "schemaVersion": "1.0",
            "bundleId": bundle["bundleId"],
            "family": "PP-OCRv6",
            "coreCompatibility": {
                "minimum": "0.2.0" if tier == "small" else "0.4.0",
                "maximumMajor": 0,
            },
            "upstream": {
                "repository": "https://github.com/PaddlePaddle/PaddleOCR",
                "release": "v3.7.0",
                "revision": bundle["paddleOcrRevision"],
            },
            "capabilities": {
                "detection": True,
                "recognition": True,
                "textlineOrientation": False,
            },
            "models": {
                "detection": {
                    "id": f"PP-OCRv6_{tier}_det_onnx",
                    "sourceRevision": bundle["detectionModelRevision"],
                    "modelPath": "det/inference.onnx",
                    "configPath": "det/inference.yml",
                    "inputRank": 4,
                    "outputRanks": [3, 4],
                },
                "recognition": {
                    "id": f"PP-OCRv6_{tier}_rec_onnx",
                    "sourceRevision": bundle["recognitionModelRevision"],
                    "modelPath": "rec/inference.onnx",
                    "configPath": "rec/inference.yml",
                    "dictionaryPath": "rec/dictionary.json",
                    "inputRank": 4,
                    "outputRank": 3,
                },
            },
            "normalizedConfigPath": "normalized-config.json",
            "files": manifest_files,
            "licenses": ["Apache-2.0"],
        }
        if tier != "small":
            manifest["productProfile"] = {
                "tier": tier,
                "languageCount": bundle["languageCoverage"]["count"],
                "excludedLanguages": bundle["languageCoverage"]["excluded"],
                "dictionaryEntries": len(characters),
                "maturity": "preview",
            }
        manifest_bytes = canonical_json(manifest)
        all_files = {**payloads, "manifest.json": manifest_bytes}
        for relative, data in all_files.items():
            destination = temporary / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(data)
        sums = "".join(
            f"{sha256(data)}  {path}\n" for path, data in sorted(all_files.items())
        )
        (temporary / "SHA256SUMS").write_bytes(sums.encode("utf-8"))
        if output.exists():
            shutil.rmtree(output)
        os.replace(temporary, output)
    except Exception:
        shutil.rmtree(temporary, ignore_errors=True)
        raise


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--tier", choices=TIERS, default="small")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--cache-dir", type=Path, default=ROOT / ".cache" / "models")
    parser.add_argument("--force", action="store_true")
    parser.add_argument(
        "--offline",
        action="store_true",
        help="require every locked model input to already exist in the cache",
    )
    arguments = parser.parse_args()
    output = arguments.output or (
        ROOT
        / "models"
        / "generated"
        / next(
            record["bundleId"]
            for record in json.loads(LOCK_PATH.read_text("utf-8"))["bundles"]
            if record.get("tier") == arguments.tier
        )
    )
    write_bundle(
        output.resolve(),
        arguments.cache_dir.resolve(),
        arguments.force,
        offline=arguments.offline,
        tier=arguments.tier,
    )
    print(output.resolve())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
