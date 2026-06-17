#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'EOF'
Usage: tools/waydroid_vulkan_probe.sh [--install-wsl-dzn] [--configure-waydroid-dri DEVICE]

Diagnose whether Waydroid can expose hardware Vulkan to Android.

Options:
  --install-wsl-dzn
      Experimental WSL-only helper. Adds kisak-mesa PPA and upgrades Mesa so
      Linux Vulkan apps can use Mesa Dozen (dzn) over /dev/dxg when available.
      This does not by itself make Android/Waydroid use hardware Vulkan.

  --configure-waydroid-dri DEVICE
      Native Linux helper. Sets waydroid.cfg drm_device to a render node such
      as /dev/dri/renderD128, then runs "waydroid upgrade -o".

The script is intentionally conservative: it reports Android SwiftShader/CPU
Vulkan as a blocker, because that will not satisfy Unreal/VR APKs that require
a real Vulkan physical device.
EOF
}

want_install_wsl_dzn=0
configure_dri=""

while [ "$#" -gt 0 ]; do
    case "$1" in
        --install-wsl-dzn)
            want_install_wsl_dzn=1
            shift
            ;;
        --configure-waydroid-dri)
            if [ "$#" -lt 2 ]; then
                echo "missing DEVICE after --configure-waydroid-dri" >&2
                exit 2
            fi
            configure_dri="$2"
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "unknown argument: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

have() {
    command -v "$1" >/dev/null 2>&1
}

section() {
    printf '\n== %s ==\n' "$1"
}

run_or_true() {
    "$@" 2>&1 || true
}

is_wsl=0
if grep -qiE 'microsoft|wsl' /proc/version 2>/dev/null; then
    is_wsl=1
fi

section "Host"
if [ "$is_wsl" -eq 1 ]; then
    echo "environment: WSL"
else
    echo "environment: native/non-WSL Linux"
fi
echo "kernel: $(uname -a)"
echo "devices:"
run_or_true ls -l /dev/dri /dev/dxg

section "Host Vulkan"
if have vulkaninfo; then
    run_or_true vulkaninfo --summary | sed -n '1,120p'
else
    echo "vulkaninfo: not installed"
fi

echo
echo "Vulkan ICD files:"
run_or_true sh -c 'ls -1 /usr/share/vulkan/icd.d /etc/vulkan/icd.d 2>/dev/null'

echo
echo "Dozen/dzn files:"
if find /usr/lib /usr/share/vulkan -iname '*dzn*' -o -iname '*dozen*' 2>/dev/null | sort | sed -n '1,80p' | grep -q .; then
    find /usr/lib /usr/share/vulkan -iname '*dzn*' -o -iname '*dozen*' 2>/dev/null | sort | sed -n '1,80p'
else
    echo "not found"
fi

if [ "$want_install_wsl_dzn" -eq 1 ]; then
    section "Install WSL Dozen"
    if [ "$is_wsl" -ne 1 ]; then
        echo "--install-wsl-dzn is only intended for WSL." >&2
        exit 2
    fi
    if ! have sudo; then
        echo "sudo is required for --install-wsl-dzn." >&2
        exit 2
    fi
    sudo apt-get update
    sudo apt-get install -y software-properties-common vulkan-tools mesa-utils
    sudo add-apt-repository -y ppa:kisak/kisak-mesa
    sudo apt-get update
    sudo apt-get upgrade -y
fi

if [ -n "$configure_dri" ]; then
    section "Configure Waydroid DRM Device"
    if [ ! -e "$configure_dri" ]; then
        echo "DRM render node does not exist: $configure_dri" >&2
        exit 2
    fi
    if ! have waydroid; then
        echo "waydroid command not found." >&2
        exit 2
    fi
    sudo sed -i '/^drm_device[[:space:]]*=/d' /var/lib/waydroid/waydroid.cfg
    sudo sed -i "/^\[waydroid\]/a drm_device = $configure_dri" /var/lib/waydroid/waydroid.cfg
    sudo waydroid upgrade -o
    echo "Configured Waydroid drm_device = $configure_dri"
fi

section "Waydroid"
if ! have waydroid; then
    echo "waydroid: not installed"
    exit 0
fi

run_or_true waydroid status

if [ "$(id -u)" -eq 0 ]; then
    wd_prefix=(waydroid)
elif have sudo && sudo -n true >/dev/null 2>&1; then
    wd_prefix=(sudo waydroid)
else
    cat <<'EOF'

Waydroid shell checks need root on this install, but sudo is unavailable or not
usable non-interactively. Re-run this probe as root, for example:

  sudo tools/waydroid_vulkan_probe.sh

From Windows/WSL:

  wsl.exe -u root -d Ubuntu -- bash -lc "/mnt/c/Users/flori/Documents/Coding/androidxr-on-pc/tools/waydroid_vulkan_probe.sh"
EOF
    exit 1
fi

echo
echo "Android graphics properties:"
run_or_true "${wd_prefix[@]}" shell -- getprop ro.hardware.vulkan
run_or_true "${wd_prefix[@]}" shell -- getprop ro.hardware.egl
run_or_true "${wd_prefix[@]}" shell -- getprop ro.hardware.gralloc

echo
echo "Android visible GPU devices:"
run_or_true "${wd_prefix[@]}" shell -- ls -l /dev/dri /dev/dxg

echo
echo "Android Vulkan device summary:"
vkjson="$("${wd_prefix[@]}" shell -- cmd gpu vkjson 2>/dev/null || true)"
if [ -z "$vkjson" ]; then
    echo "cmd gpu vkjson returned no data"
else
    printf '%s\n' "$vkjson" | grep -E '"driverName"|"deviceName"|"deviceType"' | sed -n '1,24p'
fi

if printf '%s\n' "$vkjson" | grep -q 'SwiftShader Device'; then
    cat <<'EOF'

Result: BLOCKED_FOR_ANDROID_HARDWARE_VULKAN
Android is using SwiftShader/CPU Vulkan. Vulkan calls may exist, but this is
not hardware Vulkan and Unreal/VR APKs can reject it before xrCreateSession.
EOF
elif printf '%s\n' "$vkjson" | grep -q '"deviceName"'; then
    cat <<'EOF'

Result: ANDROID_VULKAN_DEVICE_PRESENT
Android reports a non-empty Vulkan physical device list. Verify the device name,
driver, Vulkan version, and required app features before treating this as usable
for VR.
EOF
else
    cat <<'EOF'

Result: NO_ANDROID_VULKAN_DEVICE
Android did not report a Vulkan physical device.
EOF
fi
