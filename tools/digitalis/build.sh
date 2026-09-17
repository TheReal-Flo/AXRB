#!/usr/bin/env bash
# Build on a Linux x86_64 machine; the resulting Android image runs on Windows.
# Never touches an existing AXRB AVD, SDK image, APK, or save directory.
set -euo pipefail
if [[ $# -lt 1 || $# -gt 2 ]]; then
  echo "Usage: $0 /path/to/dedicated/aosp-tree [parallel-jobs]" >&2; exit 2
fi
[[ $(uname -s) == Linux && $(uname -m) == x86_64 ]] || { echo 'Requires Linux x86_64.' >&2; exit 1; }
for command in repo git python3; do command -v "$command" >/dev/null || { echo "Missing $command" >&2; exit 1; }; done
script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
mkdir -p -- "$1"
build_root=$(cd -- "$1" && pwd)
jobs=${2:-8}
[[ $jobs =~ ^[1-9][0-9]*$ ]] || { echo 'jobs must be a positive integer' >&2; exit 2; }
free_kib=$(df -Pk "$build_root" | awk 'NR==2 {print $4}')
if [[ ! -d "$build_root/.repo" && $free_kib -lt 419430400 ]]; then
  echo 'A fresh AOSP checkout/build needs at least 400 GiB free on its filesystem.' >&2; exit 1
fi
cd -- "$build_root"
if [[ ! -d .repo ]]; then
  [[ -z $(find . -mindepth 1 -maxdepth 1 -print -quit) ]] || { echo 'Use an empty dedicated build directory.' >&2; exit 1; }
  repo init -u https://github.com/DigitalisX64/manifest.git \
    -b 3969040d9ea562e8c874a57f545beeaf88975bc9 --depth=1
fi
mkdir -p .repo/local_manifests
cp -- "$script_dir/pinned-projects.xml" .repo/local_manifests/axrb-digitalis-pins.xml
# No --force-sync: do not discard local edits in an existing build tree.
repo sync -c --no-tags -j "$jobs"
repo manifest -r -o axrb-resolved-manifest.xml
# AOSP shell setup expects some unset variables and returns nonzero from tests.
set +u
source build/envsetup.sh
lunch sdk_phone64_x86_64_digitalis-trunk_staging-userdebug
m -j "$jobs"
digitalis/scripts/build-and-package-prebuilts.sh
digitalis/scripts/verify-digitalis-prebuilts.sh
echo "Built image files: $ANDROID_PRODUCT_OUT"
echo "Source lock: $build_root/axrb-resolved-manifest.xml"
