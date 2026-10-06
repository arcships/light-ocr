#!/usr/bin/env python3
"""Package one NPU addon independently from the default npm install closure."""
from __future__ import annotations
import argparse
from pathlib import Path
import sys
from types import SimpleNamespace
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tools import npm_release as release


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument('--provider', choices=['openvino', 'amdnpu'], required=True)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--metadata-dir', type=Path, required=True)
    parser.add_argument('--sdk-dir', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    args = parser.parse_args()
    provider = args.provider
    name = f'@arcships/light-ocr-{provider}-linux-x64-gnu'
    release.stage_native(SimpleNamespace(platform_id='linux-x64', build_dir=args.build_dir,
        metadata_dir=args.metadata_dir, output_dir=args.output_dir, configuration='Release',
        runtime_flavor='cpu', qualification_build=False, pdfium_dir=None,
        openvino_sdk_dir=args.sdk_dir if provider == 'openvino' else None,
        amdnpu_sdk_dir=args.sdk_dir if provider == 'amdnpu' else None,
        npu_support_provider=provider))
    output = args.output_dir
    (output / 'native-input.json').unlink()
    package = release.common_package(name, release.CORE_VERSION,
        f'Explicitly installed {provider} NPU support for light-ocr on Linux x64 glibc')
    package.update({'main': './native/light_ocr_node.node',
        'exports': {'.': './native/light_ocr_node.node'}, 'os': ['linux'], 'cpu': ['x64'],
        'libc': ['glibc'], 'engines': {'node': '>=22.0.0'},
        'files': ['native/', 'licenses/', 'qualification/', 'license-inventory.json',
                  'sbom.spdx.json', 'artifact-hashes.json', 'README.md', 'LICENSE', 'NOTICE']})
    release.write_json(output / 'package.json', package)
    release.add_project_files(output, f'''# {name}

Install this package explicitly alongside `@arcships/light-ocr`.
It is excluded from the default dependency closure and contains a separate NPU addon.
Use `execution: {{ provider: "{provider}" }}` to select it.
Vendor drivers are required. Hardware inference has not been qualified.
''' + ('AMD contexts are bound to Small 0.3.4; Tiny/Medium require their own compiled contexts.\n'
       if provider == 'amdnpu' else 'OpenVINO compiles the selected ONNX models through the installed NPU driver.\n'))
    release.artifact_hashes(output, name, release.CORE_VERSION)
    print(name)


if __name__ == '__main__':
    main()
