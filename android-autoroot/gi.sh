#!/bin/bash
# Guided install with dialog handling. usage: gi.sh <device-apk>
set -u
ADB="$HOME/Library/Android/sdk/platform-tools/adb"
S=AVXB6R5C19000481
APK="$1"
: > /tmp/gi-pm.log
"$ADB" -s "$S" shell "su -c \"runcon u:r:shell:s0 sh /data/local/tmp/sess-install.sh $APK\"" > /tmp/gi-pm.log 2>&1 &
for i in $(seq 1 45); do
  sleep 2
  if "$ADB" -s "$S" shell "pm list packages ai.mofa.honorautoroot" 2>/dev/null | grep -q ai.mofa; then
    echo "INSTALLED at tick $i"
    "$ADB" -s "$S" shell "dumpsys package ai.mofa.honorautoroot | grep -E 'lastUpdateTime|appId'"
    exit 0
  fi
  OUT=$("$ADB" -s "$S" shell "uiautomator dump /sdcard/w.xml >/dev/null 2>&1; grep -oE 'text=\"[^\"]{1,44}\"' /sdcard/w.xml" 2>/dev/null | tr '\n' '|')
  echo "[$i] $OUT"
  case "$OUT" in
    *"继续安装"*unchecked*) : ;;
  esac
  if echo "$OUT" | grep -q "已了解"; then
    if echo "$OUT" | grep -q 'checked="false"'; then
      echo "-> tick checkbox"
      "$ADB" -s "$S" shell "input tap 126 1930" >/dev/null 2>&1
      continue
    fi
  fi
  if echo "$OUT" | grep -q "继续安装"; then
    echo "-> tap 继续安装"
    "$ADB" -s "$S" shell "input tap 636 2350" >/dev/null 2>&1
    continue
  fi
  if echo "$OUT" | grep -q "安全着想"; then
    echo "-> tap 取消 (safe mode)"
    "$ADB" -s "$S" shell "input tap 370 2520" >/dev/null 2>&1
    continue
  fi
  if echo "$OUT" | grep -qE '\|继续\|'; then
    echo "-> tap 继续 (alert)"
    "$ADB" -s "$S" shell "input tap 900 2520" >/dev/null 2>&1
    continue
  fi
done
echo TIMEOUT; cat /tmp/gi-pm.log; exit 1
