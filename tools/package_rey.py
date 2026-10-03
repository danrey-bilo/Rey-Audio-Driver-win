"""Publish checked Rey Audio MSI/EXE into a new local distribution directory."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import shutil

VERSION = '2.7.0-preview.1'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def read(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def require(condition, message):
    if not condition:
        raise ValueError(message)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('setup', 'bin', 'driver', 'evidence', 'inspection', 'signing', 'tamper', 'out'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    project = Path(__file__).resolve().parents[1]
    driver = read(args.driver / 'manifest.json')
    require(not driver['signed'] and not driver['installed'], 'Expected unsigned source driver.')
    require(driver['infverif_exit_code'] == 0 and driver['inf2cat_exit_code'] == 0, 'INF checks failed.')
    require(sha(args.driver / 'ReyAudioAcx.sys') == driver['sha256'], 'SYS hash differs.')
    for relative, digest in driver['source_sha256'].items():
        require(sha(project / relative) == digest, 'Driver source differs: ' + relative)
    for name, digest in driver['contract_sha256'].items():
        require(sha(project / 'include/piaoip' / name) == digest, 'Driver contract differs: ' + name)
    evidence = read(args.evidence)
    require(evidence['passed'] and evidence['binary_sha256'] == sha(args.bin / 'ReyAudioService.exe'), 'Service evidence does not match.')
    inspection = read(args.inspection)
    signing, tamper = read(args.signing), read(args.tamper)
    require(inspection['passed'] and inspection['embedded_msi_verified'] and inspection['payload_count'] == 7, 'Setup inspection failed.')
    require(inspection['launch_conditions_verified'], 'Launch condition checks are missing.')
    require(not inspection['ini_or_install_scripts'], 'Unexpected configuration payload.')
    require(signing['catalog_members_verified'] and signing['private_key_deleted'] and not signing['private_key_exported'], 'Local signing proof failed.')
    require(signing['source_sys_sha256'] == driver['sha256'], 'Signing proof uses another driver.')
    require(tamper['passed'] and tamper['embedded_signature_tamper_rejected'] and tamper['catalog_member_tamper_rejected'], 'Tamper proof failed.')
    payload = {
        'ServiceExe': args.bin / 'ReyAudioService.exe',
        'ControlExe': args.bin / 'ReyAudioControl.exe',
        'ProbeExe': args.bin / 'ReyAudioProbe.exe',
        'SetupHelper': args.setup / 'ReyAudioSetup.Helper.exe',
        'DriverSysFile': args.driver / 'ReyAudioAcx.sys',
        'DriverInfFile': args.driver / 'ReyAudioAcx.inf',
        'DriverCatFile': args.driver / 'reyaudioacx.cat',
    }
    for name, path in payload.items():
        require(sha(path) == inspection['cab_payload_sha256'][name], 'Inspected payload differs: ' + name)
    assets = [args.setup / ('Rey-Audio-Driver-' + VERSION + '-x64.msi'),
              args.setup / ('Rey-Audio-Setup-' + VERSION + '-x64.exe')]
    require(sha(assets[0]) == inspection['msi_sha256'], 'MSI changed since inspection.')
    require(sha(assets[1]) == inspection['exe_sha256'], 'EXE changed since inspection.')
    args.out.mkdir(parents=True, exist_ok=False)
    digests = {path.name: sha(path) for path in assets}
    for source in assets:
        shutil.copyfile(source, args.out / source.name)
        require(sha(args.out / source.name) == digests[source.name], 'Copy hash differs.')
    checksums = args.out / ('SHA256SUMS-v' + VERSION + '.txt')
    checksums.write_text(''.join(digest + '  ' + name + '\n' for name, digest in sorted(digests.items())), encoding='ascii')
    manifest = {
        'version': VERSION, 'created_utc': datetime.now(timezone.utc).isoformat(),
        'assets_sha256': digests, 'cab_payload_sha256': inspection['cab_payload_sha256'],
        'free_local_signing_verified': True, 'test_mode_required': True,
        'kernel_loaded': False, 'elevated_install_qualified': False,
        'wasapi_qualified': False, 'physical_audio_qualified': False,
        'physical_devices_tested': 1, 'configured_device_limit': 10,
        'simultaneous_physical_devices_qualified': False,
        'stereo_pairs_per_direction': 4, 'endpoints_per_8x8_device': 10,
        'service_checks': len(evidence['cases']),
    }
    (args.out / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    print('PACKAGED ' + str(args.out.resolve()), flush=True)
    for name, digest in digests.items():
        print(name + ' SHA256=' + digest, flush=True)


if __name__ == '__main__':
    main()
