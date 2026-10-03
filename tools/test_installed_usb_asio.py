"""Check 18 PCM profiles through the installed ASIO COM driver and SCM service.

Uses one real Pi digital-loopback backend. Each native host runs for five
seconds; the whole matrix is bounded to five minutes. Restores the USB profile
and never resets the user's mixer, ASIO preferences or installed service.
Close the DAW before running. This is not an Ableton or physical ADC/DAC test.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import runpy
import subprocess
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bin', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--block', type=int, choices=(16, 32, 64, 128, 256))
    parser.add_argument('--lead', type=int, choices=range(1, 5), default=3)
    parser.add_argument('--depth', type=int, choices=range(1, 17), default=3)
    args = parser.parse_args()
    binary, output = args.bin.resolve(), args.out.resolve()
    output.mkdir(parents=True, exist_ok=True)
    tools = Path(__file__).resolve().parent
    request = runpy.run_path(str(tools / 'test_manager.py'))['request']
    cpu_seconds = runpy.run_path(str(tools / 'test_usb_asio.py'))['cpu_seconds']
    report = {'started_utc': datetime.now(timezone.utc).isoformat(),
        'physical_audio': False, 'actual_ableton_host': False,
        'installed_com': True, 'cases': []}
    start, original, identity, changed, host = time.monotonic(), None, None, False, None

    def save():
        (output / 'results.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')

    def apply(profile):
        fields = [profile[name] for name in ('rate', 'bits', 'depth', 'block', 'guard')]
        fields.append(int(profile['automatic']))
        command = 'USB ' + ' '.join(map(str, fields))
        reply = request(command)
        if not reply['ok']:
            raise RuntimeError('USB profile rejected: ' + reply.get('error', ''))

    def ready(rate, bits, restoring=False):
        end = time.monotonic() + 10
        if not restoring:
            end = min(end, start + 270)
        while time.monotonic() < end:
            status = request('STATUS')
            if (status['identity'] == identity and status['state'] == 'streaming' and
                    status['active']['rate'] == rate and status['active']['bits'] == bits and
                    status.get('asio', {}).get('ready') and status['stats']['callbacks'] > 500):
                return status
            time.sleep(.1)
        raise RuntimeError('Requested USB profile did not become ready')

    try:
        before = request('STATUS')
        report['before'] = before
        if (not before.get('asio_only') or before['route'] != 'usb' or
                before.get('asio', {}).get('connected') or before.get('device_limit') != 1):
            raise RuntimeError('One idle USB-ASIO SCM session is required; close the DAW')
        mixer = before['mixer']
        if (mixer['master_cdb'] != 0 or mixer['master_mute'] or any(
                channel['gain_cdb'] != 0 or channel['mute'] or channel['solo'] or channel['invert']
                for direction in ('inputs', 'outputs') for channel in mixer[direction])):
            raise RuntimeError('PCM marker checks require unity mixer; user settings were not changed')
        original, identity = before['usb'].copy(), before['identity']
        for name, filename in (('service', 'ReyAudioService.exe'), ('driver', 'ReyAudioAsio.dll'),
                               ('probe', 'ReyAsioProbe.exe')):
            report[name + '_sha256'] = hashlib.sha256((binary / filename).read_bytes()).hexdigest()
        for rate in (44100, 48000, 88200, 96000, 176400, 192000):
            for bits in (16, 24, 32):
                if time.monotonic() - start > 265:
                    raise RuntimeError('Finite matrix deadline exceeded')
                block = args.block or (16 if rate <= 48000 else 32 if rate <= 96000 else 64)
                changed = True
                apply({**original, 'rate': rate, 'bits': bits, 'depth': args.depth, 'block': 64,
                       'guard': 0, 'automatic': True})
                baseline = ready(rate, bits)
                environment = dict(os.environ, REY_ASIO_LEAD_BLOCKS=str(args.lead))
                host = subprocess.Popen([str(binary / 'ReyAsioProbe.exe'), '--registered',
                    '--seconds', '5', '--block', str(block)], stdout=subprocess.PIPE,
                    stderr=subprocess.STDOUT, text=True, env=environment,
                    creationflags=subprocess.CREATE_NO_WINDOW)
                text, _ = host.communicate(timeout=max(.1, min(15, start + 270 - time.monotonic())))
                after = request('STATUS')
                metrics = {}
                for line in text.splitlines():
                    if line.startswith('ASIO_RESULT '):
                        metrics = dict(field.split('=', 1) for field in line.split()[1:])
                same = (after['state'] == 'streaming' and after['active'] == baseline['active'] and
                        after['stats']['generation'] == baseline['stats']['generation'])
                elapsed = float(metrics.get('seconds', 0))
                actual_rate = int(metrics.get('frames', 0)) / elapsed if elapsed > 0 else 0
                case = {'rate': rate, 'bits': bits, 'block': block, 'lead_blocks': args.lead,
                    'usb_depth': args.depth, 'actual_frames_per_second': actual_rate,
                    'nominal_rate_ratio': actual_rate / rate,
                    'seconds': 5, 'exit_code': host.returncode, 'metrics': metrics,
                    'output': text, 'session_continues': same, 'service_after': after,
                    'host_cpu_seconds': cpu_seconds(host)}
                case['okay'] = host.returncode == 0 and metrics.get('okay') == '1' and same
                report['cases'].append(case)
                (output / f'{rate}-{bits}-block{block}.txt').write_text(text, encoding='utf-8')
                print('INSTALLED_ASIO_CASE', rate, bits, block, case['okay'], metrics, flush=True)
                save()
        report['okay'] = len(report['cases']) == 18 and all(case['okay'] for case in report['cases'])
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
        report['okay'] = False
        report['error'] = str(error)
    finally:
        if host is not None and host.poll() is None:
            host.terminate()
            host.wait(timeout=5)
        if changed:
            try:
                apply(original)
                report['restored'] = ready(original['rate'], original['bits'], restoring=True)
                report['profile_restored'] = all(report['restored']['usb'][key] == original[key]
                    for key in ('rate', 'bits', 'depth', 'block', 'guard', 'automatic'))
                if not report['profile_restored']:
                    report['okay'] = False
            except (OSError, RuntimeError) as error:
                report['okay'] = False
                report['restore_error'] = str(error)
        report['duration_s'] = round(time.monotonic() - start, 3)
        report['completed_utc'] = datetime.now(timezone.utc).isoformat()
        save()
    print('INSTALLED_ASIO_COMPLETE', report.get('okay', False), report['duration_s'], flush=True)
    return 0 if report.get('okay') else 1


if __name__ == '__main__':
    raise SystemExit(main())
