#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

SERIAL="${HONOR_SERIAL:-${ANDROID_SERIAL:-}}"
BUILD=0
DARK_SEC=300
PREBUILT_NAME="preload_honor_vote6b.so"
while [[ $# -gt 0 ]]; do
  case "$1" in
    -s|--serial) SERIAL="$2"; shift 2 ;;
    --build) BUILD=1; shift ;;
    --dark-sec) DARK_SEC="$2"; shift 2 ;;
    -h|--help)
      echo "Usage: $0 -s SERIAL [--build] [--dark-sec 300]"; exit 0 ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
  esac
done
if [[ -z "$SERIAL" ]]; then
  echo "Usage: $0 -s SERIAL [--build] [--dark-sec 300]" >&2; exit 2
fi

./scripts/check_env.sh --serial "$SERIAL" --require-target
if ./scripts/verify_root.sh --serial "$SERIAL" >/dev/null 2>&1; then
  echo "[root] already active; exploit not rerun"
  exec ./scripts/verify_root.sh --serial "$SERIAL"
fi

if [[ ! -f vendor/linuxoid/private_key.json ]]; then
  ./scripts/fetch_upstream.sh
fi
if [[ "$BUILD" -eq 1 ]]; then
  ./scripts/build.sh
  HOST_SO="payload/build/bin/preload.so"
else
  HOST_SO="prebuilt/$PREBUILT_NAME"
fi
test -s "$HOST_SO"
EXPECTED=$(awk -v name="$PREBUILT_NAME" '$2==name {print $1}' prebuilt/SHA256SUMS.txt)
if [[ "$BUILD" -eq 0 ]]; then
  ACTUAL=$(if command -v sha256sum >/dev/null 2>&1; then sha256sum "$HOST_SO" | awk '{print $1}'; else shasum -a 256 "$HOST_SO" | awk '{print $1}'; fi)
  [[ "$ACTUAL" == "$EXPECTED" ]] || { echo "prebuilt hash mismatch" >&2; exit 4; }
fi

mkdir -p runs
LOCK="runs/.exploit.lock"
if ! mkdir "$LOCK" 2>/dev/null; then
  echo "another run_root.sh is active ($LOCK)" >&2; exit 5
fi
trap 'rmdir "$LOCK" 2>/dev/null || true' EXIT

ADB="${ADB:-adb}"
REMOTE_SO="/data/local/tmp/preload_honor.so"
"$ADB" -s "$SERIAL" push "$HOST_SO" "$REMOTE_SO"
STAMP=$(date +%Y%m%d_%H%M%S)
LOG="runs/darkrun_${STAMP}.log"
set +e
python3 scripts/run_exploit.py --serial "$SERIAL" --so "$REMOTE_SO" \
  --dark-sec "$DARK_SEC" --output "runs/exploit_${STAMP}.full.log" 2>&1 | tee "$LOG"
RC=${PIPESTATUS[0]}
set -e
if [[ "$RC" -eq 0 ]]; then
  ./scripts/verify_root.sh --serial "$SERIAL"
else
  echo "run failed rc=$RC; inspect $LOG and the newest /data/local/tmp/sy_*.log" >&2
fi
exit "$RC"
