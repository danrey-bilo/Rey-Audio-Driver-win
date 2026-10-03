"""Finite USB-ASIO PCM/RTT checks on one real Pi digital-loopback backend.

No service installation, kernel driver or boot change. Soak host <=295 seconds;
each isolated console service exits within 300 seconds. Not a physical audio
test and not an Ableton-host performance result.
"""
import argparse
import ctypes
from ctypes import wintypes
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import runpy
import subprocess
import time


def cpu_seconds(process):
    api = ctypes.WinDLL('kernel32', use_last_error=True)
    api.GetProcessTimes.argtypes = [wintypes.HANDLE] + [ctypes.POINTER(wintypes.FILETIME)] * 4
    values = [wintypes.FILETIME() for _ in range(4)]
    if not api.GetProcessTimes(wintypes.HANDLE(int(process._handle)), *[ctypes.byref(v) for v in values]):
        raise ctypes.WinError(ctypes.get_last_error())
    return sum(((v.dwHighDateTime << 32) | v.dwLowDateTime) for v in values[2:]) / 10000000


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bin', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--mode', choices=['smoke', 'matrix', 'soak'], default='smoke')
    parser.add_argument('--seconds', type=int, default=295)
    parser.add_argument('--block', type=int, default=64)
    parser.add_argument('--lead', type=int, default=3)
    args = parser.parse_args()
    if not 1 <= args.seconds <= 295 or args.block not in (16, 32, 64, 128, 256) or args.lead not in (1, 2, 3, 4):
        parser.error('Invalid finite duration or manual buffer setting')
    binary, output = args.bin.resolve(), args.out.resolve()
    output.mkdir(parents=True, exist_ok=True)
    root = Path(__file__).resolve().parents[1]
    request = runpy.run_path(str(root / 'tools/test_manager.py'))['request']
    cases = [(192000, 32, args.block, args.lead, args.seconds)]
    if args.mode == 'smoke':
        cases = [(192000, 32, 64, 3, 5)]
    elif args.mode == 'matrix':
        cases = [(rate, bits, 16 if rate <= 48000 else 32 if rate <= 96000 else 64, 3, 5)
            for rate in (44100, 48000, 88200, 96000, 176400, 192000) for bits in (16, 24, 32)]
    duration = 300 if args.mode == 'soak' else 150 if args.mode == 'matrix' else 40
    report = {'started_utc': datetime.now(timezone.utc).isoformat(), 'mode': args.mode,
              'physical_audio': False, 'actual_ableton_host': False, 'cases': [],
              'service_sha256': hashlib.sha256((binary / 'ReyAudioService.exe').read_bytes()).hexdigest(),
              'driver_sha256': hashlib.sha256((binary / 'ReyAudioAsio.dll').read_bytes()).hexdigest()}
    start = time.monotonic()
    with (output / 'service.log').open('w', encoding='utf-8') as log:
        service = subprocess.Popen([str(binary / 'ReyAudioService.exe'), '--console', '--asio-only',
            '--seconds', str(duration), '--test-settings', str(output / f'profile-{time.time_ns()}.dat')],
            cwd=binary, stdout=log, stderr=subprocess.STDOUT, creationflags=subprocess.CREATE_NO_WINDOW)
        host = None
        def save():
            (output / 'results.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')

        def ready(rate, bits):
            for attempt in range(100):
                if service.poll() is not None:
                    raise RuntimeError('Service exited before the profile was ready')
                try:
                    status = request('STATUS')
                    if (status['state'] == 'streaming' and status['active']['rate'] == rate and
                            status['active']['bits'] == bits and status['stats']['callbacks'] > 500):
                        return status
                except OSError:
                    pass
                time.sleep(.1)
            raise RuntimeError('ASIO USB session did not become ready')

        try:
            report['before'] = ready(192000, 32)
            for rate, bits, block, lead, seconds in cases:
                if not request(f'USB {rate} {bits} 3 64 0 1')['ok']:
                    raise RuntimeError('USB profile rejected')
                before = ready(rate, bits)
                environment = dict(__import__('os').environ)
                environment['REY_ASIO_LEAD_BLOCKS'] = str(lead)
                host = subprocess.Popen([str(binary / 'ReyAsioProbe.exe'), '--dll', str(binary / 'ReyAudioAsio.dll'),
                    '--seconds', str(seconds), '--block', str(block)], stdout=subprocess.PIPE,
                    stderr=subprocess.STDOUT, text=True, env=environment, creationflags=subprocess.CREATE_NO_WINDOW)
                while True:
                    try:
                        text, _ = host.communicate(timeout=min(60, seconds + 20))
                        break
                    except subprocess.TimeoutExpired:
                        if time.monotonic() - start > duration + 5:
                            host.terminate(); host.wait(timeout=5)
                            raise RuntimeError('Finite ASIO host deadline exceeded')
                        live = request('STATUS')
                        report.setdefault('live', []).append({'elapsed_s': round(time.monotonic()-start, 3),
                            'stats': live['stats'], 'asio': live.get('asio', {})})
                        print('USB_ASIO_LIVE', round(time.monotonic()-start), live.get('asio', {}), flush=True)
                        save()
                after = request('STATUS')
                metrics = {}
                for line in text.splitlines():
                    if line.startswith('ASIO_RESULT '):
                        metrics = dict(field.split('=', 1) for field in line.split()[1:])
                case = {'rate': rate, 'bits': bits, 'block': block, 'lead_blocks': lead, 'seconds': seconds,
                        'exit_code': host.returncode, 'output': text, 'metrics': metrics, 'service_after': after,
                        'session_continues': (after['state'] == 'streaming' and
                            before['stats']['generation'] == after['stats']['generation'])}
                case['host_cpu_seconds'] = cpu_seconds(host)
                case['host_cpu_percent_of_one_core'] = case['host_cpu_seconds'] / seconds * 100
                report['cases'].append(case)
                (output / f'{rate}-{bits}-block{block}-lead{lead}.txt').write_text(text, encoding='utf-8')
                print(text, flush=True)
                report['cpu_seconds'] = cpu_seconds(service)
                report['cpu_percent_of_one_core'] = report['cpu_seconds'] / (time.monotonic() - start) * 100
                save()
            report['okay'] = all(not c['exit_code'] and c['session_continues'] for c in report['cases'])
            service.wait(timeout=max(1, duration + 5 - (time.monotonic() - start)))
            report['service_exit_code'] = service.returncode
            report['okay'] &= service.returncode == 0
        except (OSError, RuntimeError, subprocess.TimeoutExpired) as exc:
            report['okay'] = False; report['error'] = str(exc)
        finally:
            if host is not None and host.poll() is None:
                host.terminate(); host.wait(timeout=10)
            if service.poll() is None:
                service.terminate(); service.wait(timeout=10)
            report['duration_s'] = round(time.monotonic() - start, 3)
            report['completed_utc'] = datetime.now(timezone.utc).isoformat(); save()
    print('USB_ASIO_COMPLETE', report.get('okay', False), flush=True)
    return 0 if report.get('okay') else 1


if __name__ == '__main__':
    raise SystemExit(main())
