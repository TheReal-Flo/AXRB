"""Install the audio HAL adapter on the evaluated Android 16 emulator image."""
import hashlib
from pathlib import Path
import re
import shlex
import subprocess
import time

HAL_DIR = '/vendor/lib64/hw'
HAL_NAME = 'android.hardware.audio@7.1-impl.ranchu.so'
OVERLAY = '/data/vendor/axrb/audio-hw'
ORIGINAL = 'libaxrb_audio_original.so'
STOCK_SHA256 = '817284e98733d95a32df9c0cea2b09d500ee42adc05c60c605c79afeb7df018f'


def build_adapter(sdk, root):
    from distribution import bundled_library
    bundled = bundled_library(root, 'out/android/audio/libaxrb_audio_compat.so')
    if bundled:
        return bundled
    source = root / 'runtime/audio/ranchu_audio_compat.cpp'
    output = root / 'out/android/audio'
    output.mkdir(parents=True, exist_ok=True)
    compilers = sorted(sdk.glob('ndk/*/toolchains/llvm/prebuilt/windows-x86_64/bin/x86_64-linux-android29-clang.cmd'),
                       key=lambda p: tuple(int(n) for n in p.parents[5].name.split('.')))
    if not compilers:
        raise RuntimeError('Android NDK required for the audio adapter')
    compiler = compilers[-1]
    library = output / 'libaxrb_audio_compat.so'
    stamp = output / 'build.sha256'
    header = source.with_name('pcm_recovery.h')
    key = hashlib.sha256(source.read_bytes() + header.read_bytes() + Path(__file__).read_bytes() + str(compiler).encode()).hexdigest()
    if library.exists() and stamp.exists() and stamp.read_text() == key:
        return library
    # Link-only stub gives DT_NEEDED a separate name. At runtime that dependency
    # is the byte-for-byte stock HAL, never this stub or a patched copy.
    stub = output / 'original_stub.c'
    stub.write_text('void* HIDL_FETCH_IDevicesFactory(const char* name) { return 0; }\n')
    dependency = output / ORIGINAL
    commands = [
        ['-shared', '-fPIC', str(stub), '-Wl,-soname,' + ORIGINAL, '-o', str(dependency)],
        ['-shared', '-fPIC', '-O2', '-Wall', '-Wextra', '-Werror', str(source), '-nostdlib++',
         '-ldl', '-llog', '-Wl,-soname,libaxrb_audio_compat.so', '-Wl,--no-as-needed',
         str(dependency), '-o', str(library)],
    ]
    for command in commands:
        subprocess.run([str(compiler), *command], check=True, capture_output=True, timeout=120)
    stamp.write_text(key)
    return library


def audio_ready(shell, expected_frames):
    if shell('getprop init.svc.vendor.audio-hal') != 'running':
        return False
    dump = shell('dumpsys -t 2 media.audio_flinger')
    # Match live output-thread records, excluding historical stream dumps.
    frames = [int(n) for n in re.findall(r'^  HAL frame count: (\d+)$', dump, re.M)]
    return bool(frames) and all(n == expected_frames for n in frames)


def wait_for_audio(shell, frames):
    for _ in range(20):
        if audio_ready(shell, frames):
            return
        time.sleep(0.5)
    raise RuntimeError(f'Audio driver did not expose {frames}-frame output buffers')


def apply_audio_policy(run, sdk, root):
    def shell(command):
        return run('shell', 'su 0 sh -c ' + shlex.quote(command))

    if shell('getprop ro.hardware') != 'ranchu' or shell('getprop ro.build.version.sdk') != '36':
        return {'status': 'skipped', 'reason': 'Requires the Android 16 emulator'}
    mounts = [line.split() for line in shell('cat /proc/mounts').splitlines()]
    mounted = any(len(m) > 1 and m[1] == HAL_DIR for m in mounts)
    if mounted:
        identities = shell(f'stat -c %d:%i {HAL_DIR} {OVERLAY}').splitlines()
        if len(identities) != 2 or identities[0] != identities[1]:
            return {'status': 'skipped', 'reason': 'An external HAL overlay is active'}
    original = f'{HAL_DIR}/{ORIGINAL if mounted else HAL_NAME}'
    if shell('sha256sum ' + original).split()[0] != STOCK_SHA256:
        return {'status': 'skipped', 'reason': 'Audio HAL version has not been evaluated'}
    library = build_adapter(sdk, root)
    expected = hashlib.sha256(library.read_bytes()).hexdigest()
    if mounted and shell(f'sha256sum {HAL_DIR}/{HAL_NAME}').split()[0] == expected:
        if audio_ready(shell, 960):
            return {'status': 'unchanged', 'output_frames': 960}

    try:
        if not mounted:
            # Preserve every neighboring HAL and its SELinux label. cp -a on
            # Android does not preserve labels, so restore them explicitly.
            entries = []
            for line in shell('ls -Z ' + HAL_DIR).splitlines():
                label, name = line.split()
                if not re.fullmatch(r'[A-Za-z0-9_.@+-]+', name) or not re.fullmatch(r'u:object_r:[a-z0-9_]+:s0', label):
                    raise RuntimeError('Unexpected HAL directory entry')
                entries.append((label, name))
            shell(f'mkdir -p {OVERLAY}')
            shell(f'cp -a {HAL_DIR}/. {OVERLAY}/')
            shell(f'mv -f {OVERLAY}/{HAL_NAME} {OVERLAY}/{ORIGINAL}')
            for label, name in entries:
                destination = ORIGINAL if name == HAL_NAME else name
                shell(f'chcon {label} {OVERLAY}/{destination}')
            shell(f'chmod 755 {OVERLAY} && chcon u:object_r:vendor_file:s0 {OVERLAY}')
        staged = f'{OVERLAY}/{HAL_NAME}.new'
        run('push', str(library), staged)
        if shell('sha256sum ' + staged).split()[0] != expected:
            raise RuntimeError('Audio adapter transfer verification failed')
        shell(f'chmod 644 {staged} && chcon u:object_r:vendor_file:s0 {staged}')
        shell(f'mv -f {staged} {OVERLAY}/{HAL_NAME}')
        if not mounted:
            shell(f'mount --bind {OVERLAY} {HAL_DIR}')
            mounted = True
        shell('setprop ctl.restart vendor.audio-hal')
        wait_for_audio(shell, 960)
    except Exception:
        if mounted:
            shell('umount ' + HAL_DIR)
            shell('setprop ctl.restart vendor.audio-hal')
            wait_for_audio(shell, 1088)
        raise
    return {'status': 'applied', 'output_frames': 960}
