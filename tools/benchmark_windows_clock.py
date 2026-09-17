"""Compare HPET/TSC in one running game scene; always restore TSC afterwards.

Requires the corrected WHPX cold boot, a rooted test AVD and the host build
with frame-interval statistics. Wear the headset and keep the scene unchanged.
"""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import time

PATTERN = re.compile(r'host-image-arrival: rate=([\d.]+)/s avg=([\d.]+)ms max=([\d.]+)ms samples=(\d+) p50=([\d.]+)ms p95=([\d.]+)ms p99=([\d.]+)ms')

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--serial', default='emulator-5580')
    parser.add_argument('--seconds', type=int, default=35)
    parser.add_argument('--host-log', type=Path, default=Path('build-windows-game/host.err'))
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.seconds < 20 or args.output.exists():
        parser.error('Use at least 20 seconds per phase and a new output path')
    adb = str(Path(os.environ['LOCALAPPDATA'])/'Android/Sdk/platform-tools/adb.exe')
    def shell(command):
        return subprocess.check_output([adb,'-s',args.serial,'shell',command],text=True,timeout=15).strip()
    clock_dir = '/sys/devices/system/clocksource/clocksource0/'
    def switch(clock):
        shell(f'su 0 sh -c "echo {clock} > {clock_dir}current_clocksource"')
        actual = shell('su 0 cat '+clock_dir+'current_clocksource')
        if actual != clock:
            raise RuntimeError(f'Requested {clock}, actual clock is {actual}')
    if 'tsc' not in shell('su 0 cat '+clock_dir+'available_clocksource').split():
        raise RuntimeError('TSC is not accepted by the kernel; do not force it')
    phases = []
    try:
        with args.host_log.open('rb') as log:
            log.seek(0,2)
            for clock in ['hpet','tsc','hpet','tsc']:
                switch(clock)
                phase = {'clock':clock,'samples':[]}
                phases.append(phase)
                start = time.monotonic()
                print(f'{clock}: measuring {args.seconds}s, excluding first 10s after switch',flush=True)
                while time.monotonic()-start < args.seconds:
                    line = log.readline()
                    if not line:
                        time.sleep(.1)
                        continue
                    match = PATTERN.search(line.decode('utf-8',errors='replace'))
                    if match and time.monotonic()-start >= 10:
                        values = [float(v) for v in match.groups()]
                        phase['samples'].append(dict(zip(['fps','mean_ms','max_ms','frames','p50_ms','p95_ms','p99_ms'],values)))
                print(json.dumps(phase),flush=True)
    finally:
        try:
            switch('tsc')
        finally:
            args.output.parent.mkdir(parents=True,exist_ok=True)
            args.output.write_text(json.dumps(phases,indent=2))

if __name__ == '__main__':
    main()
