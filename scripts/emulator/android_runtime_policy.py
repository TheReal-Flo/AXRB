"""Apply emulator configuration without opening or rewriting application files."""
import argparse
import json
import hashlib
from pathlib import Path
import subprocess
import shlex
from storage_policy import apply_storage_policy
from audio_policy import apply_audio_policy
from distribution import bundled_library


def ensure_adb_root(run):
    # adbd can close the connection while restarting as root. Check the
    # reconnected daemon's UID instead of treating that disconnect as failure.
    root_error = None
    try:
        run('root')
    except subprocess.CalledProcessError as error:
        root_error = error
    run('wait-for-device')
    if run('shell', 'id -u') != '0':
        raise RuntimeError('The emulator ADB daemon did not obtain root access') from root_error


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk', type=Path, required=True)
    parser.add_argument('--serial', required=True)
    parser.add_argument('--package', required=True)
    parser.add_argument('--storage-read-ahead-kib', type=int, choices=(128, 1024), default=1024)
    args = parser.parse_args()
    import re
    if not re.fullmatch(r'[A-Za-z][A-Za-z0-9_]*(?:\.[A-Za-z0-9_]+)+', args.package):
        parser.error('Invalid Android package')
    adb = [str(args.sdk / 'platform-tools/adb.exe'), '-s', args.serial]
    def run(*parts):
        return subprocess.run([*adb, *parts], check=True, capture_output=True,
                              text=True, timeout=120).stdout.strip()
    if run('shell', 'getprop ro.hardware') != 'ranchu':
        print(json.dumps({'status': 'skipped', 'reason': 'Not the AXRB emulator'})); return
    ensure_adb_root(run)
    root = Path(__file__).resolve().parents[2]
    audio = apply_audio_policy(run, args.sdk, root)
    storage = apply_storage_policy(
        lambda command: run('shell', 'su 0 sh -c ' + shlex.quote(command)),
        args.storage_read_ahead_kib)
    # VR applications have no visible Android screen on which to dismiss the
    # first-use immersive-mode prompt. Confirm it for the headless guest.
    run('shell', 'settings put secure immersive_mode_confirmations confirmed')
    # Stock API 36 revision 7 predates Mesa's opaque-handle debug-name fix.
    # Scope the compatibility guard to the evaluated image, not application IDs.
    fingerprint = run('shell', 'getprop ro.build.fingerprint')
    debug_name_guard = fingerprint == 'google/sdk_gphone64_x86_64/emu64xa:16/BE2A.250530.026.F3/13894323:userdebug/dev-keys'
    run('shell', 'setprop debug.axrb.gfxstream_debug_names ' + ('1' if debug_name_guard else '0'))
    limit = int(run('shell', 'cat /proc/sys/vm/max_map_count'))
    if limit < 1048576:
        run('shell', 'echo 1048576 > /proc/sys/vm/max_map_count')
        limit = int(run('shell', 'cat /proc/sys/vm/max_map_count'))
        if limit < 1048576: raise RuntimeError('Memory mapping policy did not apply')
    source = root / 'runtime/vulkan/android_vulkan_layer.cpp'
    header = root / 'runtime/vulkan/vulkan_descriptor_template.h'
    output = root / 'out/android/vulkan/libVkLayer_AXRB_runtime.so'
    bundled = bundled_library(root, 'out/android/vulkan/libVkLayer_AXRB_runtime.so')
    if not bundled:
        output.parent.mkdir(parents=True, exist_ok=True)
    if not bundled and (not output.exists() or output.stat().st_mtime < max(source.stat().st_mtime, header.stat().st_mtime)):
        compilers = sorted((args.sdk / 'ndk').glob('*/toolchains/llvm/prebuilt/windows-x86_64/bin/x86_64-linux-android29-clang++.cmd'),
                           key=lambda p: tuple(int(n) for n in p.parents[5].name.split('.')))
        if not compilers: raise RuntimeError('Android NDK required for the runtime Vulkan layer')
        subprocess.run([str(compilers[-1]), '-std=c++17', '-shared', '-fPIC', '-O2', '-static-libstdc++',
                        '-Wl,-Bsymbolic', str(source), '-llog', '-o', str(output)], check=True, timeout=120)
    directory = '/data/local/debug/vulkan'
    remote = directory + '/libVkLayer_AXRB_runtime.so'
    expected = hashlib.sha256(output.read_bytes()).hexdigest()
    run('shell', 'mkdir -p ' + directory)
    current = run('shell', 'if [ -f ' + remote + ' ]; then sha256sum ' + remote + '; fi').split()
    if not current or current[0] != expected:
        run('push', str(output), remote + '.new')
        if run('shell', 'sha256sum ' + remote + '.new').split()[0] != expected:
            raise RuntimeError('Runtime Vulkan layer transfer verification failed')
        run('shell', 'mv -f ' + remote + '.new ' + remote)
    # System debug layers need a readable/executable native-library label on
    # this userdebug image. Only our directory and library are relabeled.
    paths = '/data/local/debug ' + directory + ' ' + remote
    run('shell', 'chmod 755 ' + paths + ' && chcon u:object_r:apk_data_file:s0 ' + paths)
    run('shell', 'settings put global enable_gpu_debug_layers 1')
    run('shell', 'settings put global gpu_debug_app ' + args.package)
    run('shell', 'settings put global gpu_debug_layers VK_LAYER_AXRB_runtime')
    run('shell', 'settings delete global gpu_debug_layer_app')
    run('shell', 'sync')
    print(json.dumps({'status': 'ready', 'max_map_count': limit, 'vulkan_layer': remote,
                      'gfxstream_debug_name_guard': debug_name_guard, 'storage': storage, 'audio': audio}))


if __name__ == '__main__': main()
