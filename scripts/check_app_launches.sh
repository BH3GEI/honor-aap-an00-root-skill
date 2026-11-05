#!/usr/bin/env bash
set -u
ADB="${ADB:-adb}"
SERIAL="${HONOR_SERIAL:?set HONOR_SERIAL}"
A=("$ADB" -s "$SERIAL")
"${A[@]}" shell logcat -c
packages=$("${A[@]}" shell pm list packages -3 | tr -d '\r' | sed 's/^package://')
pass=0
fail=0
for p in $packages; do
  component=$("${A[@]}" shell cmd package resolve-activity --brief "$p" 2>/dev/null | tr -d '\r' | tail -1)
  case "$component" in
    */*) ;;
    *) echo "NO_LAUNCHER $p ($component)"; fail=$((fail+1)); continue ;;
  esac
  "${A[@]}" shell am force-stop "$p" >/dev/null 2>&1
  out=$("${A[@]}" shell "timeout 30 am start -W -n $component" 2>&1)
  pid=$("${A[@]}" shell pidof "$p" 2>/dev/null | tr -d '\r ')
  if [ -n "$pid" ]; then
    echo "OK $p pid=$pid"
    pass=$((pass+1))
  elif printf '%s' "$out" | grep -q 'Status: ok'; then
    echo "EXITED $p"
    fail=$((fail+1))
  else
    echo "FAIL $p $(printf '%s' "$out" | tr '\n' ' ' | tail -c 240)"
    fail=$((fail+1))
  fi
done
echo "SUMMARY pass=$pass fail=$fail"
echo "--- failure logs ---"
"${A[@]}" shell "logcat -d | grep -E 'FATAL EXCEPTION|Unable to open ashmem device|Failed to execute the transaction' || true"
