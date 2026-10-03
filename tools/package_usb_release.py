"""Package a tested USB installer and committed source without external SDK files.

No installation, Git mutation or publication is performed. Pass an SDK license
notice separately; its interface headers and binaries are never copied.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import zipfile


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--setup', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--asio-notice', type=Path, required=True)
    parser.add_argument('--version', default='2.8.1-preview.1')
    parser.add_argument('--ref', default='HEAD')
    args = parser.parse_args()
    if not args.version or any(c not in '0123456789abcdefghijklmnopqrstuvwxyz.-' for c in args.version):
        parser.error('Version must contain only lowercase letters, numbers, dots and hyphens')
    project = Path(__file__).resolve().parents[1]
    setup, output = args.setup.resolve(), args.out.resolve()
    source_setup = setup / 'Rey-Audio-USB-ASIO-Setup-x64.exe'
    manifest = json.loads((setup / 'SHA256.json').read_text(encoding='utf-8-sig'))
    if args.version.split('-', 1)[0] != manifest.get('version', '').split('-', 1)[0]:
        raise RuntimeError('Release version does not match the installer manifest')
    expected = {'ReyAudioService.exe', 'ReyAudioAsio.dll', 'ReyAudioControl.exe', 'ReyAsioProbe.exe'}
    if (set(manifest['files']) != expected or manifest.get('device_limit') != 1 or
            manifest.get('transport') != 'USB' or manifest.get('kernel_driver') is not False or
            digest(source_setup) != manifest['setup_sha256']):
        raise RuntimeError('The input must be an intact one-device USB-ASIO setup')
    files = subprocess.check_output(['git', 'ls-tree', '-r', '--name-only', args.ref],
                                    cwd=project, text=True).splitlines()
    for name in files:
        if name.startswith(('external/', 'third_party/asio-sdk/')) or name.endswith(('.pfx', '.key')):
            raise RuntimeError('External SDK or private-key file in source tree: ' + name)
    output.mkdir(parents=True, exist_ok=True)
    stem = 'Rey-Audio-USB-ASIO-' + args.version
    executable = output / (stem + '-x64.exe')
    shutil.copyfile(source_setup, executable)
    shutil.copyfile(setup / 'SHA256.json', output / 'SHA256.json')
    source_zip = output / (stem + '-source.zip')
    subprocess.run(['git', 'archive', '--format=zip', '--prefix=' + stem + '-source/',
                    '--output=' + str(source_zip), args.ref], cwd=project, check=True)
    package_zip = output / (stem + '-x64.zip')
    prefix = stem + '-x64/'
    quick_start = ('Run Rey-Audio-USB-ASIO-Setup-x64.exe and accept UAC.\n'
                   'Close Ableton completely before updating. Connect one configured Pi5-AUSB board.\n'
                   'Select ASIO / Rey Audio USB ASIO in Ableton. Read docs/USB-ASIO.md.\n'
                   '96/192 kHz measurements and manual profiles: docs/USB-LATENCY-2.8.1.md.\n'
                   'Each audio test lasts at most 175 seconds. Physical ADC/DAC latency is unmeasured.\n\n'
                   'Запустите Rey-Audio-USB-ASIO-Setup-x64.exe и подтвердите UAC.\n'
                   'Перед обновлением полностью закройте Ableton. Подключите одну настроенную Pi5-AUSB.\n'
                   'В Ableton выберите ASIO / Rey Audio USB ASIO. Инструкция: docs/USB-ASIO.ru.md.\n'
                   'Профили и измерения 96/192 кГц: docs/USB-LATENCY-2.8.1.ru.md.\n'
                   'Аудиопрогоны до 175 секунд. Физическая задержка ADC/DAC не измерена.\n')
    with zipfile.ZipFile(package_zip, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        archive.write(source_setup, prefix + source_setup.name)
        archive.write(setup / 'SHA256.json', prefix + 'SHA256.json')
        archive.write(args.asio_notice, prefix + 'LICENSE-ASIO-SDK.txt')
        archive.writestr(prefix + 'START-HERE.txt', quick_start)
        for name in files:
            if name in ('README.md', 'README.ru.md', 'LICENSE') or name.startswith('docs/'):
                data = subprocess.check_output(['git', 'show', args.ref + ':' + name], cwd=project)
                archive.writestr(prefix + name, data)
    for path in (source_zip, package_zip):
        with zipfile.ZipFile(path) as archive:
            damaged = archive.testzip()
            if damaged:
                raise RuntimeError('Damaged ZIP member: ' + damaged)
    assets = [executable, package_zip, source_zip, output / 'SHA256.json']
    checksums = output / ('SHA256SUMS-v2.8.0-usb-preview.1.txt' if args.version == '2.8.0-preview.1'
                         else 'SHA256SUMS-' + args.version + '.txt')
    checksums.write_text(''.join(digest(path) + '  ' + path.name + '\n' for path in assets), encoding='ascii')
    assets.append(checksums)
    print(json.dumps({'ref': args.ref, 'device_limit': 1, 'transport': 'USB',
        'assets': [{'name': p.name, 'size': p.stat().st_size, 'sha256': digest(p)} for p in assets]}, indent=2))


if __name__ == '__main__':
    main()
