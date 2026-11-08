#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
SDK="${ANDROID_HOME:?set ANDROID_HOME}"
BT="$SDK/build-tools/35.0.0"
ANDROID_JAR="$SDK/platforms/android-35/android.jar"
BUILD="$ROOT/app/build/manual"
rm -rf "$BUILD"
mkdir -p "$BUILD/generated" "$BUILD/classes" "$BUILD/dex" "$BUILD/staging/lib/arm64-v8a" "$BUILD/out"

"$BT/aapt2" compile --dir "$ROOT/app/src/main/res" -o "$BUILD/compiled-res.zip"
"$BT/aapt2" link -o "$BUILD/base.apk" -I "$ANDROID_JAR" \
  --manifest "$ROOT/app/src/main/AndroidManifest.xml" \
  --java "$BUILD/generated" --min-sdk-version 31 --target-sdk-version 35 \
  "$BUILD/compiled-res.zip"

mapfile -t SOURCES < <(find "$ROOT/app/src/main/java" -name '*.java' ! -name '._*' -type f)
javac -source 8 -target 8 -bootclasspath "$ANDROID_JAR" \
  -d "$BUILD/classes" "${SOURCES[@]}"
"$BT/d8" --release --min-api 31 --lib "$ANDROID_JAR" \
  --output "$BUILD/dex" $(find "$BUILD/classes" -name '*.class' -type f)

cp "$BUILD/base.apk" "$BUILD/unsigned.apk"
zip -q -j "$BUILD/unsigned.apk" "$BUILD/dex/classes.dex"
cp "$ROOT/app/src/main/jniLibs/arm64-v8a/libhonorroot.so" \
  "$BUILD/staging/lib/arm64-v8a/libhonorroot.so"
(cd "$BUILD/staging" && zip -q -0 -r "$BUILD/unsigned.apk" lib)
"$BT/zipalign" -f -p 4 "$BUILD/unsigned.apk" "$BUILD/aligned.apk"

KS="$ROOT/debug.keystore"
if [ ! -f "$KS" ]; then
  keytool -genkeypair -alias androiddebugkey -keyalg RSA -keysize 2048 \
    -validity 10000 -storepass android -keypass android \
    -dname 'CN=Android Debug,O=Android,C=US' -keystore "$KS"
fi
"$BT/apksigner" sign --ks "$KS" \
  --ks-pass pass:android --key-pass pass:android \
  --out "$BUILD/out/honor-autoroot-debug.apk" "$BUILD/aligned.apk"
"$BT/apksigner" verify --verbose "$BUILD/out/honor-autoroot-debug.apk"
sha256sum "$BUILD/out/honor-autoroot-debug.apk"
