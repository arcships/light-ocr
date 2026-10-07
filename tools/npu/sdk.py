"""Validate and stage NPU SDKs without trusting system library search paths."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import shutil
import re

GATES = {"quality", "performance", "coldStart", "cache", "lifecycle",
         "failurePaths", "autoSelection", "offlineDistribution", "placement",
         "redistribution"}


def validate_acceptance(value: dict, reports: list[dict]) -> None:
    """Validate reviewed gate reports, never infer acceptance from build success."""
    if not reports:
        raise ValueError("NPU acceptance requires device reports")
    families = set()
    for report in reports:
        if (report.get("schemaVersion") != "1.0" or
                report.get("provider") != value["provider"] or
                report.get("platformId") != value["platformId"] or
                report.get("artifactSetSha256") != value["artifactSetSha256"] or
                report.get("configuration") != value["configuration"] or
                not isinstance(report.get("reviewedBy"), str) or not report["reviewedBy"].strip() or
                not isinstance(report.get("evidence"), dict) or not report["evidence"] or
                set(report.get("gates", {})) != GATES or
                any(result is not True for result in report["gates"].values()) or
                not isinstance(report.get("deviceFamily"), str) or not report["deviceFamily"]):
            raise ValueError("NPU device report is not reviewed, complete, or bound to this SDK")
        families.add(report["deviceFamily"])
    if value["provider"] == "openvino" and any(
            not re.fullmatch(r"[1-9][0-9]*", family) for family in families):
        raise ValueError("Intel deviceFamily must be the numeric DEVICE_ARCHITECTURE property")
    if value["provider"] == "openvino" and len(families) < 2:
        raise ValueError("Intel release requires reviewed reports from at least two NPU generations")
    if value["provider"] == "amdnpu" and not families <= {"STX", "KRK", "STX/KRK"}:
        raise ValueError("AMD BF16 release requires STX/KRK device reports")


def digest(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def record(path: Path, root: Path) -> dict:
    return {"path": path.relative_to(root).as_posix(),
            "bytes": path.stat().st_size, "sha256": digest(path)}


def relative_path(value: str) -> PurePosixPath:
    if not isinstance(value, str):
        raise ValueError("NPU artifact path must be a string")
    path = PurePosixPath(value)
    if (not value or value == "." or path.is_absolute() or ".." in path.parts or
            "\\" in value or path.as_posix() != value):
        raise ValueError(f"unsafe NPU artifact path: {value}")
    return path


def verify_record(item: dict, root: Path) -> Path:
    if not isinstance(item, dict) or set(item) != {"path", "bytes", "sha256"}:
        raise ValueError("invalid NPU artifact record")
    path = root.joinpath(*relative_path(item["path"]).parts)
    if (path.is_symlink() or not path.is_file() or
            any(parent.is_symlink() for parent in path.parents if parent != root.parent)):
        raise ValueError(f"NPU artifact must be a regular file: {path}")
    path.resolve().relative_to(root.resolve())
    if (type(item["bytes"]) is not int or item["bytes"] <= 0 or
            path.stat().st_size != item["bytes"] or digest(path) != item["sha256"]):
        raise ValueError(f"NPU artifact identity mismatch: {path}")
    return path


def artifact_set(items: list[dict]) -> str:
    return hashlib.sha256(json.dumps(items, sort_keys=True,
                                    separators=(",", ":")).encode()).hexdigest()


def validate_aie_configuration(compiler: dict, configuration: dict, artifacts: list[dict], root: Path) -> None:
    if (compiler.get("runtimeAbi") != 1 or compiler.get("device") != "npu4" or
            compiler.get("sourceModelSha256") != configuration["sourceModelSha256"] or
            compiler.get("parameterScope") != "recognition" or
            compiler.get("precision") != "bf16-bfp16ebs8" or
            compiler.get("cpuPartitionRequired") is not True or
            compiler.get("widths") != recognition_widths() or
            not isinstance(compiler.get("runtimeVersion"), str) or not compiler["runtimeVersion"]):
        raise ValueError("invalid AMD AIE deployment configuration")
    parameters = compiler.get("parameters", {})
    relative_path(parameters.get("path"))
    prefix = PurePosixPath(configuration["compilerConfiguration"]["path"]).parent
    item = {**parameters, "path": (prefix / parameters["path"]).as_posix()}
    verify_record(item, root)
    if item not in artifacts:
        raise ValueError("AMD shared parameters are outside the runtime inventory")
    if any(not item["artifact"]["path"].endswith(".vmfb") for item in configuration["recognitionModels"]):
        raise ValueError("AMD AIE requires compiled VMFB recognition modules")


def validate_sdk(root: Path, provider: str | None = None) -> dict:
    root = root.absolute()
    if (root / "sdk-manifest.json").is_symlink():
        raise ValueError("NPU SDK manifest must be a regular file")
    value = json.loads((root / "sdk-manifest.json").read_text("utf-8"))
    if (value.get("schemaVersion") != "1.0" or
            value.get("provider") not in {"openvino", "amdnpu"} or
            value.get("platformId") != "linux-x64" or
            (provider and value["provider"] != provider) or
            type(value.get("qualificationOnly")) is not bool or
            not isinstance(value.get("qualificationId"), str) or
            not re.fullmatch(r"[A-Za-z0-9_.-]+", value["qualificationId"]) or
            not isinstance(value.get("providerVersion"), str) or not value["providerVersion"]):
        raise ValueError("invalid NPU SDK identity")
    files = value.get("files", [])
    if not files or len({item["path"] for item in files}) != len(files):
        raise ValueError("duplicate or empty NPU SDK inventory")
    for item in files:
        verify_record(item, root)
    if any(p.is_symlink() for p in root.rglob("*")):
        raise ValueError("NPU SDK inventory cannot contain symlinks")
    actual = {p.relative_to(root).as_posix() for p in root.rglob("*")
              if p.is_file() and p != root / "sdk-manifest.json"}
    if actual != {item["path"] for item in files}:
        raise ValueError("NPU SDK inventory is incomplete")
    artifacts = value.get("artifacts", [])
    if (not artifacts or len({item["path"] for item in artifacts}) != len(artifacts)
            or any(item not in files for item in artifacts)):
        raise ValueError("NPU runtime artifacts are outside the SDK inventory")
    if artifact_set(artifacts) != value.get("artifactSetSha256"):
        raise ValueError("NPU artifact set hash mismatch")
    if value.get("runtimeLibrary") not in {item["path"] for item in artifacts}:
        raise ValueError("NPU runtime library is absent from the SDK")
    if not value.get("licenses") or any(item not in files for item in value["licenses"]):
        raise ValueError("NPU SDK has no verified licenses")
    config = value.get("configuration", {})
    if value["provider"] == "openvino":
        if set(config) != {"runtimeVersionPrefix", "minimumDriverVersion", "minimumCompilerVersion"}:
            raise ValueError("invalid OpenVINO configuration")
        if (any(not isinstance(item, str) or not item for item in config.values()) or
                not re.fullmatch(r"[0-9]+", config["minimumDriverVersion"]) or
                not re.fullmatch(r"[0-9]+", config["minimumCompilerVersion"]) or
                any(int(config[key]) > 2**64 - 1 for key in
                    ("minimumDriverVersion", "minimumCompilerVersion"))):
            raise ValueError("invalid OpenVINO numeric driver/compiler floors")
        required = {"libopenvino_c.so.2640", "libopenvino.so.2640",
                    "libopenvino_intel_npu_plugin.so", "libopenvino_onnx_frontend.so.2640",
                    "libtbb.so.12"}
        if {Path(item["path"]).name for item in artifacts} != required:
            raise ValueError("incomplete OpenVINO NPU dependency closure")
    else:
        if set(config) != {"sourceModelSha256", "recognitionModels", "compilerConfiguration"}:
            raise ValueError("invalid AMD NPU configuration")
        widths = [item.get("width") for item in config["recognitionModels"]]
        if widths != recognition_widths():
            raise ValueError("AMD NPU recognition must contain all 20 locked width buckets")
        if (not isinstance(config["sourceModelSha256"], str) or
                not re.fullmatch(r"[0-9a-f]{64}", config["sourceModelSha256"]) or
                len({item["artifact"]["path"] for item in config["recognitionModels"]}) != 20 or
                config["compilerConfiguration"] not in artifacts or
                any(item["artifact"] not in artifacts for item in config["recognitionModels"])):
            raise ValueError("AMD NPU recognition models are outside the SDK inventory")
        compiler = json.loads(verify_record(config["compilerConfiguration"], root).read_text("utf-8"))
        if compiler.get("target") == "IREEAMDAIE":
            validate_aie_configuration(compiler, config, artifacts, root)
            if Path(value["runtimeLibrary"]).name != "liblight_ocr_amdaie.so.1":
                raise ValueError("invalid AMD AIE runtime library")
        elif (compiler.get("target") != "VAIML" or
                not any(item.get("name") == "vaiml_partition" for item in compiler.get("passes", []))):
            raise ValueError("AMD NPU SDK must use a VAIML BF16 configuration")
    # A production SDK must carry reviewed evidence bound to the exact bytes.
    if not value["qualificationOnly"]:
        accepted = value.get("acceptance", {})
        if (accepted.get("artifactSetSha256") != value["artifactSetSha256"] or
                accepted.get("providerGatePassed") is not True or not accepted.get("reports") or
                any(item not in files for item in accepted["reports"])):
            raise ValueError("NPU SDK has no accepted qualification evidence")
        validate_acceptance(value, [json.loads(verify_record(item, root).read_text("utf-8"))
                                    for item in accepted["reports"]])
    return value


def recognition_widths() -> list[int]:
    return [320, 384, 480, 544, 576, 608, 704, 736, 832, 960,
            1056, 1184, 1248, 1376, 1600, 1984, 2240, 2560, 2880, 3200]


def validate_descriptor_npu(descriptor: dict, root: Path) -> set[str]:
    """Check the additional providers before accepting their runtime inventory."""
    paths = set()
    for name in ("openvino", "amdnpu"):
        provider = descriptor["providers"].get(name)
        if provider is None:
            continue
        if descriptor["schemaVersion"] != "2.1" or descriptor["platform"]["id"] != "linux-x64":
            raise ValueError("NPU providers require schema 2.1 and Linux x64 glibc")
        if set(provider) != {"runtimeProvider", "providerVersion", "qualificationId",
                             "providerLibrary", "configuration", "artifacts"}:
            raise ValueError("invalid NPU provider descriptor fields")
        expected = {"OpenVINO"} if name == "openvino" else {"VitisAIExecutionProvider", "IREEAMDAIE"}
        if (provider["runtimeProvider"] not in expected or not provider["providerVersion"] or
                not provider["qualificationId"] or not provider["artifacts"] or
                provider["providerLibrary"] not in provider["artifacts"]):
            raise ValueError("invalid NPU provider descriptor identity")
        seen = set()
        for item in provider["artifacts"]:
            verify_record(item, root)
            if (not item["path"].startswith(f"native/{name}/") or
                    item not in descriptor["runtime"]["artifacts"] or item["path"] in seen):
                raise ValueError("NPU artifact is outside its provider inventory")
            seen.add(item["path"])
        paths.update(seen)
        config = provider["configuration"]
        if name == "openvino":
            if (set(config) != {"runtimeVersionPrefix", "minimumDriverVersion", "minimumCompilerVersion"}
                    or any(not isinstance(v, str) or not v for v in config.values())
                    or not config["minimumDriverVersion"].isascii()
                    or not config["minimumDriverVersion"].isdecimal()
                    or not config["minimumCompilerVersion"].isascii()
                    or not config["minimumCompilerVersion"].isdecimal()):
                raise ValueError("invalid OpenVINO numeric driver/compiler floors")
            expected_names = {"libopenvino_c.so.2640", "libopenvino.so.2640",
                              "libopenvino_intel_npu_plugin.so", "libopenvino_onnx_frontend.so.2640",
                              "libtbb.so.12"}
            if {Path(p).name for p in seen} != expected_names:
                raise ValueError("incomplete OpenVINO dependency closure")
            if Path(provider["providerLibrary"]["path"]).name != "libopenvino_c.so.2640":
                raise ValueError("invalid OpenVINO C runtime library")
        else:
            if (set(config) != {"sourceModelSha256", "recognitionModels", "compilerConfiguration"} or
                    not isinstance(config["sourceModelSha256"], str) or
                    len(config["sourceModelSha256"]) != 64 or
                    any(c not in "0123456789abcdef" for c in config["sourceModelSha256"])):
                raise ValueError("invalid AMD NPU source model identity")
            if config["compilerConfiguration"] not in provider["artifacts"]:
                raise ValueError("AMD NPU compiler configuration is outside its inventory")
            models = config["recognitionModels"]
            if ([item.get("width") for item in models] != recognition_widths() or
                    any(set(item) != {"width", "artifact"} or
                        item["artifact"] not in provider["artifacts"] for item in models)):
                raise ValueError("incomplete AMD NPU recognition buckets")
            if provider["runtimeProvider"] == "IREEAMDAIE":
                compiler = json.loads(verify_record(config["compilerConfiguration"], root).read_text("utf-8"))
                if compiler.get("target") != "IREEAMDAIE" or Path(provider["providerLibrary"]["path"]).name != "liblight_ocr_amdaie.so.1":
                    raise ValueError("invalid AMD AIE runtime identity")
                validate_aie_configuration(compiler, config, provider["artifacts"], root)
            elif not Path(provider["providerLibrary"]["path"]).name.startswith("libonnxruntime.so"):
                raise ValueError("invalid AMD ORT runtime library")
    return paths


def stage_sdk(root: Path, stage: Path, descriptor: dict) -> dict:
    value = validate_sdk(root)
    provider = value["provider"]
    if descriptor["platform"]["id"] != value["platformId"]:
        raise ValueError("NPU SDK platform does not match the native package")
    prefix = f"native/{provider}/"
    artifacts = []
    by_path = {}
    for item in value["artifacts"]:
        source = verify_record(item, root)
        destination = stage / (prefix + item["path"])
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, destination)
        output = record(destination, stage)
        artifacts.append(output)
        by_path[item["path"]] = output
    configuration = dict(value["configuration"])
    if provider == "amdnpu":
        configuration["compilerConfiguration"] = by_path[configuration["compilerConfiguration"]["path"]]
        configuration["recognitionModels"] = [
            {"width": item["width"], "artifact": by_path[item["artifact"]["path"]]}
            for item in configuration["recognitionModels"]]
    amd_aie = provider == "amdnpu" and Path(value["runtimeLibrary"]).name == "liblight_ocr_amdaie.so.1"
    descriptor["providers"][provider] = {
        "runtimeProvider": "OpenVINO" if provider == "openvino" else ("IREEAMDAIE" if amd_aie else "VitisAIExecutionProvider"),
        "providerVersion": value["providerVersion"],
        "qualificationId": value["qualificationId"],
        "providerLibrary": by_path[value["runtimeLibrary"]],
        "configuration": configuration, "artifacts": artifacts,
    }
    descriptor["runtime"]["artifacts"].extend(artifacts)
    # NPU candidates are ordered consistently across Core, addon and loader.
    descriptor["autoPolicy"]["providers"] = [
        name for name in ["openvino", "webgpu", "cpu"]
        if name in descriptor["providers"]]
    # SDK qualification metadata records optional hardware evidence. Shipping
    # an unvalidated NPU backend does not change the base runtime release gate.
    descriptor["schemaVersion"] = "2.1"
    return value


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--sdk-dir", type=Path, required=True)
    parser.add_argument("--provider", choices=["openvino", "amdnpu"])
    parser.add_argument("--stage-dir", type=Path)
    parser.add_argument("--emit-header", type=Path)
    args = parser.parse_args()
    manifest = validate_sdk(args.sdk_dir, args.provider)
    if args.emit_header:
        provider = manifest["provider"]
        q = json.dumps
        library = next(item for item in manifest["artifacts"] if item["path"] == manifest["runtimeLibrary"])
        lines = ["#pragma once", '#include "core/engine_factory.hpp"',
                 "namespace light_ocr::internal {",
                 f"inline void apply_{provider}_sdk_defaults(RuntimePolicy& policy) {{"]
        library_path = (args.sdk_dir.absolute() / library["path"]).as_posix()
        if provider == "openvino":
            lines.extend([f"policy.openvino_runtime_library = {q(library_path)};",
                          f"policy.openvino_runtime_bytes = {library['bytes']}ULL;",
                          f"policy.openvino_runtime_sha256 = {q(library['sha256'])};"])
            for field, key in [("runtime_version_prefix", "runtimeVersionPrefix"),
                               ("minimum_driver_version", "minimumDriverVersion"),
                               ("minimum_compiler_version", "minimumCompilerVersion")]:
                lines.append(f"policy.openvino_{field} = {q(manifest['configuration'][key])};")
        else:
            lines.append(f"policy.amdnpu_runtime = {{{q(library_path)}, {library['bytes']}ULL, {q(library['sha256'])}}};")
            file = manifest["configuration"]["compilerConfiguration"]
            file_path = (args.sdk_dir.absolute() / file["path"]).as_posix()
            lines.append(f"policy.amdnpu_compiler_configuration = {{{q(file_path)}, {file['bytes']}ULL, {q(file['sha256'])}}};")
            lines.append(f"policy.amdnpu_source_model_sha256 = {q(manifest['configuration']['sourceModelSha256'])};")
            for item in manifest["configuration"]["recognitionModels"]:
                file = item["artifact"]
                file_path = (args.sdk_dir.absolute() / file["path"]).as_posix()
                lines.append(f"policy.amdnpu_recognition_models.push_back({{{item['width']}, "
                             f"{{{q(file_path)}, {file['bytes']}ULL, {q(file['sha256'])}}}}});")
        lines.extend(["}", "}", ""])
        args.emit_header.parent.mkdir(parents=True, exist_ok=True)
        args.emit_header.write_text("\n".join(lines), "utf-8")
    if args.stage_dir:
        path = args.stage_dir / "native/runtime-descriptor.json"
        descriptor = json.loads(path.read_text("utf-8"))
        stage_sdk(args.sdk_dir, args.stage_dir, descriptor)
        path.write_text(json.dumps(descriptor, indent=2) + "\n", "utf-8")


if __name__ == "__main__":
    main()
