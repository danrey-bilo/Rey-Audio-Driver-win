"""Check 18 exclusive 8x8 Windows/TAG/Rey/USB/Pi PCM profiles in <=120 s.

The original TAG demo must already be installed, with its unmodified x64 SDK DLL
beside an optional-backend ReyAudioService build. Requires a Pi digital echo
backend. This tool measures PCM transfer, not ASIO or physical audio latency.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import runpy
import subprocess
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--service', type=Path, required=True)
    parser.add_argument('--probe', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--render-id', required=True)
    parser.add_argument('--capture-id', required=True)
    args = parser.parse_args()
    service_exe, probe_exe = args.service.resolve(), args.probe.resolve()
    if not service_exe.is_file() or not probe_exe.is_file():
        parser.error('Both executable paths must exist')
    folder = args.out.resolve()
    folder.mkdir(parents=True, exist_ok=True)
    root = Path(__file__).resolve().parents[2]
    request = runpy.run_path(str(root / 'tools/test_manager.py'))['request']
    try:
        request('STATUS')
    except OSError:
        pass
    else:
        parser.error('Stop the existing Rey service before running the isolated test')

    report = {
        'started_utc': datetime.now(timezone.utc).isoformat(),
        'scope': 'exclusive WASAPI markers through TAG/Rey/USB/Pi digital echo',
        'maximum_seconds': 120, 'physical_audio_qualified': False,
        'asio_latency_measured': False, 'cases': [],
        'binary_sha256': hashlib.sha256(service_exe.read_bytes()).hexdigest(),
    }
    start = time.monotonic()
    test_settings = folder / f'profile-{time.time_ns()}.dat'
    report['test_settings'] = str(test_settings)

    def save():
        (folder / 'format-matrix.json').write_text(
            json.dumps(report, indent=2) + '\n', encoding='utf-8')

    with (folder / 'service.log').open('w', encoding='utf-8') as log:
        service = subprocess.Popen(
            [str(service_exe), '--console', '--seconds', '120', '--test-settings', str(test_settings)],
            cwd=service_exe.parent, stdout=log, stderr=subprocess.STDOUT,
            creationflags=subprocess.CREATE_NO_WINDOW)

        def wait_for(rate, bits):
            for _ in range(70):
                if service.poll() is not None:
                    raise RuntimeError('Service exited during startup or reconfiguration')
                try:
                    state = request('STATUS')
                    if (state['state'] == 'streaming' and state['active']['rate'] == rate and
                            state['active']['bits'] == bits and state['stats']['callbacks'] > 500):
                        return state
                except OSError:
                    pass
                time.sleep(.1)
            raise RuntimeError(f'Streaming did not start for {rate}/{bits}')

        try:
            initial = wait_for(192000, 32)
            if initial['route'] != 'usb' or initial['digital_test'] or initial['backend'] != 'digital-loopback':
                raise RuntimeError('Expected the real USB digital-loopback backend with Windows endpoints attached')
            for rate in (44100, 48000, 88200, 96000, 176400, 192000):
                for bits in (16, 24, 32):
                    if time.monotonic() - start > 105:
                        raise RuntimeError('The finite run is approaching its deadline')
                    reply = request(f'USB {rate} {bits} 3 64 0 1')
                    if not reply['ok']:
                        raise RuntimeError('Profile rejected: ' + reply.get('error', ''))
                    before = wait_for(rate, bits)
                    attempts = []
                    for _ in range(5):
                        probe = subprocess.run(
                            [str(probe_exe), args.render_id, args.capture_id, str(rate), str(bits)],
                            capture_output=True, text=True, timeout=10)
                        output = probe.stdout + probe.stderr
                        attempts.append({'exit_code': probe.returncode, 'output': output})
                        # Only retry endpoint activation invalidation after a profile
                        # change. Never conceal PCM mismatches or running-stream errors.
                        if probe.returncode != 4 or '0x88890004' not in output:
                            break
                        time.sleep(.2)
                    after = request('STATUS')
                    case = {
                        'rate': rate, 'bits': bits, 'active': after['active'],
                        'stats': after['stats'], 'probe_exit_code': probe.returncode,
                        'output': output, 'startup_attempts': attempts,
                        'session_continues': (after['state'] == 'streaming' and
                            before['stats']['generation'] == after['stats']['generation']),
                    }
                    report['cases'].append(case)
                    save()
                    print('FORMAT', rate, bits, probe.returncode, case['session_continues'], flush=True)
                    if probe.returncode or not case['session_continues'] or after['stats']['missing_frames']:
                        raise RuntimeError(f'PCM/continuity check failed for {rate}/{bits}')
            service.wait(timeout=max(1, 125 - (time.monotonic() - start)))
            report['service_exit_code'] = service.returncode
            report['okay'] = len(report['cases']) == 18 and service.returncode == 0
        except (OSError, RuntimeError, subprocess.TimeoutExpired) as exc:
            report['okay'] = False
            report['error'] = str(exc)
        finally:
            if service.poll() is None:
                service.terminate()
                service.wait(timeout=10)
            report['duration_s'] = round(time.monotonic() - start, 3)
            report['completed_utc'] = datetime.now(timezone.utc).isoformat()
            save()
    print('FORMAT_MATRIX_COMPLETE', report.get('okay', False), flush=True)
    return 0 if report.get('okay') else 1


if __name__ == '__main__':
    raise SystemExit(main())
