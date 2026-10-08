#!/usr/bin/env python3
"""Build the pinned AMD AIE compiler without device access or test targets.

Source checkout/submodule population and checksum-verified Peano extraction
are prerequisites. All outputs remain development tooling, never npm payload.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

LOCK = Path(__file__).with_name("iree-source.lock.json")


def head(path: Path, configure_diff=None) -> str:
    changes = subprocess.check_output(
        ["git", "-C", str(path), "status", "--porcelain", "--untracked-files=no", "--ignore-submodules=all"],
        text=True)
    if changes:
        diff = subprocess.check_output(["git", "-C", str(path), "diff", "--binary", "--no-ext-diff",
                                        "--no-color", "--ignore-submodules=all", "--src-prefix=a/", "--dst-prefix=b/", "HEAD", "--"])
        if configure_diff is None or hashlib.sha256(diff).hexdigest() != configure_diff:
            raise ValueError(f"source contains unexpected tracked modifications: {path}")
    return subprocess.check_output(["git", "-C", str(path), "rev-parse", "HEAD"], text=True).strip()


def apply_source_patches(source: Path, lock: dict) -> None:
    patches = []
    for record in lock.get("localPatches", []):
        patch = LOCK.parent / record["path"]
        with patch.open("rb") as stream:
            if hashlib.file_digest(stream, "sha256").hexdigest() != record["sha256"]:
                raise ValueError("local compiler patch hash mismatch")
        patches.append(str(patch.resolve()))
    dirty = subprocess.check_output(["git", "-C", str(source), "status", "--porcelain",
                                     "--untracked-files=no", "--ignore-submodules=all"], text=True)
    if patches and not dirty:
        subprocess.run(["git", "-C", str(source), "apply", "--check", *patches], check=True)
        subprocess.run(["git", "-C", str(source), "apply", *patches], check=True)
    head(source, lock.get("localSourceDiffSha256"))


def build(source: Path, peano: Path, output: Path, jobs: int, configure_only=False,
          runtime_only=False) -> dict:
    if jobs < 1:
        raise ValueError("jobs must be positive")
    source, peano, output = source.resolve(), peano.resolve(), output.resolve()
    if output == source or output.is_relative_to(source):
        raise ValueError("build output must be separate from the source tree")
    lock = json.loads(LOCK.read_text("utf-8"))
    if head(source, lock.get("localSourceDiffSha256")) != lock["amdAie"]["commit"]:
        raise ValueError("AMD AIE source commit mismatch")
    apply_source_patches(source, lock)
    revisions = {"amdAie": head(source, lock.get("localSourceDiffSha256"))}
    for relative, expected in lock["amdAie"]["submodules"].items():
        actual = head(source / relative, lock.get("upstreamConfigureDiffSha256", {}).get(relative))
        if actual != expected:
            raise ValueError(f"source submodule commit mismatch: {relative}")
        revisions[relative] = actual
    iree = source / "third_party/iree"
    for relative, expected in lock["ireeSubmodules"].items():
        actual = head(iree / relative)
        if actual != expected:
            raise ValueError(f"IREE submodule commit mismatch: {relative}")
        revisions[f"iree/{relative}"] = actual
    # Archive extraction should preserve executable modes.
    if not os.access(peano / "bin/clang", os.X_OK):
        raise ValueError("Peano clang is missing or not executable")
    provenance = json.loads((peano / "toolchain-source.json").read_text("utf-8"))
    if provenance["source"] != lock["peano"]:
        raise ValueError("Peano source identity mismatch; use fetch_peano.py")
    if [b["path"] for b in provenance["binaries"]] != ["bin/clang", "bin/ld.lld"]:
        raise ValueError("Peano provenance must include clang and ld.lld")
    for binary in provenance["binaries"]:
        if binary["path"] not in ("bin/clang", "bin/ld.lld"):
            raise ValueError("unexpected Peano binary record")
        with (peano / binary["path"]).open("rb") as stream:
            if hashlib.file_digest(stream, "sha256").hexdigest() != binary["sha256"]:
                raise ValueError("Peano binary hash mismatch")
    for tool in ("cmake", "ninja", "clang", "clang++"):
        if shutil.which(tool) is None:
            raise ValueError(f"required build tool missing: {tool}")
    output.mkdir(parents=True, exist_ok=True)
    kind = "runtime" if runtime_only else "compiler"
    previous_path = output / "toolchain-build.json"
    previous = json.loads(previous_path.read_text("utf-8")) if previous_path.exists() else {}
    if previous and previous.get("kind", "compiler") != kind:
        raise ValueError("compiler and runtime builds require separate output directories")
    flags = ["-GNinja", "-DCMAKE_BUILD_TYPE=Release",
             "-DCMAKE_C_COMPILER=clang", "-DCMAKE_CXX_COMPILER=clang++",
             "-DIREE_BUILD_TESTS=OFF", "-DIREE_BUILD_SAMPLES=OFF",
             "-DIREE_BUILD_PYTHON_BINDINGS=OFF",
             "-DIREE_ENABLE_ASSERTIONS=OFF", "-DIREE_ERROR_ON_MISSING_SUBMODULES=OFF",
             "-DIREE_INPUT_STABLEHLO=OFF", "-DIREE_INPUT_TORCH=OFF", "-DIREE_INPUT_TOSA=OFF",
             "-DIREE_TARGET_BACKEND_DEFAULTS=OFF", "-DIREE_TARGET_BACKEND_LLVM_CPU=ON",
             "-DIREE_HAL_DRIVER_DEFAULTS=OFF", "-DIREE_HAL_DRIVER_LOCAL_TASK=ON",
             "-DIREE_HAL_DRIVER_LOCAL_SYNC=ON", "-DIREE_EXTERNAL_HAL_DRIVERS=amdxdna",
             "-DENABLE_AMDXDNA_CTS_TESTS=OFF", "-DENABLE_AMDXDNA_AIE_EXECUTABLE_TESTS=OFF",
             "-DIREE_DEFAULT_CPU_LLVM_TARGETS=X86", "-DLLVM_TARGETS_TO_BUILD=X86", "-DLLVM_INCLUDE_TESTS=OFF",
             "-DLLVM_INCLUDE_BENCHMARKS=OFF", "-DLLVM_ENABLE_ASSERTIONS=OFF",
             "-DLLVM_PARALLEL_LINK_JOBS=1",
             f"-DIREE_CMAKE_PLUGIN_PATHS={source}", f"-DPEANO_INSTALL_DIR={peano}",
             f"-DOPENSSL_BUILD_HASH={lock['openssl']['sha256']}"]
    linker = peano / "bin/ld.lld"
    if os.access(linker, os.X_OK):
        for linker_kind in ("EXE", "SHARED", "MODULE"):
            flags.append(f"-DCMAKE_{linker_kind}_LINKER_FLAGS=-fuse-ld={linker}")
    if runtime_only:
        flags.append("-DIREE_BUILD_COMPILER=OFF")
    configure = ["cmake", "-S", str(iree), "-B", str(output), *flags]
    names = ("iree-run-module", "iree-dump-module") if runtime_only else ("iree-compile", "iree-opt")
    compile_command = ["cmake", "--build", str(output), "--target", *names,
                       "--parallel", str(jobs)]
    manifest = {"schemaVersion": "1.0", "stage": "development", "kind": "runtime" if runtime_only else "compiler",
                "sourceCommits": revisions,
                "upstreamConfigureDiffSha256": lock.get("upstreamConfigureDiffSha256", {}),
                "localPatches": lock.get("localPatches", []),
                "localSourceDiffSha256": lock.get("localSourceDiffSha256"),
                "peanoVersion": lock["peano"]["version"], "openssl": lock["openssl"], "configureCommand": configure,
                "buildCommand": compile_command, "status": "configuring", "deviceValidated": False,
                "hostTools": {tool: subprocess.check_output([tool, "--version"], text=True).splitlines()[0]
                              for tool in ("cmake", "ninja", "clang")}}
    report = output / "toolchain-build.json"
    reuse_configuration = (not configure_only and (output / "CMakeCache.txt").is_file() and
                           previous.get("configureCommand") == configure and
                           previous.get("sourceCommits") == revisions and
                           previous.get("hostTools") == manifest["hostTools"] and
                           (previous.get("configurationCompleted") or
                            previous.get("status") in ("built", "building", "configured")))
    manifest["configurationReused"] = bool(reuse_configuration)

    def write():
        report.write_text(json.dumps(manifest, indent=2) + "\n", "utf-8")

    write()
    try:
        if not reuse_configuration:
            subprocess.run(configure, check=True)
        # Upstream CMake patches these checkouts in place. Accept only the
        # exact changes produced by the locked configuration, including on
        # subsequent incremental builds.
        for relative in lock["amdAie"]["submodules"]:
            head(source / relative, lock.get("upstreamConfigureDiffSha256", {}).get(relative))
        manifest["configurationCompleted"] = True
        manifest["status"] = "configured" if configure_only else "building"
        write()
        if not configure_only:
            subprocess.run(compile_command, check=True)
            manifest["binaries"] = []
            paths = [f"tools/{name}" for name in names]
            if not runtime_only:
                paths.append("lib/libIREECompiler.so")
            for relative in paths:
                path = output / relative
                with path.open("rb") as stream:
                    identity = hashlib.file_digest(stream, "sha256").hexdigest()
                manifest["binaries"].append({"path": relative, "bytes": path.stat().st_size,
                                             "sha256": identity})
            manifest["status"] = "built"
            write()
    except subprocess.CalledProcessError as exc:
        manifest.update(status="failed", returnCode=exc.returncode)
        write()
        raise
    return manifest


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-dir", type=Path, required=True)
    parser.add_argument("--peano-dir", type=Path, required=True)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--jobs", type=int, default=2)
    parser.add_argument("--configure-only", action="store_true")
    parser.add_argument("--runtime-only", action="store_true",
                        help="build standalone execution tools in a separate directory")
    args = parser.parse_args()
    result = build(args.source_dir, args.peano_dir, args.build_dir, args.jobs, args.configure_only, args.runtime_only)
    print(f"AMD AIE {result['kind']}: {result['status']}")


if __name__ == "__main__":
    main()
