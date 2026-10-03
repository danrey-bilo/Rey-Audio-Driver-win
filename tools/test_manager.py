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
    parser = argparse.ArgumentParser(description='Finite checks of the installed single-device USB service')
    parser.add_argument('--bin', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    report = {'physical_audio': False, 'device_limit': 1, 'cases': [],
              'binary_sha256': hashlib.sha256((args.bin / 'ReyAudioService.exe').read_bytes()).hexdigest()}
    original = None
    start = time.monotonic()
    def usb(profile):
        return request('USB ' + ' '.join(map(str, (profile['rate'], profile['bits'], profile['depth'],
            profile['block'], profile['guard'], int(profile['automatic'])))))
    try:
        before = request('STATUS')
        if before.get('device_limit') != 1 or not before.get('asio_only') or before['asio']['connected']:
            raise RuntimeError('One idle USB-only ASIO service is required; close the DAW')
        original = before['usb'].copy()
        report['before'] = before
        for command in ('SCAN', 'LAN_CONNECT', 'LAN_DISCONNECT', 'USE lan', 'USE usb',
                        'SELECT ' + before['identity'], 'FORGET ' + before['identity'],
                        'DEVICE ' + before['identity'] + ' MIX_RESET',
                        'USB 192000 17 3 64 0 1', 'USB -1 32 3 64 0 1', 'MIX 0:8 6000 0 0 0 apply'):
            reply = request(command)
            if reply['ok'] or reply['usb'] != before['usb'] or reply['mixer'] != before['mixer']:
                raise RuntimeError('Rejected command changed settings: ' + command)
            report['cases'].append({'command': command, 'rejected': True})
        if 'lan' in before or 'devices' in before or 'selected_device' in before:
            raise RuntimeError('Retired transport/catalog remains in the control API')
        if not usb({**original, 'automatic': False})['ok']:
            raise RuntimeError('Pause rejected')
        idle = wait_for(lambda s: s['state'] == 'idle' and not s['asio']['ready'] and s['active']['rate'] == 0)
        if idle['usb']['present'] != 1:
            raise RuntimeError('Paused device presence was lost')
        report['paused'] = idle
        if not usb(original)['ok']:
            raise RuntimeError('Resume rejected')
        resumed = wait_for(lambda s: s['state'] == 'streaming' and s['asio']['ready'] and s['stats']['callbacks'] > 500)
        if resumed['identity'] != before['identity'] or resumed['stats']['generation'] <= before['stats']['generation']:
            raise RuntimeError('Resume identity/generation mismatch')
        report['resumed'] = resumed
        report['okay'] = True
    except (OSError, RuntimeError, AssertionError) as error:
        report['okay'] = False
        report['error'] = str(error)
    finally:
        if original is not None:
            try:
                usb(original)
                report['restored'] = wait_for(lambda s: s['usb'] == original and s['state'] == 'streaming' and s['asio']['ready'])
            except (OSError, AssertionError) as error:
                report['okay'] = False
                report['restore_error'] = str(error)
        report['duration_s'] = round(time.monotonic() - start, 3)
        (args.out / 'results.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print('SINGLE_USB_SERVICE', report.get('okay'), report['duration_s'], flush=True)
    return 0 if report.get('okay') else 1


if __name__ == '__main__':
    raise SystemExit(main())
