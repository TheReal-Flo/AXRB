"""Build and run descriptor compatibility tests on an already running x86_64 emulator."""
import argparse
import os
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--sdk', type=Path, default=Path(os.environ['LOCALAPPDATA']) / 'Android/Sdk')
parser.add_argument('--serial', default='emulator-5582')
args = parser.parse_args()
compiler = sorted(args.sdk.glob('ndk/*/toolchains/llvm/prebuilt/windows-x86_64/bin/x86_64-linux-android29-clang++.cmd'),
                  key=lambda p: tuple(map(int, p.parents[5].name.split('.'))))[-1]
adb = [str(args.sdk / 'platform-tools/adb.exe'), '-s', args.serial]
output = root / 'out/android/vulkan'
output.mkdir(parents=True, exist_ok=True)
for name in ('vulkan_descriptor_template_smoke', 'vulkan_descriptor_layer_smoke'):
    binary = output / name
    subprocess.run([str(compiler), '-std=c++17', '-O2', '-static-libstdc++',
                    str(root / 'tests/native' / (name + '.cpp')), '-llog', '-o', str(binary)], check=True, timeout=120)
    remote = '/data/local/tmp/' + name
    subprocess.run(adb + ['push', str(binary), remote], check=True, timeout=30)
    subprocess.run(adb + ['shell', 'chmod 755 ' + remote + ' && ' + remote], check=True, timeout=60)
