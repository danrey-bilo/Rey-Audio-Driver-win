"""Package Rey Audio service/panel and unsigned ACX development driver."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import re
from pathlib import Path
import posixpath
import zipfile

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('bin','driver','evidence','out'):
        parser.add_argument('--'+name,type=Path,required=True)
    args=parser.parse_args()
    project=Path(__file__).resolve().parents[1]
    driver=json.loads((args.driver/'manifest.json').read_text(encoding='utf-8'))
    assert not driver['signed'] and not driver['installed']
    assert driver['infverif_exit_code']==0 and driver['inf2cat_exit_code']==0
    assert sha(args.driver/'ReyAudioAcx.sys')==driver['sha256']
    for relative,digest in driver['source_sha256'].items():
        assert sha(project/relative)==digest,relative
    evidence=json.loads(args.evidence.read_text(encoding='utf-8'))
    assert evidence['passed'] and evidence['binary_sha256']==sha(args.bin/'ReyAudioService.exe')
    entries={}
    for name in ('ReyAudioService.exe','ReyAudioControl.exe','ReyAudioProbe.exe'):
        entries['bin/'+name]=(args.bin/name).read_bytes()
    for name in ('ReyAudioAcx.sys','ReyAudioAcx.inf','reyaudioacx.cat','manifest.json','infverif.log','inf2cat.log'):
        data=(args.driver/name).read_bytes()
        if name.endswith('.log'):
            text=data.decode('utf-8-sig').replace(str(project),'workspace').replace(project.as_posix(),'workspace')
            data=text.encode('utf-8')
        entries['driver/'+name]=data
    entries['service.example.ini']=(project/'config/rey-service.example.ini').read_bytes()
    entries['LICENSE']=(project/'LICENSE').read_bytes()
    for name in ('install_rey.ps1',):
        entries['tools/'+name]=(project/'tools'/name).read_bytes()
    for name in ('README.md','README.ru.md','docs/REY-AUDIO-2.6.md','docs/REY-AUDIO-2.6.ru.md','docs/USB-TRANSPORT.md'):
        entries[name]=(project/name).read_bytes()
    for folder in ('assets','evidence'):
        for path in sorted((project/'docs'/folder).glob('rey-*')):
            if path.is_file(): entries[path.relative_to(project).as_posix()]=path.read_bytes()
    for name,data in list(entries.items()):
        if not name.endswith('.md'):continue
        def portable_link(match):
            target=match.group(2)
            if re.match(r'^[a-zA-Z][a-zA-Z0-9+.-]*:|^#',target):return match.group(0)
            relative,separator,anchor=target.partition('#')
            resolved=posixpath.normpath(posixpath.join(posixpath.dirname(name),relative))
            if resolved in entries:return match.group(0)
            return '['+match.group(1)+'](https://github.com/danrey-bilo/Rey-Audio-Driver-win/blob/v2.6.0-preview.1/'+resolved+('#'+anchor if separator else '')+')'
        entries[name]=re.sub(r'\[([^\]]*)\]\(([^)]+)\)',portable_link,data.decode('utf-8-sig')).encode('utf-8')
    manifest={'version':'2.6.0-preview.1','created_utc':datetime.now(timezone.utc).isoformat(),
              'kernel_signed':False,'kernel_installed':False,'wasapi_qualified':False,
              'physical_audio_qualified':False,'single_pi':True,
              'service_sha256':sha(args.bin/'ReyAudioService.exe'),
              'panel_sha256':sha(args.bin/'ReyAudioControl.exe'),
              'driver_sha256':driver['sha256'],
              'payload_sha256':{name:hashlib.sha256(data).hexdigest() for name,data in entries.items()}}
    entries['manifest.json']=(json.dumps(manifest,indent=2)+'\n').encode('utf-8')
    entries['SHA256SUMS.txt']=''.join(hashlib.sha256(data).hexdigest()+'  '+name+'\n' for name,data in sorted(entries.items())).encode('ascii')
    args.out.parent.mkdir(parents=True,exist_ok=True)
    package=args.out.with_suffix('')
    package.mkdir(exist_ok=False)
    for name,data in entries.items():
        path=package/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(data)
    with zipfile.ZipFile(args.out,'x',compression=zipfile.ZIP_DEFLATED) as archive:
        for name,data in sorted(entries.items()): archive.writestr(name,data)
    print('PACKAGED '+args.out.name+' SHA256='+sha(args.out),flush=True)

if __name__=='__main__':main()
