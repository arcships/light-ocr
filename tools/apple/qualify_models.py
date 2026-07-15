#!/usr/bin/env python3
"""Produce placement and tensor-parity evidence for derived Apple models."""

from __future__ import annotations

import argparse
from collections import Counter
from concurrent.futures import ThreadPoolExecutor, as_completed
import hashlib
import json
from pathlib import Path
import platform
import subprocess
import sys
import time

import coremltools as ct
from coremltools.models.compute_plan import MLComputePlan
import numpy as np
import onnxruntime as ort


ROOT = Path(__file__).resolve().parents[2]
DEFAULT_BUNDLE = ROOT / "models" / "generated" / "ppocrv6-small-onnx-20260714.2"
DEFAULT_MODELS = ROOT / "models" / "generated" / "apple-fp16-20260715.1"
DEFAULT_REPORT = ROOT / "reports" / "apple" / "model-qualification.json"
DEFAULT_ACCEPTANCE = ROOT / "tools" / "apple" / "acceptance.json"
QUALIFIED_DETECTOR_CPU_OPERATIONS = {"ios18.relu": 1, "pad": 1}
QUALIFIED_RECOGNIZER_CPU_OPERATIONS = {
    "ios18.cast": 1,
    "ios18.conv": 3,
    "ios18.relu": 3,
    "pad": 3,
}


def cpu_operations_within_declaration(
    observed: dict[str, int], declared: dict[str, int]
) -> bool:
    """Return whether observed CPU fallbacks stay inside the qualified envelope."""
    return all(
        operation in declared and count <= declared[operation]
        for operation, count in observed.items()
    )


def scheduled_operations_fully_accounted_for(
    placement: dict[str, object], target_device: str,
    allow_cpu: bool,
) -> bool:
    """Require every scheduled operation to use the declared device set.

    Core ML reports constants and other compile-time-only operations with no
    device usage. Those remain visible as ``none`` and are not runtime fallback.
    """
    devices = placement["preferredDevices"]
    allowed_devices = {target_device, "none"}
    if allow_cpu:
        allowed_devices.add("MLCPUComputeDevice")
    if any(device not in allowed_devices for device in devices):
        return False
    scheduled = sum(
        int(count) for device, count in devices.items() if device != "none"
    )
    accounted = int(devices.get(target_device, 0))
    if allow_cpu:
        accounted += int(devices.get("MLCPUComputeDevice", 0))
    return scheduled > 0 and accounted == scheduled


def preferred_devices(model: ct.models.MLModel, function_name: str) -> dict[str, object]:
    plan = MLComputePlan.load_from_path(
        model.get_compiled_model_path(), compute_units=model.compute_unit
    )
    program = plan.model_structure.program
    if program is None or function_name not in program.functions:
        raise RuntimeError(f"compute plan does not contain function {function_name}")
    operations = program.functions[function_name].block.operations
    devices: Counter[str] = Counter()
    cpu_operations: Counter[str] = Counter()
    for operation in operations:
        usage = plan.get_compute_device_usage_for_mlprogram_operation(operation)
        if usage is None:
            devices["none"] += 1
            continue
        device = type(usage.preferred_compute_device).__name__
        devices[device] += 1
        if device == "MLCPUComputeDevice":
            cpu_operations[operation.operator_name] += 1
    return {
        "operationCount": len(operations),
        "preferredDevices": dict(sorted(devices.items())),
        "cpuOperations": dict(sorted(cpu_operations.items())),
    }


