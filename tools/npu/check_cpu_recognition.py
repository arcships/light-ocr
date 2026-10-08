#!/usr/bin/env python3
"""Compare the original ONNX and IREE recognition transforms on CPU only.

The partitioned CPU model emulates BF16 operands with CPU matmul; it does not
execute the AMD BFP16ebs8 microkernel or qualify the packaged NPU binaries.
"""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import subprocess
import sys

REFERENCE = '''import sys,numpy as np,onnxruntime as ort
options=ort.SessionOptions();options.intra_op_num_threads=2
session=ort.InferenceSession(sys.argv[1],options,providers=["CPUExecutionProvider"])
for source,destination in zip(sys.argv[2::2],sys.argv[3::2]):
 np.save(destination,session.run(None,{session.get_inputs()[0].name:np.load(source)})[0])
'''
COMPARE = '''import sys,json,numpy as np
records=[]
def decode(array):
 result=[];previous=-1
 for value in array.argmax(-1).reshape(-1).tolist():
  if value!=previous and value!=0:result.append(value)
  previous=value
 return result
for record in json.load(open(sys.argv[1])):
 reference=np.load(record.pop("reference"));fp32=np.load(record.pop("fp32"));bf16=np.load(record.pop("bf16"))
 record["referenceCtcClassIds"]=decode(reference)
 assert reference.shape==fp32.shape==bf16.shape==(1,record["width"]//8,18710)
 assert all(np.isfinite(value).all() for value in (reference,fp32,bf16))
 for label,value in [("fp32",fp32),("bf16CpuEmulation",bf16)]:
  delta=np.abs(value-reference)
  record[label]={"maxAbsoluteError":float(delta.max()),"meanAbsoluteError":float(delta.mean()),
   "argmaxAgreement":float(np.mean(reference.argmax(-1)==value.argmax(-1))),
   "ctcClassSequenceMatches":decode(reference)==decode(value)}
 record["fp32WithinTolerance"]=bool(np.allclose(reference,fp32,rtol=1e-3,atol=1e-4))
 records.append(record)
json.dump({"cases":records,"fp32Passed":all(r["fp32WithinTolerance"] for r in records),
 "bf16CpuEmulationOnly":True,"amdMicrokernelExecuted":False,"deviceValidated":False},open(sys.argv[2],"w"),indent=2)
assert all(r["fp32WithinTolerance"] for r in records),"FP32 conversion differs beyond rtol=1e-3/atol=1e-4"
'''

def main():
 p=argparse.ArgumentParser(description=__doc__)
 for name in ('source-model','frontend-environment','reference-python','toolchain-dir','runtime-tool','output-dir'):
  p.add_argument('--'+name,type=Path,required=True)
 p.add_argument('--width',type=int,action='append')
 p.add_argument('--text-image',type=Path,help='Optional text fixture; reference Python must provide OpenCV')
 a=p.parse_args();out=a.output_dir.resolve()
 if out.exists():p.error('select a fresh output directory')
 out.mkdir(parents=True)
 tool=Path(__file__).parent.resolve();frontend=a.frontend_environment.resolve()/'bin/python'
 compiler=a.toolchain_dir.resolve()/'tools/iree-compile';runtime=a.runtime_tool.resolve()
 records=[]
 def run(command,log):
  with log.open('w') as stream:subprocess.run(list(map(str,command)),stdout=stream,stderr=subprocess.STDOUT,check=True,timeout=900)
 for width in a.width or [320,384,3200]:
  work=out/str(width);work.mkdir();print(f'CPU comparison width={width}',flush=True)
  run([frontend,tool/'export_iree_model.py','--source-model',a.source_model.resolve(),'--output-dir',work/'model','--width',width],work/'export.log')
  run([sys.executable,tool/'import_iree_model.py','--model-dir',work/'model','--frontend-environment',a.frontend_environment.resolve(),'--output-dir',work/'imported'],work/'import.log')
  source=work/'imported/core-linalg.mlir'
  fp32=work/'fp32.mlir';fp32.write_text(source.read_text().replace('PaddlePaddle Graph in PIR mode','recognition'))
  partitioned=work/'partitioned.mlir'
  run([frontend,tool/'partition_iree_model.py','--input',source,'--output',partitioned,'--report',work/'partition.json'],work/'partition.log')
  run([frontend,'-c', '''import numpy as np,sys
from pathlib import Path
p=Path(sys.argv[1]);w=int(sys.argv[2]);shape=(1,3,48,w)
np.save(p/'zero.npy',np.zeros(shape,np.float32))
np.save(p/'random.npy',np.random.default_rng(20261008).uniform(-1,1,shape).astype(np.float32))
np.save(p/'gradient.npy',np.broadcast_to(np.linspace(-1,1,w,dtype=np.float32),shape).copy())
''',work,width],work/'inputs.log')
  cases=['zero','random','gradient']
  if a.text_image:
   run([a.reference_python.absolute(),'-c', '''import cv2,numpy as np,sys,math
image=cv2.imread(sys.argv[1]);assert image is not None
rows,cols=np.where(cv2.cvtColor(image,cv2.COLOR_BGR2GRAY)<200)
assert len(rows),"text fixture has no foreground"
image=image[max(0,rows.min()-2):rows.max()+3,max(0,cols.min()-2):cols.max()+3]
w=int(sys.argv[3]);content=min(w,math.ceil(48*image.shape[1]/image.shape[0]))
resized=cv2.resize(image,(content,48),interpolation=cv2.INTER_LINEAR)
tensor=np.zeros((1,3,48,w),np.float32)
tensor[0,:,:,:content]=(resized.astype(np.float32)/127.5-1).transpose(2,0,1)
np.save(sys.argv[2],tensor)
''',a.text_image.resolve(),work/'text.npy',width],work/'text-input.log')
   cases.append('text')
  pairs=[value for case in cases for value in (work/f'{case}.npy',work/f'{case}-reference.npy')]
  run([a.reference_python.absolute(),'-c',REFERENCE,a.source_model.resolve(),*pairs],work/'reference.log')
  for label,ir in [('fp32',fp32),('bf16',partitioned)]:
   binary=work/f'{label}.vmfb'
   run([compiler,ir,'--iree-hal-target-device=local','--iree-hal-local-target-device-backends=llvm-cpu','--iree-llvmcpu-target-cpu=generic','-o',binary],work/f'{label}-compile.log')
   for case in cases:
    run([runtime,'--device=local-sync',f'--module={binary}','--function=recognition',f'--input=@{work}/{case}.npy',f'--output=@{work}/{case}-{label}.npy'],work/f'{case}-{label}-run.log')
  for case in cases:records.append({'width':width,'case':case,'reference':str(work/f'{case}-reference.npy'),'fp32':str(work/f'{case}-fp32.npy'),'bf16':str(work/f'{case}-bf16.npy')})
 paths=out/'comparison-inputs.json';paths.write_text(json.dumps(records))
 run([frontend,'-c',COMPARE,paths,out/'cpu-comparison.json'],out/'comparison.log')
 print(out/'cpu-comparison.json',flush=True)

if __name__=='__main__':main()
