"""Package the measured Windows service and unsigned ACX driver; never install."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import urllib.parse
import zipfile

def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('driver', 'bin', 'qualification', 'out'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    project = Path(__file__).resolve().parents[1]
    driver = json.loads((args.driver / 'manifest.json').read_text(encoding='utf-8'))
    if driver['signed'] or driver['installed'] or driver['infverif_exit_code'] or driver['inf2cat_exit_code']:
        raise RuntimeError('Unexpected driver build/check manifest')
    if sha256(args.driver / 'PiAoipAcx.sys') != driver['sha256']:
        raise RuntimeError('Driver binary changed')
    for relative, digest in driver['source_sha256'].items():
        if sha256(project / relative) != digest:
            raise RuntimeError('Driver source changed: ' + relative)
    if sha256(project / 'include/piaoip/bridge.h') != driver['contract_sha256']:
        raise RuntimeError('Bridge contract changed')
    suite = json.loads(args.qualification.read_text(encoding='utf-8'))
    if 'ended_utc' not in suite or not suite['restored'] or len(suite['normal']) != 1:
        raise RuntimeError('Completed rebuild measurement and restoration required')
    case = suite['normal'][0]
    if sha256(args.bin / 'PiAoipService.exe') != case['exe_sha256']:
        raise RuntimeError('Service differs from measured binary')
    entries = {}
    for name in ('PiAoipService.exe', 'PiAoipWasapiProbe.exe'):
        entries['bin/' + name] = (args.bin / name).read_bytes()
    for name in ('PiAoipAcx.sys', 'PiAoipAcx.inf', 'piaoipacx.cat', 'manifest.json', 'infverif.log', 'inf2cat.log'):
        entries['driver/' + name] = (args.driver / name).read_bytes()
        if name.endswith('.log'):
            text = entries['driver/' + name].decode('utf-8-sig')
            for prefix in (str(project.parents[1]), project.parents[1].as_posix()):
                text = text.replace(prefix, 'workspace')
            entries['driver/' + name] = text.encode('utf-8')
    entries['service.example.ini'] = (project / 'config/service.example.ini').read_bytes()
    entries['LICENSE'] = (project / 'LICENSE').read_bytes()
    for path in (project / 'docs').glob('*.md'):
        if path.name.startswith(('ACX-SERVICE', 'TRANSPORT-2.5', 'VALIDATION', 'RELEASE-2.5.0', 'BUILD', 'INSTALL', 'ARCHITECTURE', 'API', 'BUFFER-GUIDE')):
            entries['docs/' + path.name] = path.read_bytes()
    for name, data in list(entries.items()):
        if not name.endswith('.md') or not name.startswith('docs/'):
            continue
        def portable_link(match):
            target = match.group(2)
            if re.match(r'^[a-zA-Z][a-zA-Z0-9+.-]*:|^#', target):
                return match.group(0)
            relative, _, anchor = target.partition('#')
            source = (project / name).parent / relative
            source = source.resolve()
            if source.is_relative_to(project) and source.is_file():
                payload = source.relative_to(project).as_posix()
                if payload not in entries:
                    url = 'https://github.com/danrey-bilo/Win11-asio-AoIP/blob/v2.5.0/' + payload
                    if anchor:
                        url += '#' + anchor
                    return '[' + match.group(1) + '](' + urllib.parse.quote(url, safe=':/#') + ')'
            return match.group(0)
        entries[name] = re.sub(r'\[([^\]]+)\]\(([^)]+)\)', portable_link, data.decode('utf-8-sig')).encode('utf-8')
    entries['README.md'] = (
        '# PiAoIP 2.5.0 Windows service / ACX development package\n\n'
        'Read docs/ACX-SERVICE.md or docs/ACX-SERVICE.ru.md. Use an absolute --config path. '
        'Digital --echo is a test fixture.\n\n'
        'SYS/CAT are unsigned; the kernel driver has not been installed or tested with WASAPI. '
        'This archive has no automatic installer or signing/boot changes. ASIO has its own MSI. '
        'Physical ADC/DAC and several Pi devices remain unqualified.\n'
    ).encode('utf-8')
    manifest = {'version': '2.5.0', 'created_utc': datetime.now(timezone.utc).isoformat(),
                'signed': False, 'kernel_installed': False, 'kernel_streaming_qualified': False,
                'physical_audio_qualified': False, 'single_pi': True,
                'lan_rebuild_qualification_clean': case['qualification_clean'],
                'service_sha256': case['exe_sha256'], 'pi_sha256': case['pi_after']['binary_sha256'],
                'driver_sha256': driver['sha256'], 'rtt': case['rtt'],
                'payload_sha256': {n: hashlib.sha256(d).hexdigest() for n, d in entries.items()}}
    entries['manifest.json'] = json.dumps(manifest, indent=2).encode('utf-8')
    entries['SHA256SUMS.txt'] = ''.join(hashlib.sha256(data).hexdigest() + '  ' + name + '\n'
        for name, data in sorted(entries.items())).encode('ascii')
    args.out.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(args.out, 'x', compression=zipfile.ZIP_DEFLATED) as archive:
        for name, data in sorted(entries.items()):
            info = zipfile.ZipInfo(name, (2026, 10, 3, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o100644 << 16
            archive.writestr(info, data)
    print('Packaged ' + str(args.out) + ' SHA-256=' + sha256(args.out))

if __name__ == '__main__':
    main()