def tensor_parity(
    onnx_path: Path,
    model: ct.models.MLModel,
    shape: tuple[int, ...],
    seed: int,
) -> dict[str, object]:
    random = np.random.default_rng(seed)
    values = random.normal(0.0, 0.25, size=shape).astype(np.float32)
    session = ort.InferenceSession(str(onnx_path), providers=["CPUExecutionProvider"])
    expected = session.run(None, {session.get_inputs()[0].name: values})[0]
    started = time.monotonic()
    actual = next(iter(model.predict({"x": values}).values()))
    elapsed = time.monotonic() - started
    if actual.shape != expected.shape:
        raise RuntimeError(f"output shape mismatch: {actual.shape} != {expected.shape}")
    difference = np.abs(expected - actual)
    result: dict[str, object] = {
        "shape": list(shape),
        "outputShape": list(actual.shape),
        "maximumAbsoluteDifference": float(np.max(difference)),
        "meanAbsoluteDifference": float(np.mean(difference)),
        "elementsAbove1e-3": int(np.count_nonzero(difference > 1e-3)),
        "predictionMilliseconds": elapsed * 1000.0,
    }
    if actual.ndim == 3:
        expected_indices = np.argmax(expected, axis=2)
        actual_indices = np.argmax(actual, axis=2)
        result["argmaxMatches"] = int(np.count_nonzero(expected_indices == actual_indices))
        result["argmaxCount"] = int(expected_indices.size)
    return result


def probe_detector(bundle: Path, models: Path) -> dict[str, object]:
    detector_path = models / "detector-fp16.mlpackage"
    detector_ane = ct.models.MLModel(
        str(detector_path), compute_units=ct.ComputeUnit.CPU_AND_NE
    )
    detector_gpu = ct.models.MLModel(
        str(detector_path), compute_units=ct.ComputeUnit.CPU_AND_GPU
    )
    return {
        "ane": preferred_devices(detector_ane, "main"),
        "gpu": preferred_devices(detector_gpu, "main"),
        "parity": tensor_parity(
            bundle / "det" / "inference.onnx",
            detector_gpu,
            (1, 3, 768, 960),
            20260715,
        ),
    }


def probe_recognition(
    bundle: Path, models: Path, width: int, ane_maximum_width: int
) -> dict[str, object]:
    function_name = f"w{width:04d}"
    compute_units = (
        ct.ComputeUnit.CPU_AND_NE
        if width <= ane_maximum_width
        else ct.ComputeUnit.CPU_AND_GPU
    )
    package_path = models / "recognizer-fp16.mlpackage"
    model = ct.models.MLModel(
        str(package_path),
        compute_units=compute_units,
        function_name=function_name,
    )
    # MLComputePlan only assigns devices for a multifunction package's default
    # function. Rewrite only the temporary model spec's default, reusing the
    # original 91-function program and weights, so every routed function gets
    # placement evidence without changing the delivered program.
    placement_spec = ct.utils.load_spec(str(package_path))
    placement_spec.description.defaultFunctionName = function_name
    placement_model = ct.models.MLModel(
        placement_spec,
        weights_dir=str(package_path / "Data/com.apple.CoreML/weights"),
        compute_units=compute_units,
    )
    placement = preferred_devices(placement_model, function_name)
    record: dict[str, object] = {
        "width": width,
        "function": function_name,
        "requestedComputeUnits": compute_units.name,
        "placementInspection": "temporary-default-function-rewrite",
        "placement": placement,
    }
    if width in {320, 1024, 1600, 2176, 3200}:
        record["parity"] = tensor_parity(
            bundle / "rec" / "inference.onnx",
            model,
            (1, 3, 48, width),
            20260715 + width,
        )
    return record


def isolated_probe(command: list[str], context: str) -> dict[str, object]:
    try:
        completed = subprocess.run(
            command,
            check=False,
            capture_output=True,
            text=True,
            timeout=300,
        )
    except subprocess.TimeoutExpired as error:
        return {
            "status": "failed",
            "failure": {"context": context, "reason": "timeout", "seconds": 300},
        }
    if completed.returncode != 0:
        return {
            "status": "failed",
            "failure": {
                "context": context,
                "reason": "process_exit",
                "returnCode": completed.returncode,
                "stderrTail": completed.stderr[-4000:],
            },
        }
    try:
        payload = json.loads(completed.stdout.strip().splitlines()[-1])
    except (IndexError, json.JSONDecodeError) as error:
        return {
            "status": "failed",
            "failure": {
                "context": context,
                "reason": "invalid_probe_output",
                "detail": str(error),
                "stdoutTail": completed.stdout[-4000:],
                "stderrTail": completed.stderr[-4000:],
            },
        }
    payload["status"] = "passed"
    return payload


