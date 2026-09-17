"""Read an installed app's default display label from its base APK.

Uses SDK adb/aapt2, removes the temporary APK, and emits ASCII JSON so Windows
PowerShell can preserve Unicode labels independently of its console encoding.
"""
import argparse
import base64
import json
from pathlib import Path
import re
import subprocess
import tempfile


def application_label(badging, package):
    for line in badging.splitlines():
        if line.startswith("application-label:'") and line.endswith("'"):
            label = line[len("application-label:'"):-1].strip()
            if label:
                return label
    return package


def installed_label(sdk, serial, package):
    if not re.fullmatch(r"[a-zA-Z0-9_.]+", package):
        raise ValueError("Invalid package name")
    adb = sdk / "platform-tools/adb.exe"
    versions = []
    for directory in (sdk / "build-tools").iterdir():
        if re.fullmatch(r"\d+\.\d+\.\d+", directory.name) and (directory / "aapt2.exe").is_file():
            versions.append((tuple(map(int, directory.name.split('.'))), directory / "aapt2.exe"))
    if not versions:
        raise RuntimeError("Android SDK build-tools with aapt2 are required")
    aapt = max(versions)[1]
    def run(args, timeout=30):
        return subprocess.run(list(map(str, args)), check=True, capture_output=True,
                              encoding="utf-8", errors="replace", timeout=timeout,
                              creationflags=subprocess.CREATE_NO_WINDOW).stdout
    paths = run([adb, '-s', serial, 'shell', 'pm', 'path', package]).splitlines()
    apks = [line.removeprefix('package:').strip() for line in paths if line.startswith('package:')]
    base = next((path for path in apks if path.endswith('/base.apk')), None)
    if base is None and len(apks) == 1:
        base = apks[0]
    if base is None:
        raise RuntimeError(f"Cannot locate base APK for {package}")
    with tempfile.TemporaryDirectory(prefix='axrb-label-') as temp:
        apk = Path(temp) / 'base.apk'
        run([adb, '-s', serial, 'pull', base, apk], timeout=180)
        return application_label(run([aapt, 'dump', 'badging', apk], timeout=60), package)


def installed_metadata(sdk, serial, package, icon_output):
    if not re.fullmatch(r"[a-zA-Z0-9_.]+", package):
        raise ValueError("Invalid package name")
    try:
        result = subprocess.run([str(sdk / 'platform-tools/adb.exe'), '-s', serial,
            'shell', 'content', 'call', '--uri', 'content://org.khronos.openxr.system_runtime_broker',
            '--method', 'axrb_app_metadata', '--arg', package], check=True, capture_output=True,
            encoding='utf-8', timeout=30, creationflags=subprocess.CREATE_NO_WINDOW)
        fields = dict(re.findall(r'(label64|icon64)=([A-Za-z0-9+/=]+)', result.stdout))
        label = base64.b64decode(fields['label64'], validate=True).decode('utf-8')
        icon = base64.b64decode(fields['icon64'], validate=True)
        if not icon.startswith(b'\x89PNG\r\n\x1a\n'):
            raise ValueError('Invalid icon PNG')
        icon_output.parent.mkdir(parents=True, exist_ok=True)
        icon_output.write_bytes(icon)
        return {'label': label or package, 'icon': str(icon_output.resolve())}
    except (subprocess.SubprocessError, KeyError, ValueError):
        # Older runtime APKs still provide a name, without claiming stale artwork.
        return {'label': installed_label(sdk, serial, package), 'icon': None}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk', type=Path, required=True)
    parser.add_argument('--serial', required=True)
    parser.add_argument('--package', required=True)
    parser.add_argument('--icon-output', type=Path)
    args = parser.parse_args()
    print(json.dumps(installed_metadata(args.sdk, args.serial, args.package, args.icon_output)
        if args.icon_output else {'label': installed_label(args.sdk, args.serial, args.package)}))
