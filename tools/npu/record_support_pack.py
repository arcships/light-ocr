#!/usr/bin/env python3
"""Record an actual npm archive and enforce the lightweight AMD size budget."""
from __future__ import annotations
import argparse
import json
from pathlib import Path
from sdk import digest


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package-dir",type=Path,required=True)
    parser.add_argument("--pack-manifest",type=Path,required=True)
    parser.add_argument("--output-file",type=Path,required=True)
    a=parser.parse_args()
    packed=json.loads(a.pack_manifest.read_text())
    if not isinstance(packed,list) or len(packed)!=1:raise ValueError("expected one npm archive")
    item=packed[0]
    filename=item["filename"]
    if Path(filename).name!=filename:raise ValueError("unsafe npm archive name")
    archive=a.pack_manifest.parent/filename
    if archive.stat().st_size!=item["size"]:raise ValueError("npm archive size differs from manifest")
    descriptor=json.loads((a.package_dir/"native/runtime-descriptor.json").read_text())
    lightweight=descriptor["providers"].get("amdnpu",{}).get("runtimeProvider")=="IREEAMDAIE"
    report={"package":item["name"],"version":item["version"],
            "archive":{"path":filename,"bytes":archive.stat().st_size,"sha256":digest(archive)},
            "unpackedBytes":item["unpackedSize"],"fileCount":len(item["files"]),
            "lightweightAmd":lightweight,"deviceValidated":False,"numericsValidated":False}
    if lightweight:
        report["budget"]={"compressedBytes":64*1024*1024,"unpackedBytes":128*1024*1024}
        if item["size"]>report["budget"]["compressedBytes"] or item["unpackedSize"]>report["budget"]["unpackedBytes"]:
            raise ValueError("lightweight AMD package exceeds its 64 MiB/128 MiB budget")
    a.output_file.write_text(json.dumps(report,indent=2)+"\n")
    print(f"{item['name']}: {item['size']} compressed / {item['unpackedSize']} unpacked bytes")


if __name__=="__main__":main()
