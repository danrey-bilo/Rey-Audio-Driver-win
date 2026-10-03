"""Isolated x64 ACX/KMDF development build; never sign/install/load a driver."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

def main():
    project=Path(__file__).resolve().parents[1]
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('wdk','sdk','msvc-include','llvm','out'): parser.add_argument('--'+name,type=Path,required=True)
    args=parser.parse_args()
    args.out.mkdir(parents=True,exist_ok=True)
    includes=[args.wdk/'Include/10.0.26100.0/km',args.sdk/'Include/10.0.26100.0/shared',args.sdk/'Include/10.0.26100.0/ucrt',args.sdk/'Include/10.0.26100.0/um',
        args.wdk/'Include/wdf/kmdf/1.31',args.wdk/'Include/10.0.26100.0/km/acx/km/1.1',args.msvc_include]
    libraries=[args.wdk/'Lib/10.0.26100.0/km/x64',args.wdk/'Lib/wdf/kmdf/x64/1.31',args.wdk/'Lib/10.0.26100.0/km/x64/acx/km/1.1']
    objects=[]
    logs=[]
    for source in sorted((project/'drivers/Acx').glob('*.c')):
        obj=args.out/(source.stem+'.obj')
        command=[str(args.llvm/'clang-22.exe'),'--driver-mode=cl','--target=x86_64-pc-windows-msvc','/nologo','/c','/TC','/O2','/Zl','/GS','/W3',
            '/D_AMD64_','/D_WIN64','/DWINNT','/DNTDDI_VERSION=0x0A00000B','/DKMDF_VERSION_MAJOR=1','/DKMDF_VERSION_MINOR=31','/DACX_VERSION_MAJOR=1','/DACX_VERSION_MINOR=1',
            '/clang:-fno-vectorize','/clang:-fno-slp-vectorize','/clang:-mno-sse','/clang:-mno-sse2','/clang:-mno-mmx','/clang:-Wno-nonportable-include-path','/clang:-Wno-microsoft-enum-forward-reference','/clang:-Wno-switch','/clang:-Wno-ignored-attributes','/clang:-Wno-pragma-pack','/clang:-Wno-unknown-pragmas','/clang:-Wno-unused-but-set-variable']
        command+=[arg for path in includes for arg in ('-imsvc',str(path))]+[str(source),'/Fo'+str(obj)]
        result=subprocess.run(command,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
        logs.append({'source':str(source),'command':command,'returncode':result.returncode,'output':result.stdout})
        if result.returncode:
            (args.out/'build.json').write_text(json.dumps(logs,indent=2)); print(result.stdout[-15000:]); return result.returncode
        objects.append(obj)
    command=[str(args.llvm/'ld.lld.exe'),'-flavor','link','/nologo','/driver','/subsystem:native,10.00','/machine:x64','/entry:FxDriverEntry','/nodefaultlib',
        '/dynamicbase','/nxcompat','/opt:ref','/opt:icf','/integritycheck','/release','/Brepro','/out:'+str(args.out/'ReyAudioAcx.sys')]
    command += [str(obj) for obj in objects]+['/libpath:'+str(path) for path in libraries]+['ntoskrnl.lib','hal.lib','wmilib.lib','ksguid.lib','wdmguid.lib','wdfdriverentry.lib','wdfldr.lib','acxstub.lib','BufferOverflowFastFailK.lib']
    result=subprocess.run(command,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    logs.append({'stage':'link','command':command,'returncode':result.returncode,'output':result.stdout})
    (args.out/'build.json').write_text(json.dumps(logs,indent=2))
    if result.returncode: print(result.stdout[-15000:]); return result.returncode
    shutil.copyfile(project/'drivers/Acx/ReyAudioAcx.inf',args.out/'ReyAudioAcx.inf')
    command=[str(args.wdk/'tools/10.0.26100.0/x64/infverif.exe'),'/u',str(args.out/'ReyAudioAcx.inf')]
    result=subprocess.run(command,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    (args.out/'infverif.log').write_text(result.stdout)
    print(result.stdout[-12000:],flush=True)
    if result.returncode: return result.returncode
    command=[str(args.wdk/'bin/10.0.26100.0/x86/Inf2Cat.exe'),'/driver:'+str(args.out.resolve()),'/os:10_NI_X64,10_GE_X64','/uselocaltime']
    result=subprocess.run(command,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    (args.out/'inf2cat.log').write_text(result.stdout)
    print(result.stdout[-12000:],flush=True)
    digest=hashlib.sha256((args.out/'ReyAudioAcx.sys').read_bytes()).hexdigest()
    (args.out/'manifest.json').write_text(json.dumps({'architecture':'x64','kmdf':'1.31','acx':'1.1','compiler':'Clang 22 MSVC ABI','sha256':digest,
        'signed':False,'installed':False,'infverif_exit_code':0,'inf2cat_exit_code':result.returncode,
        'source_sha256':{p.relative_to(project).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted((project/'drivers/Acx').glob('*')) if p.suffix in ('.c','.h','.inf')},
        'contract_sha256':hashlib.sha256((project/'include/piaoip/bridge.h').read_bytes()).hexdigest()},indent=2))
    print('BUILT development ReyAudioAcx.sys sha256='+digest,flush=True)
    return result.returncode

if __name__=='__main__': raise SystemExit(main())
