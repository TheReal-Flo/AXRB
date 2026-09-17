"""Register APK metadata with SteamVR; associate it with an OpenXR host PID.

Uses the installed SteamVR DLL, as a utility client, never a second scene client.
FnTable layout: Valve openvr/headers/openvr_capi.h, IVRApplications_008.
"""
import argparse
import ctypes as C
import hashlib
import json
import os
from pathlib import Path
import subprocess


def app_key(package):
    return 'axrb.android.' + hashlib.sha256(package.encode('utf-8')).hexdigest()[:32]


def manifest_data(package, name, icon, launcher, avd, activity):
    arguments = subprocess.list2cmdline(['-NoProfile', '-ExecutionPolicy', 'Bypass',
        '-File', str(launcher.resolve()), '-Avd', avd, '-Package', package, '-Activity', activity])
    app = {'app_key': app_key(package), 'launch_type': 'binary',
        'binary_path_windows': str(Path(os.environ.get('SystemRoot', 'C:/Windows')) /
            'System32/WindowsPowerShell/v1.0/powershell.exe'),
        'arguments': arguments, 'working_directory': str(launcher.resolve().parent.parent),
        'strings': {'en_us': {'name': name + ' \u2013 AXRB'}}}
    if icon and icon.is_file():
        app['image_path'] = str(icon.resolve())
    return {'source': 'AXRB', 'applications': [app]}


class SteamVR:
    def __init__(self):
        paths = json.loads((Path(os.environ['LOCALAPPDATA']) / 'openvr/openvrpaths.vrpath').read_text())
        dll = Path(paths['runtime'][0]) / 'bin/win64/openvr_api.dll'
        self.dll = C.CDLL(str(dll))
        self.dll.VR_InitInternal.argtypes = [C.POINTER(C.c_int), C.c_int]
        self.dll.VR_InitInternal.restype = C.c_uint32
        self.dll.VR_ShutdownInternal.argtypes = []
        self.dll.VR_ShutdownInternal.restype = None
        self.dll.VR_GetGenericInterface.argtypes = [C.c_char_p, C.POINTER(C.c_int)]
        self.dll.VR_GetGenericInterface.restype = C.c_void_p
        error = C.c_int()
        self.dll.VR_InitInternal(C.byref(error), 4)  # VRApplication_Utility
        if error.value:
            raise RuntimeError(f'OpenVR initialization error {error.value}')
        table = self.dll.VR_GetGenericInterface(b'FnTable:IVRApplications_008', C.byref(error))
        if not table or error.value:
            self.close()
            raise RuntimeError(f'OpenVR applications interface error {error.value}')
        self.table = C.cast(table, C.POINTER(C.c_void_p))

    def close(self):
        self.dll.VR_ShutdownInternal()

    def function(self, index, result, *args):
        return C.WINFUNCTYPE(result, *args)(self.table[index])

    def check(self, code):
        if code:
            name = self.function(13, C.c_char_p, C.c_int)(code)
            raise RuntimeError(f'SteamVR: {name.decode() if name else code}')

    def register(self, path):
        self.check(self.function(0, C.c_int, C.c_char_p, C.c_bool)(str(path.resolve()).encode('utf-8'), False))

    def identify(self, pid, key):
        self.check(self.function(11, C.c_int, C.c_uint32, C.c_char_p)(pid, key.encode()))
        actual = C.create_string_buffer(128)
        self.check(self.function(5, C.c_int, C.c_uint32, C.c_char_p, C.c_uint32)(pid, actual, len(actual)))
        if actual.value.decode() != key:
            raise RuntimeError('SteamVR process identity did not match')

    def property(self, key, prop):
        value = C.create_string_buffer(32768)
        error = C.c_int()
        self.function(14, C.c_uint32, C.c_char_p, C.c_int, C.c_char_p, C.c_uint32,
            C.POINTER(C.c_int))(key.encode(), prop, value, len(value), C.byref(error))
        if error.value == 201:  # optional property not set
            return None
        self.check(error.value)
        return value.value.decode('utf-8')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--manifest', type=Path, required=True)
    parser.add_argument('--package', required=True)
    parser.add_argument('--name')
    parser.add_argument('--icon', type=Path)
    parser.add_argument('--launcher', type=Path)
    parser.add_argument('--avd')
    parser.add_argument('--activity')
    parser.add_argument('--pid', type=int)
    args = parser.parse_args()
    key = app_key(args.package)
    if args.pid is None:
        if not all([args.name, args.launcher, args.avd, args.activity]):
            parser.error('Registration requires name, launcher, avd and activity')
        args.manifest.parent.mkdir(parents=True, exist_ok=True)
        args.manifest.write_text(json.dumps(manifest_data(args.package, args.name, args.icon,
            args.launcher, args.avd, args.activity), ensure_ascii=False, indent=2), encoding='utf-8')
    vr = SteamVR()
    try:
        vr.register(args.manifest)
        if args.pid is not None:
            vr.identify(args.pid, key)
        print(json.dumps({'app_key': key, 'pid': args.pid,
            'name': vr.property(key, 0), 'image': vr.property(key, 52)}))
    finally:
        vr.close()


if __name__ == '__main__':
    main()
