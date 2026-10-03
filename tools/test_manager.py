"""Finite real-device checks for Rey Audio Service; does not install a driver.

Runs isolated console sessions for at most 90 seconds each. The USB device must
be our High-Speed laboratory transport; no physical ADC/DAC result is claimed.
"""
import argparse
import ctypes
from ctypes import wintypes
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import subprocess
import time

kernel = ctypes.WinDLL('kernel32', use_last_error=True)
kernel.CallNamedPipeW.argtypes = [wintypes.LPCWSTR, wintypes.LPVOID, wintypes.DWORD,
    wintypes.LPVOID, wintypes.DWORD, ctypes.POINTER(wintypes.DWORD), wintypes.DWORD]
kernel.CallNamedPipeW.restype = wintypes.BOOL


def request(command):
    data = command.encode('ascii')
    output = ctypes.create_string_buffer(32768)
    read = wintypes.DWORD()
    if not kernel.CallNamedPipeW(r'\\.\pipe\ReyAudio.Control.v1', data, len(data), output,
                                len(output), ctypes.byref(read), 2000):
        raise OSError(ctypes.get_last_error(), 'Rey Audio control request failed')
    return json.loads(output.raw[:read.value])


def wait_for(predicate, seconds=8):
    end = time.monotonic() + seconds
    last = None
    while time.monotonic() < end:
        try:
            last = request('STATUS')
            if predicate(last):
                return last
        except OSError:
            pass
        time.sleep(.15)
    raise AssertionError('Expected state not reached: ' + json.dumps(last))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bin', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    binary = args.bin.resolve() / 'ReyAudioService.exe'
    report = {'started_utc': datetime.now(timezone.utc).isoformat(),
              'binary_sha256': hashlib.sha256(binary.read_bytes()).hexdigest(),
              'physical_audio_qualified': False, 'windows_endpoints_qualified': False, 'cases': []}
    try:
        for digital in (False, True):
            config = args.out.resolve() / ('digital.ini' if digital else 'production.ini')
            duration = 55 if digital else 20
            command = [str(binary), '--console', '--seconds', str(duration), '--config', str(config)]
            if digital:
                command += ['--digital-test']
            with (args.out / ('digital.log' if digital else 'production.log')).open('w') as log:
                process = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT)
                try:
                    status = wait_for(lambda d: d['usb']['present'] == 1 and d['usb']['identity'])
                    report['cases'].append({'name': 'USB auto discovery ' + str(digital), 'status': status})
                    if not digital:
                        status = wait_for(lambda d: d['state'] == 'driver_unavailable')
                        assert not status['driver_present'] and status['stats']['callbacks'] == 0
                        report['cases'].append({'name': 'missing driver is explicit', 'status': status})
                        subprocess.run([str(args.bin.resolve() / 'ReyAudioControl.exe'), '--render-test',
                            str(args.out.resolve() / 'ui')], check=True, timeout=20)
                        layout = json.loads((args.out / 'ui/layout-check.json').read_text())
                        assert layout['service_available'] and layout['usb_apply_enabled'] and layout['usb_select_enabled']
                    else:
                        status = wait_for(lambda d: d['state'] == 'digital_test' and d['stats']['callbacks'] > 1000)
                        assert status['usb']['present'] == 1  # enumeration must survive exclusive streaming
                        baseline = status['usb'].copy()
                        assert request('LAN 192.168.1.2 50021 128 2048 32 0')['ok']
                        after = request('STATUS')
                        assert after['usb'] == baseline and after['route'] == 'usb'
                        count = after['stats']['callbacks']
                        time.sleep(.5)
                        after = request('STATUS')
                        assert after['stats']['callbacks'] > count  # inactive LAN edit must not restart USB
                        report['cases'].append({'name': 'separate LAN profile without USB restart', 'status': after})
                        for invalid in ('USB 192000 17 3 64 0 1', 'USB 192000 32 0 64 0 1',
                                        'USB -1 32 3 64 0 1', 'USE shell', 'LAN 127.0.0.1 0 64 256 32 0',
                                        'LAN_CONNECT extra', 'USB 192000 32 3 64 0 1 trailing'):
                            before = request('STATUS')
                            reply = request(invalid)
                            assert not reply['ok'] and reply['usb'] == before['usb'] and reply['lan'] == before['lan']
                        report['cases'].append({'name': 'malformed requests leave settings unchanged', 'passed': True})
                        for rate, bits in ((44100, 16), (96000, 24), (192000, 32)):
                            assert request(f'USB {rate} {bits} 3 64 0 1')['ok']
                            after = wait_for(lambda d: d['state'] == 'digital_test' and
                                d['active']['rate'] == rate and d['active']['bits'] == bits and
                                d['stats']['callbacks'] > 1000)
                            assert after['stats']['missing_frames'] == 0
                            report['cases'].append({'name': f'USB profile restart {rate}/{bits}', 'status': after})
                        assert request('USB 192000 32 3 64 0 0')['ok']
                        wait_for(lambda d: d['route'] == 'none' and d['state'] == 'idle')
                        assert request('USE usb')['ok']
                        after = wait_for(lambda d: d['state'] == 'digital_test' and d['stats']['callbacks'] > 1000)
                        report['cases'].append({'name': 'USB pause and reconnect', 'status': after})
                finally:
                    # CTRL_BREAK is unavailable without a separate console; a finite
                    # session ends itself. Test stop via its bounded timer.
                    process.wait(timeout=duration + 5)
                    assert process.returncode == 0
        report['passed'] = True
    finally:
        report['ended_utc'] = datetime.now(timezone.utc).isoformat()
        (args.out / 'manager-proof.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print('REY_MANAGER_PASS cases=' + str(len(report['cases'])), flush=True)


if __name__ == '__main__':
    main()
