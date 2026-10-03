"""Finite manual-buffer check through installed COM ASIO.

Requires an idle USB-ASIO service and unity mixer. Checks one native digital
loopback profile for up to 295 seconds, with periodic status and CPU evidence.
ASIO preferences are unchanged; an optional USB depth is restored afterward.
This is not an Ableton-host or physical ADC/DAC latency measurement.
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
import winreg


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bin', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--block', type=int, required=True, choices=(16, 32, 64, 128, 256))
    parser.add_argument('--lead', type=int, required=True, choices=(1, 2, 3, 4))
    parser.add_argument('--depth', type=int, choices=(1, 2, 3, 4, 6, 8, 12, 16))
    parser.add_argument('--seconds', type=int, default=295)
    args = parser.parse_args()
    if not 1 <= args.seconds <= 295:
        parser.error('Audio duration must be 1..295 seconds')
    binary, output = args.bin.resolve(), args.out.resolve()
    output.mkdir(parents=True, exist_ok=True)
    tools = Path(__file__).resolve().parent
    request = runpy.run_path(str(tools / 'test_manager.py'))['request']
    cpu_seconds = runpy.run_path(str(tools / 'test_usb_asio.py'))['cpu_seconds']
    report = {'started_utc': datetime.now(timezone.utc).isoformat(),
        'physical_audio': False, 'actual_ableton_host': False, 'installed_com': True,
        'block': args.block, 'lead_blocks': args.lead, 'seconds_requested': args.seconds,
        'saved_preferences_changed': False, 'live': []}
    start, host, original, usb_changed = time.monotonic(), None, None, False

    def usb_profile(status, depth):
        profile = status['usb']
        reply = request('USB ' + ' '.join(map(str,
            (profile['rate'], profile['bits'], depth, profile['block'], profile['guard'],
             int(profile['automatic'])))))
        if not reply['ok']:
            raise RuntimeError('Temporary USB profile rejected: ' + reply.get('error', ''))

    def ready_after(generation, status, depth):
        end = time.monotonic() + 10
        while time.monotonic() < end:
            current = request('STATUS')
            if (current['identity'] == status['identity'] and current['state'] == 'streaming' and
                    current['stats']['generation'] > generation and current['usb']['depth'] == depth and
                    current['active'] == status['active'] and current.get('asio', {}).get('ready') and
                    current['stats']['callbacks'] > 500):
                return current
            time.sleep(.1)
        raise RuntimeError('USB profile restart did not become ready')

    def save():
        (output / 'results.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')

    try:
        with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE,
                r'Software\Classes\CLSID\{91BA2C4A-56BC-4C49-A499-231979EC5F60}\InprocServer32',
                0, winreg.KEY_READ | winreg.KEY_WOW64_64KEY) as key:
            registered_dll = Path(winreg.QueryValueEx(key, None)[0]).resolve()
        if registered_dll != binary / 'ReyAudioAsio.dll':
            raise RuntimeError('--bin must name the registered USB-ASIO installation')
        before = request('STATUS')
        report['before'] = before
        if (not before.get('asio_only') or before['route'] != 'usb' or before['state'] != 'streaming' or
                not before.get('asio', {}).get('ready') or before['asio'].get('connected')):
            raise RuntimeError('An idle ready USB-ASIO service is required; close ASIO in the DAW')
        mixer = before['mixer']
        if (mixer['master_cdb'] != 0 or mixer['master_mute'] or any(
                channel['gain_cdb'] != 0 or channel['mute'] or channel['solo'] or channel['invert']
                for direction in ('inputs', 'outputs') for channel in mixer[direction])):
            raise RuntimeError('PCM markers require unity mixer; user settings were not changed')
        for name, filename in (('service', 'ReyAudioService.exe'), ('driver', 'ReyAudioAsio.dll'),
                               ('probe', 'ReyAsioProbe.exe')):
            report[name + '_sha256'] = hashlib.sha256((binary / filename).read_bytes()).hexdigest()
        if args.depth is not None and args.depth != before['usb']['depth']:
            original = before
            report['original'] = original
            usb_profile(original, args.depth)
            usb_changed = True
            before = ready_after(original['stats']['generation'], original, args.depth)
            report['before'] = before
        environment = dict(os.environ, REY_ASIO_LEAD_BLOCKS=str(args.lead))
        host = subprocess.Popen([str(binary / 'ReyAsioProbe.exe'), '--registered',
            '--seconds', str(args.seconds), '--block', str(args.block)], env=environment,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
            creationflags=subprocess.CREATE_NO_WINDOW)
        probe_deadline = time.monotonic() + 300
        save()
        while True:
            remaining = probe_deadline - time.monotonic()
            if remaining <= 0:
                raise RuntimeError('Finite probe deadline exceeded')
            try:
                text, _ = host.communicate(timeout=min(30, remaining))
                break
            except subprocess.TimeoutExpired:
                status = request('STATUS')
                report['live'].append({'elapsed_s': round(time.monotonic() - start, 3),
                    'asio': status.get('asio', {}), 'stats': status['stats'],
                    'host_cpu_seconds': cpu_seconds(host)})
                print('USB_ASIO_PROFILE_LIVE', report['live'][-1], flush=True)
                save()
        report['output'] = text
        report['exit_code'] = host.returncode
        report['metrics'] = {}
        for line in text.splitlines():
            if line.startswith('ASIO_RESULT '):
                report['metrics'] = dict(field.split('=', 1) for field in line.split()[1:])
        (output / 'probe.txt').write_text(text, encoding='utf-8')
        after = request('STATUS')
        report['after'] = after
        report['profile_unchanged'] = after['usb'] == before['usb'] and after['active'] == before['active']
        report['session_continues'] = (after['state'] == 'streaming' and
            after['stats']['generation'] == before['stats']['generation'])
        report['host_cpu_seconds'] = cpu_seconds(host)
        report['host_cpu_percent_of_one_core'] = report['host_cpu_seconds'] / float(
            report['metrics'].get('seconds', args.seconds)) * 100
        report['okay'] = (host.returncode == 0 and report['metrics'].get('okay') == '1' and
            report['profile_unchanged'] and report['session_continues'])
        if all(name in report['metrics'] for name in ('frames', 'seconds', 'rtt_max_us')):
            report['observed_frames_per_second'] = int(report['metrics']['frames']) / float(report['metrics']['seconds'])
            report['nominal_rate_ratio'] = report['observed_frames_per_second'] / before['active']['rate']
            # This detects gross slowing on the synthetic bench. It is not an
            # independent hardware-clock accuracy qualification.
            report['cadence_within_one_percent'] = abs(report['nominal_rate_ratio'] - 1) <= .01
            report['sampled_rtt_max_below_2ms'] = float(report['metrics']['rtt_max_us']) < 2000
            report['low_latency_candidate_qualified'] = (report['okay'] and
                report['cadence_within_one_percent'] and report['sampled_rtt_max_below_2ms'])
        else:
            report['okay'] = False
            report['error'] = 'Probe returned no complete ASIO_RESULT'
        print(text, flush=True)
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
        report['okay'] = False
        report['error'] = str(error)
    finally:
        if host is not None and host.poll() is None:
            host.terminate()
            host.wait(timeout=5)
        if usb_changed:
            try:
                previous_generation = request('STATUS')['stats']['generation']
                usb_profile(original, original['usb']['depth'])
                restored = ready_after(previous_generation, original, original['usb']['depth'])
                report['restored'] = restored
                report['usb_profile_restored'] = restored['usb'] == original['usb']
                if not report['usb_profile_restored']:
                    report['okay'] = False
            except (OSError, RuntimeError) as error:
                report['okay'] = False
                report['restore_error'] = str(error)
        report['duration_s'] = round(time.monotonic() - start, 3)
        report['low_latency_candidate_qualified'] = bool(report.get('okay') and
            report.get('low_latency_candidate_qualified'))
        report['completed_utc'] = datetime.now(timezone.utc).isoformat()
        save()
    print('USB_ASIO_PROFILE_COMPLETE functional=', report.get('okay', False),
        'low_latency=', report.get('low_latency_candidate_qualified', False), flush=True)
    return 0 if report.get('okay') and report.get('low_latency_candidate_qualified') else 1


if __name__ == '__main__':
    raise SystemExit(main())
