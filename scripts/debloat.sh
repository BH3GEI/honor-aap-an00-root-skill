#!/system/bin/sh
# Conservative Honor AAP-AN00 debloat. All removals are `pm uninstall -k --user 0`:
# APK and data stay on disk; restore anytime with `pm install-existing <pkg>`.
for p in \
  com.hihonor.appmarket \
  com.hihonor.vmall \
  com.hihonor.gamecenter \
  com.honor.quickgamecenter \
  com.hihonor.quickgame \
  com.hihonor.browserhomepage \
  com.hihonor.suggestion \
  com.hihonor.tips \
  com.hihonor.magazine \
  com.hihonor.youku.video \
  com.hihonor.cloudmusic \
  com.hihonor.dz.reader \
  com.baidu.swan \
  com.honor.yoyoappsug \
  com.honor.yoyocards \
  com.microsoft.appmanager \
  com.hihonor.redteamobile.roaming \
  com.hihonor.instantonline \
  com.hihonor.email \
  com.tencent.nrc \
; do
  r=$(pm uninstall -k --user 0 "$p" 2>&1 | tail -1)
  echo "$p -> $r"
done
