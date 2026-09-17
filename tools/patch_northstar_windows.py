"""Prepare North Star's ovrport APK for the Windows Nvidia emulator.

Accepts the known North Star ovrport APK, writes an unsigned APK for zipalign
and apksigner. Disables optional audio metrics, Vulkan descriptor templates,
and Quest application space warp; selects multi-pass stereo and restores hand
joint forwarding in ovrport. Requires UnityPy.
Unknown library versions fail closed.
"""
import argparse
import hashlib
import json
import shutil
import struct
import zipfile
from pathlib import Path

LIBRARY = "lib/arm64-v8a/libMetaXRAudioUnity.so"
EXPECTED_SHA256 = "4d789be08b294d00300669a094fabbce7299a7de705969efa1ebadee0879a483"
CALL_OFFSET = 0x13C8C4
UNITY_LIBRARY = "lib/arm64-v8a/libunity.so"
UNITY_SHA256 = "1055d99941122b08a0ccd168de3f2fb91ee85f30c064f6dd3eb97f4563ac7da4"
OVR_LIBRARY = "lib/arm64-v8a/libOVRPlugin.so"
OVR_SHA256 = "2f8025a11178ba05b57661bd8caf2ee154eabbd86c6dcbacd6ef913103a71566"
OCULUS_LIBRARY = "lib/arm64-v8a/libOculusXRPlugin.so"
OCULUS_SHA256 = "73b055fe02045f8ea6ca2605b108e2ea553b36ab88333ad513caad6b72ccd17f"


OPENXR_SHIM_LIBRARY = "lib/arm64-v8a/libopenxr_loader.so"
OPENXR_SHIM_SHA256 = "da9b5977f24241eb6fb790b914bd0d8d70839f59fa826d7f1ea55379bcd65d09"


def patch_hand_forwarding(data):
    if hashlib.sha256(data).hexdigest() != OPENXR_SHIM_SHA256:
        raise ValueError("Unknown ovrport shim; inspect hand-joint forwarding first")
    offset = 0xF870
    expected = bytes.fromhex("fd7bbfa9fd030091a0ffffb0006c2f91fb350094e0031f2afd7bc1a8c0035fd6")
    if data[offset:offset + len(expected)] != expected:
        raise ValueError("Expected ovrport no-op xrLocateHandJointsEXT stub")
    # ovrport 3.4.2 saves the real function at module+0x25d60, but its wrapper
    # merely logs and returns XR_SUCCESS without populating joints. Tail-call
    # the saved runtime function, preserving x0/x1/x2 (tracker/info/locations).
    # If unavailable, return XR_ERROR_FUNCTION_UNSUPPORTED (-7).
    instructions = (0xD00000A8, 0xF946B108, 0xB4000048, 0xD61F0100,
                    0x128000C0, 0xD65F03C0, 0xD503201F, 0xD503201F)
    result = bytearray(data)
    result[offset:offset + 32] = struct.pack("<8I", *instructions)
    return result


def patch_oculus(data):
    if hashlib.sha256(data).hexdigest() != OCULUS_SHA256:
        raise ValueError("Unknown Unity Oculus plugin; inspect SetSpaceWarp first")
    result = bytearray(data)
    offset = 0x100AC
    if result[offset:offset + 4] != bytes.fromhex("e8 17 9f 1a"):
        raise ValueError("Expected SetSpaceWarp cset w8, eq instruction")
    # Keep Unity's ASW flag false even when North Star's quality preset enables
    # it; otherwise URP creates zero-sized motion textures without the extension.
    result[offset:offset + 4] = bytes.fromhex("08 00 80 52") # mov w8, #0
    return result


def patch_settings(bundle):
    import UnityPy
    env = UnityPy.load(bundle)
    matches = [o for o in env.objects if o.assets_file.name == "globalgamemanagers.assets" and o.path_id == 3006]
    if len(matches) != 1:
        raise ValueError("Expected North Star OculusSettings asset")
    obj = matches[0]
    data = obj.get_raw_data()
    if hashlib.sha256(data).hexdigest() != "b9f2918f4462a836d7f913baad813ddb6f1b9dcf8e27ba047c3896a553120f4b":
        raise ValueError("Unknown OculusSettings serialization")
    data = bytearray(data)
    struct.pack_into("<I", data, 0x34, 0) # m_StereoRenderingModeAndroid: MultiPass.
    struct.pack_into("<I", data, 0x68, 0) # SpaceWarp: disabled at initialization.
    obj.set_raw_data(bytes(data))
    return env.file.save(packer="lz4")


