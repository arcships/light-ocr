#!/usr/bin/env python3
"""Prove that the opt-in validated-only policy takes the CPU fallback path."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import subprocess

try:
    from .collect_qualification import read_json, report_hash
except ImportError:  # Direct script execution.
    from collect_qualification import read_json, report_hash


ROOT = Path(__file__).resolve().parents[2]
DEFAULT_ACCEPTANCE = ROOT / "tools" / "apple" / "acceptance.json"
HELLO_PIXELS = ROOT / "corpus" / "fixtures" / "generated-hello-123" / "pixels.bin"


def run_json(command: list[str]) -> dict[str, object]:
    process = subprocess.run(
        command, check=False, capture_output=True, text=True, encoding="utf-8"
    )
    lines = [line for line in process.stdout.splitlines() if line.strip()]
    if process.returncode != 0 or not lines:
        detail = process.stderr.strip() or process.stdout.strip()
        raise RuntimeError(f"fallback probe failed ({process.returncode}): {detail}")
    value = json.loads(lines[-1])
    if not isinstance(value, dict) or value.get("ok") is not True:
        raise RuntimeError("fallback probe did not return a successful JSON object")
    return value


def validate_cpu_fallback(
    benchmark: dict[str, object], expected_hashes: tuple[str, str]
) -> None:
    execution = benchmark.get("execution", {})
    if execution.get("requestedProvider") != "apple":
        raise RuntimeError("fallback probe did not request the Apple provider")
    for stage, expected_hash in zip(
        ("detection", "recognition"), expected_hashes, strict=True
    ):
        session = execution.get(stage, {})
        if (
            session.get("requestedProvider") != "apple"
            or session.get("actualProviderChain") != ["CPUExecutionProvider"]
            or session.get("device") != "cpu"
            or session.get("precision") != "fp32"
            or session.get("modelSha256") != expected_hash
            or session.get("sessionFallback") is not True
            or session.get("fallbackReason") != "apple_device_unqualified"
        ):
            raise RuntimeError(f"{stage} did not take the locked CPU fallback path")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--native-benchmark", type=Path, required=True)
    parser.add_argument("--native-validate", type=Path, required=True)
    parser.add_argument("--bundle", type=Path, required=True)
    parser.add_argument("--expected-device-family", required=True)
    parser.add_argument("--acceptance", type=Path, default=DEFAULT_ACCEPTANCE)
    parser.add_argument("--report", type=Path, required=True)
    arguments = parser.parse_args()

    acceptance = read_json(arguments.acceptance.resolve())
    models = acceptance["models"]
    bundle = arguments.bundle.resolve()
    manifest = read_json(bundle / "manifest.json")
    provider = manifest.get("providers", {}).get("apple", {})
    validated_families = provider.get("validatedDeviceFamilies", [])
    if (
        provider.get("devicePolicy") != "validated-only"
        or not isinstance(validated_families, list)
        or not validated_families
        or arguments.expected_device_family in validated_families
    ):
        parser.error(
            "fallback bundle must use validated-only and exclude the expected family"
        )
    if (
        provider.get("qualificationId") != acceptance["qualificationId"]
        or provider.get("detection", {}).get("packageSha256")
        != models["detectionPackageSha256"]
        or provider.get("recognition", {}).get("packageSha256")
        != models["recognitionPackageSha256"]
    ):
        parser.error("fallback bundle does not match the locked Apple models")

    common = [
        "--bundle", str(bundle),
        "--pixels", str(HELLO_PIXELS),
        "--width", "800",
        "--height", "180",
        "--stride", "2400",
        "--format", "bgr8",
        "--profile", "apple_cpu_fallback",
    ]
    benchmark = run_json([
        str(arguments.native_benchmark.resolve()), *common,
        "--warmup", "0", "--iterations", "1",
    ])
    expected_cpu_hashes = (
        str(models["detectionCpuSha256"]),
        str(models["recognitionCpuSha256"]),
    )
    validate_cpu_fallback(benchmark, expected_cpu_hashes)
    validation = run_json([
        str(arguments.native_validate.resolve()), *common, "--diagnostics",
    ])
    lines = validation.get("lines", [])
    if len(lines) != 1 or lines[0].get("text") != "HELLO 123":
        raise RuntimeError("CPU fallback changed the locked canary result")

    execution = benchmark["execution"]
    report: dict[str, object] = {
        "schemaVersion": "1.0",
        "qualificationId": acceptance["qualificationId"],
        "expectedDeviceFamily": arguments.expected_device_family,
        "passed": True,
        "bundleId": manifest["bundleId"],
        "validatedDeviceFamilies": sorted(validated_families),
        "models": {
            "detectionPackageSha256": models["detectionPackageSha256"],
            "recognitionPackageSha256": models["recognitionPackageSha256"],
            "detectionCpuSha256": expected_cpu_hashes[0],
            "recognitionCpuSha256": expected_cpu_hashes[1],
        },
        "execution": execution,
        "canary": {
            "fixtureId": "generated-hello-123",
            "text": "HELLO 123",
            "acceptedLines": benchmark.get("result", {}).get("acceptedLines"),
        },
    }
    report["reportSha256"] = report_hash(report)
    arguments.report.parent.mkdir(parents=True, exist_ok=True)
    arguments.report.write_text(
        json.dumps(report, ensure_ascii=False, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(json.dumps({
        "passed": True,
        "deviceFamily": arguments.expected_device_family,
        "report": str(arguments.report),
        "reportSha256": report["reportSha256"],
    }, ensure_ascii=False, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
