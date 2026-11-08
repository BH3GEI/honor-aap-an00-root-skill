#!/system/bin/sh
# usage: sess-install.sh <apk-path> [extra-pm-install-create-flags...]
APK="$1"; shift
SIZE=$(stat -c %s "$APK")
sid=$(pm install-create -r -i android -S "$SIZE" "$@" | sed -n 's/.*\[\([0-9]*\)\].*/\1/p')
echo "sid=$sid size=$SIZE args=$*"
pm install-write -S "$SIZE" "$sid" base.apk "$APK"
pm install-commit "$sid"
echo "commit_rc=$?"
