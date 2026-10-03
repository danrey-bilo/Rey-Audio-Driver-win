"""Observe an existing ASIO host for at most three minutes; never change audio.

This checks live PCM/deadline counters and process ownership. It does not inject
markers or measure roundtrip/converter latency. The host keeps running afterward.
"""
import argparse
import ctypes
from ctypes import wintypes
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import runpy
import time


class Process:
    def __init__(self, pid):
        self.api = ctypes.WinDLL('kernel32', use_last_error=True)
        self.api.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
        self.api.OpenProcess.restype = wintypes.HANDLE
        self.api.QueryFullProcessImageNameW.argtypes = [wintypes.HANDLE, wintypes.DWORD, wintypes.LPWSTR, ctypes.POINTER(wintypes.DWORD)]
        self.api.GetProcessTimes.argtypes = [wintypes.HANDLE] + [ctypes.POINTER(wintypes.FILETIME)] * 4
        self.api.CloseHandle.argtypes = [wintypes.HANDLE]
        self.handle = self.api.OpenProcess(0x1000, False, pid)
        if not self.handle:
            raise ctypes.WinError(ctypes.get_last_error())
        text, size = ctypes.create_unicode_buffer(32768), wintypes.DWORD(32768)
        if not self.api.QueryFullProcessImageNameW(self.handle, 0, text, ctypes.byref(size)):
            self.close(); raise ctypes.WinError(ctypes.get_last_error())
        self.path = text.value

    def cpu(self):
        values = [wintypes.FILETIME() for _ in range(4)]
        if not self.api.GetProcessTimes(self.handle, *[ctypes.byref(value) for value in values]):
            raise ctypes.WinError(ctypes.get_last_error())
        return sum((value.dwHighDateTime << 32) | value.dwLowDateTime for value in values[2:]) / 10000000

    def close(self):
        if self.handle:
            self.api.CloseHandle(self.handle); self.handle = None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--seconds', type=int, default=175)
    parser.add_argument('--rate', type=int, default=192000)
    parser.add_argument('--block', type=int, choices=(16, 32, 64, 128, 256),
                        help='Require this live ASIO block before observing')
    parser.add_argument('--cpu-simulator', type=int, choices=range(0, 101),
                        help='Ableton CPU Usage Simulator value confirmed by the operator')
    parser.add_argument('--service-pid', type=int, required=True)
    parser.add_argument('--service-exe', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    if not 1 <= args.seconds <= 175:
        parser.error('Duration must be 1..175 seconds')
    request = runpy.run_path(str(Path(__file__).resolve().with_name('test_manager.py')))['request']
    before = request('STATUS'); asio = before.get('asio', {})
    if before['state'] != 'streaming' or not asio.get('running') or before['active']['rate'] != args.rate:
        raise RuntimeError('The requested live ASIO profile is not ready')
    if args.block is not None and asio.get('block') != args.block:
        raise RuntimeError('The requested live ASIO buffer size is not ready')
    if not isinstance(asio.get('connection_id'), int) or asio['connection_id'] <= 0:
        raise RuntimeError('A service build with ASIO connection identifiers is required')
    owner, service, cpu_error = Process(asio['pid']), None, None
    try:
        service = Process(args.service_pid)
    except OSError as error:
        cpu_error = str(error)
    start, owner_cpu = time.monotonic(), owner.cpu()
    service_cpu = service.cpu() if service else None
    profile = {**before['active'], 'block': asio['block'], 'lead_blocks': asio['lead_blocks']}
    counters = ('capture_dropped', 'render_late_frames', 'render_missing_frames', 'render_overflow')
    previous_counters = {name: asio.get(name, 0) for name in counters}
    report = {'started_utc': datetime.now(timezone.utc).isoformat(), 'profile': profile,
        'host_pid': asio['pid'], 'host_path': owner.path, 'service_pid': args.service_pid,
        'service_path': service.path if service else None, 'installed_service_file': str(args.service_exe.resolve()),
        'service_cpu_unavailable': cpu_error, 'physical_audio': False, 'rtt_measured': False,
        'actual_ableton_host': Path(owner.path).name.lower() == 'ableton live 12 suite.exe',
        'operator_confirmed_cpu_simulator_percent': args.cpu_simulator,
        'before': before, 'samples': [], 'signal_snapshots': {'inputs': 0, 'outputs': 0}}
    folder = args.out.resolve(); folder.mkdir(parents=True, exist_ok=True)
    output = folder / 'continuity.json'
    report['service_sha256'] = hashlib.sha256(args.service_exe.read_bytes()).hexdigest()
    report['driver_sha256'] = hashlib.sha256(args.service_exe.with_name('ReyAudioAsio.dll').read_bytes()).hexdigest()
    def save():
        output.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    save()
    okay = True
    try:
        previous_print = -30
        while time.monotonic() - start < args.seconds:
            time.sleep(max(0, min(1, args.seconds - (time.monotonic() - start))))
            status = request('STATUS'); live = status.get('asio', {})
            elapsed = time.monotonic() - start
            same = (status['state'] == 'streaming' and live.get('running') and live.get('pid') == asio['pid'] and
                    status['stats']['generation'] == before['stats']['generation'] and status['active'] == before['active'] and
                    live.get('connection_id') == asio.get('connection_id') and
                    live.get('block') == profile['block'] and live.get('lead_blocks') == profile['lead_blocks'])
            if any(live.get(name, -1) < previous_counters[name] for name in counters):
                same = False; report['counter_reset_detected'] = True
            previous_counters = {name: live.get(name, 0) for name in counters}
            okay &= bool(same)
            peaks = {direction: [channel['peak'] for channel in status['mixer'][direction]]
                for direction in ('inputs', 'outputs')}
            for direction, values in peaks.items():
                report['signal_snapshots'][direction] += int(any(value > 0 for value in values))
            report['samples'].append({'elapsed_s': round(elapsed, 3), 'same_session': bool(same),
                'stats': status['stats'], 'asio': live, 'peaks': peaks})
            if elapsed - previous_print >= 30:
                previous_print = elapsed
                print('ASIO_HOST_LIVE', round(elapsed), live, flush=True); save()
            if not same:
                report['error'] = 'Host/profile/session changed during observation'; break
        report['after'] = request('STATUS')
        report['duration_s'] = round(time.monotonic() - start, 3)
        report['counter_deltas'] = {name: report['after'].get('asio', {}).get(name, -1) - asio.get(name, 0) for name in counters}
        report['frame_delta'] = report['after']['stats']['frames'] - before['stats']['frames']
        okay &= all(value == 0 for value in report['counter_deltas'].values())
        report['observed_frames_per_second'] = report['frame_delta'] / report['duration_s']
        report['nominal_rate_ratio'] = report['observed_frames_per_second'] / args.rate
        report['cadence_within_one_percent'] = abs(report['nominal_rate_ratio'] - 1) <= .01
        okay &= report['cadence_within_one_percent']
        report['host_cpu_seconds'] = owner.cpu() - owner_cpu
        report['service_cpu_seconds'] = service.cpu() - service_cpu if service else None
        report['host_cpu_percent_of_one_core'] = report['host_cpu_seconds'] / report['duration_s'] * 100
        report['service_cpu_percent_of_one_core'] = report['service_cpu_seconds'] / report['duration_s'] * 100 if service else None
    except (OSError, RuntimeError) as error:
        okay = False; report['error'] = str(error)
    finally:
        owner.close()
        if service:
            service.close()
        report['okay'] = okay; report['completed_utc'] = datetime.now(timezone.utc).isoformat(); save()
    print('ASIO_HOST_COMPLETE', okay, flush=True)
    return 0 if okay else 1


if __name__ == '__main__':
    raise SystemExit(main())