def patch_ovr(data):
    if hashlib.sha256(data).hexdigest() != OVR_SHA256:
        raise ValueError("Unknown OVRPlugin library; inspect space-warp detection first")
    extension = b"XR_FB_space_warp\0"
    if data.count(extension) != 1:
        raise ValueError("Expected unique space-warp extension lookup")
    # This runtime does not expose application space warp. ovrport advertises
    # it anyway; prevent OVRPlugin from allocating unsupported motion buffers.
    return data.replace(extension, b"XR_AX_space_warp\0")


def patch_unity(data):
    if hashlib.sha256(data).hexdigest() != UNITY_SHA256:
        raise ValueError("Unknown Unity library; inspect descriptor template fallback first")
    extension = b"VK_KHR_descriptor_update_template"
    if data.count(extension) != 1:
        raise ValueError("Expected unique descriptor template extension name")
    # Unity falls back to vkUpdateDescriptorSets when this optional extension is
    # unavailable. The emulator/native bridge template path crashes the Nvidia
    # driver; the regular descriptor-update path reaches game rendering.
    return data.replace(extension, b"VK_AXR_descriptor_update_template")


def patch_library(data):
    if hashlib.sha256(data).hexdigest() != EXPECTED_SHA256:
        raise ValueError("Unknown Meta audio library; inspect its metrics constructor first")
    if data[CALL_OFFSET:CALL_OFFSET + 4] != bytes.fromhex("00 01 3f d6"):
        raise ValueError("Expected ARM64 blr x8 instruction missing")
    result = bytearray(data)
    # JNI_GetCreatedJavaVMs -> JNI_ERR (-1), following the existing cleanup path.
    # This is inside OVRAudioMetricsDispatcher, not audio rendering/initialization.
    result[CALL_OFFSET:CALL_OFFSET + 4] = bytes.fromhex("00 00 80 12")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    if args.input.resolve() == args.output.resolve():
        parser.error("Input and output must differ")
    with zipfile.ZipFile(args.input) as source:
        patched = patch_library(source.read(LIBRARY))
        unity = patch_unity(source.read(UNITY_LIBRARY))
        ovr = patch_ovr(source.read(OVR_LIBRARY))
        oculus = patch_oculus(source.read(OCULUS_LIBRARY))
        hand_shim = patch_hand_forwarding(source.read(OPENXR_SHIM_LIBRARY))
        with zipfile.ZipFile(args.output, "x") as target:
            for entry in source.infolist():
                if entry.filename.upper().startswith("META-INF/") and entry.filename.upper().endswith((".RSA", ".DSA", ".EC", ".SF", "MANIFEST.MF")):
                    continue
                if entry.filename == LIBRARY:
                    target.writestr(entry, patched)
                elif entry.filename == UNITY_LIBRARY:
                    target.writestr(entry, unity)
                elif entry.filename == OVR_LIBRARY:
                    target.writestr(entry, ovr)
                elif entry.filename == OPENXR_SHIM_LIBRARY:
                    target.writestr(entry, hand_shim)
                elif entry.filename == OCULUS_LIBRARY:
                    target.writestr(entry, oculus)
                elif entry.filename == "assets/bin/Data/data.unity3d":
                    target.writestr(entry, patch_settings(source.read(entry)))
                elif entry.filename == "lib/arm64-v8a/liboverport.config.so":
                    config = json.loads(source.read(entry))
                    config["disable_space_warp"] = 1
                    target.writestr(entry, json.dumps(config, separators=(",", ":")))
                else:
                    with source.open(entry) as reader, target.open(entry, "w") as writer:
                        shutil.copyfileobj(reader, writer, 1024 * 1024)
    print(f"Wrote {args.output}; zipalign and sign before installation")


if __name__ == "__main__":
    main()
