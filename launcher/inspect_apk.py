"""Read APK identity with Android SDK tools; never install or modify the APK."""
import argparse, base64, json, re, subprocess, zipfile
from pathlib import Path

def inspect(apk, sdk):
    versions = [p for p in (sdk / 'build-tools').glob('*/aapt2.exe')]
    if not versions:
        raise RuntimeError('Android SDK build-tools (aapt2) are required')
    tool = max(versions, key=lambda p: tuple(int(x) for x in re.findall(r'\d+', p.parent.name)))
    output = subprocess.run([str(tool), 'dump', 'badging', str(apk)], check=True,
        capture_output=True, encoding='utf-8', errors='replace', timeout=60,
        creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0)).stdout
    package = re.search(r"^package: name='([^']+)'", output, re.M)
    activity = re.search(r"^launchable-activity: name='([^']+)'", output, re.M)
    if not package or not activity:
        raise RuntimeError('APK does not contain a launchable Android application')
    label = re.search(r"^application-label:'(.*)'$", output, re.M)
    version = re.search(r"versionName='([^']*)'", output)
    result = {'package': package[1], 'activity': package[1] + '/' + activity[1],
        'name': label[1] if label else package[1], 'version': version[1] if version else '', 'image': ''}
    with zipfile.ZipFile(apk) as archive:
        names = archive.namelist()
        result['patched'] = any('overport' in n.lower() or 'ovrport' in n.lower() for n in names)
        result['nativeAbis'] = sorted({n.split('/')[1] for n in names if n.startswith('lib/') and n.endswith('.so')})
        icons = re.findall(r"^application-icon-\d+:'([^']+)'", output, re.M)
        for icon in reversed(icons):
            if icon in names and icon.endswith('.png') and archive.getinfo(icon).file_size < 2 * 1024 * 1024:
                result['image'] = 'data:image/png;base64,' + base64.b64encode(archive.read(icon)).decode()
                break
    return result

if __name__ == '__main__':
    p = argparse.ArgumentParser()
    p.add_argument('--apk', type=Path, required=True)
    p.add_argument('--sdk', type=Path, required=True)
    a = p.parse_args()
    try: print(json.dumps(inspect(a.apk, a.sdk)))
    except Exception as error: raise SystemExit(str(error))
