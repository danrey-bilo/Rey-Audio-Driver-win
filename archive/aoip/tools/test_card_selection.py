"""One physical USB stream plus an offline saved-card fixture; no second Pi claim."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import time
from test_manager import request, wait_for

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bin', type=Path, required=True)
    parser.add_argument('--profiles', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    binary = args.bin.resolve() / 'ReyAudioService.exe'
    profiles = list(args.profiles.glob('digital.dat.*.dat'))
    assert len(profiles) == 1
    physical_id = profiles[0].name.split('.')[-2]
    fixture_id = 'ffffffffffffffffffffffffffffff01'
    assert fixture_id != physical_id
    out = args.out.resolve(); out.mkdir(exist_ok=False)
    base = out / 'selection.dat'
    data = bytearray(profiles[0].read_bytes())
    assert struct.unpack_from('<II', data) == (0x31535952, 2)
    (out / (base.name + '.' + physical_id + '.dat')).write_bytes(data)
    peer_length = struct.unpack_from('<I', data, 60)[0]
    del data[64:64 + peer_length]
    for offset, value in ((28, 0), (32, 0), (36, 50022), (56, 0), (60, 0)):
        struct.pack_into('<I', data, offset, value)
    (out / (base.name + '.' + fixture_id + '.dat')).write_bytes(data)
    report = {'physical_devices': 1, 'offline_fixture': fixture_id,
        'physical_multi_device_qualified': False, 'windows_endpoints_qualified': False,
        'binary_sha256': hashlib.sha256(binary.read_bytes()).hexdigest(),
        'started_utc': datetime.now(timezone.utc).isoformat()}
    with (out / 'digital.log').open('w') as log:
        process = subprocess.Popen([str(binary), '--console', '--seconds', '30', '--digital-test',
            '--test-settings', str(base)], stdout=log, stderr=subprocess.STDOUT)
        try:
            wait_for(lambda s: len(s['devices']) == 2)
            assert request('SELECT ' + physical_id)['ok']
            active = wait_for(lambda s: s['state'] == 'digital_test' and s['stats']['callbacks'] > 2000)
            generation = active['stats']['generation']; callbacks = active['stats']['callbacks']
            assert request('DEVICE ' + physical_id + ' MIX 0:1 5700 0 0 0 apply')['ok']
            assert request('SELECT ' + fixture_id)['ok']
            assert request('MIX 0:1 4800 1 0 1 apply')['ok']
            time.sleep(.5)
            offline = request('STATUS')
            assert offline['selected_device'] == fixture_id and offline['route'] == 'none'
            assert offline['stats']['callbacks'] == 0 and offline['mixer']['inputs'][1]['gain_cdb'] == -1200
            live = next(card for card in offline['devices'] if card['id'] == physical_id)
            assert live['state'] == 'digital_test' and live['route'] == 'usb' and live['callbacks'] > callbacks
            assert request('SELECT ' + physical_id)['ok']
            returned = request('STATUS')
            assert returned['stats']['generation'] == generation and returned['stats']['callbacks'] > callbacks
            assert returned['mixer']['inputs'][1]['gain_cdb'] == -300 and not returned['mixer']['inputs'][1]['mute']
            assert request('MIX_RESET')['ok']
            report.update(passed=True, before=active, offline_view=offline, after=returned)
        finally:
            process.wait(timeout=35); assert process.returncode == 0
            report['ended_utc'] = datetime.now(timezone.utc).isoformat()
            (out / 'selection-proof.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print('CARD_SELECTION_PASS physical_devices=1 offline_fixtures=1', flush=True)

if __name__ == '__main__': main()
