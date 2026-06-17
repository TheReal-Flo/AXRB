#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APP_DIR="$ROOT/android-runtime-apk"
BUILD_DIR="$ROOT/build-android-runtime-apk"
SDK="${ANDROID_HOME:-/mnt/c/Users/flori/AppData/Local/Android/Sdk}"
NDK="${ANDROID_NDK_HOME:-$SDK/ndk/27.1.12297006}"
BUILD_TOOLS="${ANDROID_BUILD_TOOLS:-$SDK/build-tools/36.0.0}"
ANDROID_PLATFORM="${ANDROID_PLATFORM:-android-29}"
ABI="${ANDROID_ABI:-x86_64}"

AAPT2="$BUILD_TOOLS/aapt2"
D8="$BUILD_TOOLS/d8"
ZIPALIGN="$BUILD_TOOLS/zipalign"
APKSIGNER="$BUILD_TOOLS/apksigner"
ANDROID_JAR="$SDK/platforms/$ANDROID_PLATFORM/android.jar"
TOOLCHAIN="$NDK/build/cmake/android.toolchain.cmake"

for required in "$AAPT2" "$D8" "$ZIPALIGN" "$APKSIGNER" "$ANDROID_JAR" "$TOOLCHAIN"; do
    if [[ ! -e "$required" ]]; then
        echo "Missing required Android tool: $required" >&2
        exit 1
    fi
done

rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"

cmake -S "$ROOT" -B "$BUILD_DIR/runtime" \
    -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
    -DANDROID_ABI="$ABI" \
    -DANDROID_PLATFORM="$ANDROID_PLATFORM" \
    -DAXRB_BUILD_HOST_BRIDGE=OFF \
    -DAXRB_BUILD_TESTS=OFF
cmake --build "$BUILD_DIR/runtime"

mkdir -p "$BUILD_DIR/compiled-res" "$BUILD_DIR/gen" "$BUILD_DIR/classes" "$BUILD_DIR/dex" "$BUILD_DIR/package/lib/$ABI"

"$AAPT2" compile --dir "$APP_DIR/res" -o "$BUILD_DIR/compiled-res"
"$AAPT2" link \
    -I "$ANDROID_JAR" \
    --manifest "$APP_DIR/AndroidManifest.xml" \
    --java "$BUILD_DIR/gen" \
    -o "$BUILD_DIR/base.apk" \
    "$BUILD_DIR"/compiled-res/*.flat

javac \
    -source 8 \
    -target 8 \
    -bootclasspath "$ANDROID_JAR" \
    -d "$BUILD_DIR/classes" \
    $(find "$BUILD_DIR/gen" "$APP_DIR/src" -name '*.java' -print)

"$D8" --min-api 29 --output "$BUILD_DIR/dex" $(find "$BUILD_DIR/classes" -name '*.class' -print)

cp "$BUILD_DIR/base.apk" "$BUILD_DIR/unsigned.apk"
cp "$BUILD_DIR/runtime/android-runtime/libopenxr_runtime.so" "$BUILD_DIR/package/lib/$ABI/"
cp "$BUILD_DIR/dex/classes.dex" "$BUILD_DIR/package/"

jar uf "$BUILD_DIR/unsigned.apk" \
    -C "$BUILD_DIR/package" classes.dex \
    -C "$BUILD_DIR/package" lib

"$ZIPALIGN" -f -p 4 "$BUILD_DIR/unsigned.apk" "$BUILD_DIR/aligned.apk"

KEYSTORE="$BUILD_DIR/debug.keystore"
keytool -genkeypair \
    -keystore "$KEYSTORE" \
    -storepass android \
    -keypass android \
    -alias androiddebugkey \
    -keyalg RSA \
    -keysize 2048 \
    -validity 10000 \
    -dname "CN=Android Debug,O=Android,C=US" >/dev/null

"$APKSIGNER" sign \
    --ks "$KEYSTORE" \
    --ks-pass pass:android \
    --key-pass pass:android \
    --out "$BUILD_DIR/axrb-openxr-runtime-debug.apk" \
    "$BUILD_DIR/aligned.apk"

"$APKSIGNER" verify "$BUILD_DIR/axrb-openxr-runtime-debug.apk"

echo "Built $BUILD_DIR/axrb-openxr-runtime-debug.apk"