def placement_gate(
    detection: dict[str, object], recognition: list[dict[str, object]],
    ane_maximum_width: int, expected_widths: list[int],
) -> dict[str, object]:
    failures: list[str] = []
    if detection.get("status") != "passed":
        failures.append("detector probe did not complete")
    else:
        ane = detection["ane"]
        gpu = detection["gpu"]
        if ane["preferredDevices"].get("MLNeuralEngineComputeDevice", 0) == 0:
            failures.append("detector has no Neural Engine placement")
        if not scheduled_operations_fully_accounted_for(
            ane, "MLNeuralEngineComputeDevice", allow_cpu=True
        ):
            failures.append("detector has unexpected scheduled placement")
        if not cpu_operations_within_declaration(
            ane["cpuOperations"], QUALIFIED_DETECTOR_CPU_OPERATIONS
        ):
            failures.append("detector MLCPU operations exceed the declaration")
        if not scheduled_operations_fully_accounted_for(
            gpu, "MLGPUComputeDevice", allow_cpu=False
        ) or gpu["cpuOperations"]:
            failures.append("strict detector is not fully placed on GPU")
        parity = detection["parity"]
        if parity["maximumAbsoluteDifference"] > 0.01 or parity["meanAbsoluteDifference"] > 0.001:
            failures.append("detector tensor parity exceeds the locked tolerance")

    observed_widths: list[int] = []
    for record in recognition:
        width = int(record.get("width", -1))
        observed_widths.append(width)
        if record.get("status") != "passed":
            failures.append(f"recognizer width {width} probe did not complete")
            continue
        placement = record["placement"]
        devices = placement["preferredDevices"]
        if width <= ane_maximum_width:
            if devices.get("MLNeuralEngineComputeDevice", 0) == 0:
                failures.append(f"recognizer width {width} has no Neural Engine placement")
            if not scheduled_operations_fully_accounted_for(
                placement, "MLNeuralEngineComputeDevice", allow_cpu=True
            ):
                failures.append(
                    f"recognizer width {width} has unexpected scheduled placement"
                )
            if not cpu_operations_within_declaration(
                placement["cpuOperations"], QUALIFIED_RECOGNIZER_CPU_OPERATIONS
            ):
                failures.append(
                    f"recognizer width {width} MLCPU operations exceed the declaration"
                )
        elif not scheduled_operations_fully_accounted_for(
            placement, "MLGPUComputeDevice", allow_cpu=False
        ) or placement["cpuOperations"]:
            failures.append(f"recognizer width {width} is not fully placed on GPU")
        if "parity" in record:
            parity = record["parity"]
            if parity["argmaxMatches"] != parity["argmaxCount"]:
                failures.append(f"recognizer width {width} changes an argmax token")
    coverage_complete = observed_widths == expected_widths
    if not coverage_complete:
        failures.append("recognizer function coverage is incomplete")
    return {
        "passed": not failures,
        "coverageComplete": coverage_complete,
        "failures": failures,
        "declaredMaximumMLCPUOperations": {
            "detection": QUALIFIED_DETECTOR_CPU_OPERATIONS,
            "recognition": QUALIFIED_RECOGNIZER_CPU_OPERATIONS,
        },
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--bundle", type=Path, default=DEFAULT_BUNDLE)
    parser.add_argument("--models", type=Path, default=DEFAULT_MODELS)
    parser.add_argument("--report", type=Path, default=DEFAULT_REPORT)
    parser.add_argument("--acceptance", type=Path, default=DEFAULT_ACCEPTANCE)
    parser.add_argument("--ane-maximum-width", type=int, default=1600)
    parser.add_argument(
        "--qualification-id",
        default="apple-fp16-mixed-20260715.2",
    )
    parser.add_argument("--widths", help="Comma-separated development subset")
    parser.add_argument("--jobs", type=int, default=1,
                        help="Independent recognition probe processes")
    parser.add_argument("--probe-kind", choices=("detector", "recognition"),
                        help=argparse.SUPPRESS)
    parser.add_argument("--probe-width", type=int, help=argparse.SUPPRESS)
    arguments = parser.parse_args()
    if arguments.jobs < 1 or arguments.jobs > 8:
        parser.error("--jobs must be between 1 and 8")
    acceptance = json.loads(arguments.acceptance.read_text("utf-8"))
    locked_routing = acceptance["routing"]
    if arguments.qualification_id != acceptance["qualificationId"]:
        parser.error("--qualification-id must match the locked acceptance")
    if arguments.ane_maximum_width != locked_routing["recognitionAneMaximumWidth"]:
        parser.error("--ane-maximum-width must match the locked acceptance")
    bundle = arguments.bundle.resolve()
    models = arguments.models.resolve()
    provenance = json.loads((models / "provenance.json").read_text("utf-8"))

    if arguments.probe_kind == "detector":
        print(json.dumps(probe_detector(bundle, models), sort_keys=True))
        return 0
    if arguments.probe_kind == "recognition":
        if arguments.probe_width is None:
            parser.error("--probe-width is required for a recognition probe")
        print(json.dumps(probe_recognition(
            bundle, models, arguments.probe_width, arguments.ane_maximum_width
        ), sort_keys=True))
        return 0

    all_widths = list(provenance["recognition"]["widths"])
    widths = all_widths if arguments.widths is None else [
        int(value) for value in arguments.widths.split(",") if value
    ]
    base_command = [
        sys.executable,
        str(Path(__file__).resolve()),
        "--bundle", str(bundle),
        "--models", str(models),
        "--acceptance", str(arguments.acceptance.resolve()),
        "--ane-maximum-width", str(arguments.ane_maximum_width),
        "--qualification-id", str(arguments.qualification_id),
    ]
    detection = isolated_probe(
        base_command + ["--probe-kind", "detector"], "detector"
    )
    def recognition_probe(width: int) -> dict[str, object]:
        record = isolated_probe(
            base_command + ["--probe-kind", "recognition", "--probe-width", str(width)],
            f"recognizer width {width}",
        )
        record.setdefault("width", width)
        return record

    recognition_by_width: dict[int, dict[str, object]] = {}
    with ThreadPoolExecutor(max_workers=arguments.jobs) as executor:
        futures = {executor.submit(recognition_probe, width): width for width in widths}
        for future in as_completed(futures):
            width = futures[future]
            record = future.result()
            recognition_by_width[width] = record
            print(json.dumps({"width": width, "status": record["status"]}), flush=True)
    recognition = [recognition_by_width[width] for width in widths]

    report = {
        "schemaVersion": "1.0",
        "qualificationId": arguments.qualification_id,
        "device": {
            "platform": platform.platform(),
            "machine": platform.machine(),
            "processor": platform.processor(),
        },
        "runtime": {"coremltools": ct.__version__, "onnxruntime": ort.__version__},
        "probeProcesses": arguments.jobs,
        "models": provenance,
        "routing": {
            "recognitionWidthMultiple": locked_routing["recognitionWidthMultiple"],
            "aneMaximumWidth": arguments.ane_maximum_width,
            "runtimeWidthBuckets": locked_routing["recognitionRuntimeWidthBuckets"],
            "maximumCachedFunctions": locked_routing["maximumCachedFunctions"],
        },
        "detection": detection,
        "recognition": recognition,
    }
    report["gate"] = placement_gate(
        detection, recognition, arguments.ane_maximum_width, all_widths
    )
    encoded = json.dumps(report, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
    report["reportSha256"] = hashlib.sha256(encoded.encode("utf-8")).hexdigest()
    arguments.report.parent.mkdir(parents=True, exist_ok=True)
    arguments.report.write_text(
        json.dumps(report, ensure_ascii=False, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(json.dumps({
        "passed": report["gate"]["passed"],
        "report": str(arguments.report),
        "reportSha256": report["reportSha256"],
    }, ensure_ascii=False, sort_keys=True))
    return 0 if report["gate"]["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
