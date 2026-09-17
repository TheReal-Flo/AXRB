"""Build unchanged ARM64 atomic tests and a small benchmark; run via NativeBridge.

Only installs our own diagnostic APK. Never modifies a game or translator.
The same generated APK can be reused for both guests with --skip-build.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import time
import zipfile

ROOT = Path(__file__).resolve().parents[1]
PACKAGE = "com.axrb.mathprobe"


def run(args, timeout=60):
    return subprocess.run([str(a) for a in args], check=True, capture_output=True,
                          text=True, errors="replace", timeout=timeout).stdout.strip()


def build(sdk, directory):
    bt = sdk / "build-tools/36.1.0"
    jdk = Path(os.environ.get("ProgramFiles", "C:/Program Files")) / "Android/Android Studio/jbr/bin"
    clang = sdk / "ndk/29.0.14206865/toolchains/llvm/prebuilt/windows-x86_64/bin/aarch64-linux-android29-clang++.cmd"
    source = ROOT / "tests/android_translation_probe"
    jar = sdk / "platforms/android-34/android.jar"
    for name in ("classes", "dex"):
        (directory / name).mkdir(parents=True, exist_ok=True)
    run([clang, "-O2", "-std=c++17", "-fPIC", "-c",
         "-DJava_com_axrb_mathprobe_MainActivity_run=AtomicRun",
         ROOT / "tests/arm64_atomic_probe.cpp", "-o", directory / "atomic.o"])
    # A second copy reports a bit per failed check, leaving the original gate
    # untouched. This separates correctness failures from unsupported opcodes.
    diagnostic = (ROOT / "tests/arm64_atomic_probe.cpp").read_text()
    diagnostic = diagnostic.replace("int failures=0;", "int failures=0;unsigned mask=0;")
    checks = []
    def annotate(match):
        index = len(checks)
        expression = match.group(1)
        checks.append(expression)
        return "if(" + expression + "){++failures;mask|=" + str(1 << index) + "u;}"
    diagnostic = re.sub(r"failures\s*\+=\s*([^;]+);", annotate, diagnostic)
    diagnostic = diagnostic.replace("failures; value=%u", "failures; value=%u mask=%x")
    diagnostic = diagnostic.replace("mode,failures,value)", "mode,failures,value,mask)")
    (directory / "atomic-diagnostic.cpp").write_text(diagnostic)
    (directory / "atomic-checks.json").write_text(json.dumps(checks, indent=2))
    run([clang, "-O2", "-std=c++17", "-fPIC", "-c",
         "-DJava_com_axrb_mathprobe_MainActivity_run=AtomicDiagnosticRun",
         directory / "atomic-diagnostic.cpp", "-o", directory / "atomic-diagnostic.o"])
    run([clang, "-O2", "-std=c++17", "-shared", "-fPIC", "-static-libstdc++",
         "-Wl,-z,max-page-size=16384", directory / "atomic.o", directory / "atomic-diagnostic.o", source / "benchmark.cpp",
         "-o", directory / "libmathprobe.so"])
    run([jdk / "javac.exe", "-source", "8", "-target", "8", "-classpath", jar,
         "-d", directory / "classes", source / "MainActivity.java"])
    run([bt / "d8.bat", "--lib", jar, "--min-api", "29", "--output", directory / "dex",
         *sorted((directory / "classes").rglob("*.class"))])
    unsigned = directory / "unsigned.apk"
    run([bt / "aapt2.exe", "link", "-I", jar, "--manifest", source / "AndroidManifest.xml",
         "-o", unsigned])
    with zipfile.ZipFile(unsigned, "a", compression=zipfile.ZIP_DEFLATED) as archive:
        archive.write(directory / "dex/classes.dex", "classes.dex")
        archive.write(directory / "libmathprobe.so", "lib/arm64-v8a/libmathprobe.so")
    run([bt / "zipalign.exe", "-f", "-p", "4", unsigned, directory / "aligned.apk"])
    run([bt / "apksigner.bat", "sign", "--ks",
         ROOT / "build-android-runtime-windows-x86_64/debug.keystore",
         "--ks-pass", "pass:android", "--out", directory / "probe.apk", directory / "aligned.apk"])
    run([bt / "apksigner.bat", "verify", directory / "probe.apk"])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk", type=Path, default=Path(os.environ["LOCALAPPDATA"]) / "Android/Sdk")
    parser.add_argument("--serial", required=True)
    parser.add_argument("--skip-build", action="store_true")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--modes", type=int, nargs="+", default=[0, 1, 2, 2, 2, 2, 2])
    parser.add_argument("--apk", type=Path, help="Reuse a particular probe APK for an identical-binary comparison")
    args = parser.parse_args()
    directory = ROOT / "build-android-translation-probe"
    directory.mkdir(exist_ok=True)
    if not args.skip_build and not args.apk:
        build(args.sdk, directory)
    adb = [args.sdk / "platform-tools/adb.exe", "-s", args.serial]
    apk = args.apk or directory / "probe.apk"
    report = {"serial": args.serial, "apk_sha256": hashlib.sha256(apk.read_bytes()).hexdigest(),
              "properties": {}, "runs": []}
    for prop in ("ro.build.fingerprint", "ro.build.version.sdk", "ro.dalvik.vm.native.bridge",
                 "ro.product.cpu.abilist", "ro.boot.qemu.avd_name"):
        report["properties"][prop] = run(adb + ["shell", "getprop", prop])
    report["translator_sha256"] = run(adb + ["shell", "sha256sum", "/system/lib64/libndk_translation.so"])
    report["clocksource"] = run(adb + ["shell", "su", "0", "cat",
                                      "/sys/devices/system/clocksource/clocksource0/current_clocksource"])
    run(adb + ["install", "--no-incremental", "-r", apk])
    for mode in args.modes:
        run(adb + ["shell", "am", "force-stop", PACKAGE])
        run(adb + ["logcat", "-c"])
        started = time.monotonic()
        run(adb + ["shell", "am", "start", "-W", "-n", PACKAGE + "/.MainActivity", "--ei", "mode", str(mode)])
        text = ""
        outcome = "timeout"
        while time.monotonic() - started < 45:
            text = run(adb + ["logcat", "-d", "-v", "brief"], timeout=10)
            if re.search(r"atomic mode \d+: \d+ failures|atomic details:|benchmark ns=", text):
                outcome = "result"
                break
            if "Fatal signal" in text or "FATAL EXCEPTION" in text:
                outcome = "crash"
                break
            time.sleep(0.25)
        lines = [line for line in text.splitlines() if any(word in line for word in
                 ("AXRB.Translation", "Fatal signal", "FATAL EXCEPTION", "libndk_translation", "libmathprobe", "signal 4"))]
        report["runs"].append({"mode": mode, "outcome": outcome, "log": lines})
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=2), encoding="utf-8")
        print(json.dumps(report["runs"][-1]), flush=True)
    run(adb + ["shell", "am", "force-stop", PACKAGE])
    print("Report:", args.output)


if __name__ == "__main__":
    main()
