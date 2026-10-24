#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

if [[ -z "${ANDROID_NDK_HOME:-}${ANDROID_NDK_ROOT:-}" ]]; then
  if ! compgen -G "$HOME/android-ndk-cache/android-ndk-r29/toolchains/llvm/prebuilt/*/bin/aarch64-linux-android35-clang" >/dev/null &&
     ! compgen -G "$HOME/Library/Android/sdk/ndk/*/toolchains/llvm/prebuilt/*/bin/aarch64-linux-android35-clang" >/dev/null; then
    cat >&2 <<'MSG'
Android NDK clang was not found. Set ANDROID_NDK_HOME to an NDK r29 installation.
The prebuilt payload does not require an NDK; use scripts/run_root.sh without --build.
MSG
    exit 2
  fi
fi

make -C payload preload
PAYLOAD="payload/build/bin/preload.so"
test -s "$PAYLOAD"
file "$PAYLOAD"
if command -v sha256sum >/dev/null 2>&1; then
  sha256sum "$PAYLOAD"
else
  shasum -a 256 "$PAYLOAD"
fi
echo "built=$ROOT/$PAYLOAD"
