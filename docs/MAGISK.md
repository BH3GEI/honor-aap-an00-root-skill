# Honor AAP-AN00：Magisk 运行时安装与使用

## 当前实测状态

- Magisk v30.7 daemon 可以在临时 root 后启动，`magisk -v` 返回 `30.7:MAGISK:R`，`magisk -V` 返回 `30700`。
- `su` applet 可用，实测 `uid=0(root)`。
- 桌面已安装 Magisk 应用。应用内加入状态刷新、动态 root 判定和菜单重载逻辑。
- 提权后新启动的 Android 进程仍可能出现 `SharedMemory`/ashmem 异常。需要界面时，先在正常系统状态启动 Magisk，再进行提权；命令行不依赖该界面。

本方案提供运行时 Magisk，不提供已验证的 boot 持久化。

## 发布物料

GitHub Release `magisk-runtime-v1`：

- `Magisk-refresh.apk`：带状态刷新逻辑的 ARM64 Android 应用。
- `magisk`：ARM64 daemon/client，MAGISKTMP 已改为 `/data/adb/m`。
- `SHA256SUMS.txt`：文件校验值。

应用签名与官方版本不同。若系统提示签名不兼容，先卸载旧应用再安装；这会删除 Magisk 应用自身的设置，不影响 `/data/adb` 中的 daemon 文件。

## 一键安装

完成临时 root 后，也可以直接使用一键脚本下载并校验全部物料，然后安装应用和 daemon：

```bash
./scripts/reproduce.sh
```

脚本会下载 `firmware-v175` 与 `magisk-runtime-v1` 中的全部文件并检查 SHA-256。若需覆盖不同签名的旧应用，使用 `./scripts/reproduce.sh --replace-magisk`。

## 安装步骤

### 1. 先获得临时 root

```bash
export HONOR_SERIAL=<adb-serial>
./scripts/check_env.sh --require-target
./scripts/run_root.sh
```

需要界面时，在运行上一步之前先打开 Magisk 应用并保持其进程存在。

### 2. 安装应用

```bash
./scripts/install_magisk_app.sh -s "$HONOR_SERIAL" --apk /path/to/Magisk-refresh.apk
```

脚本会临时把 `secure/adb_install_need_confirm` 改为 0，安装结束后恢复为 1。

### 3. 部署 daemon

将 Release 中的 `magisk` 推送到设备，并安装到 `/data/adb/m`：

```bash
adb -s "$HONOR_SERIAL" push /path/to/magisk /data/local/tmp/magisk-bin
adb -s "$HONOR_SERIAL" shell '/data/local/tmp/su -c "mkdir -p /data/adb/m/.magisk/device && cp /data/local/tmp/magisk-bin /data/adb/m/magisk.new && mv /data/adb/m/magisk.new /data/adb/m/magisk && chmod 755 /data/adb/m/magisk"'
```

### 4. 启动并验证

```bash
./scripts/start_magisk.sh -s "$HONOR_SERIAL"
```

脚本自动完成：

- 恢复 `/data/adb` 与应用目录的访问权限；
- 启动 daemon；
- 创建 `su` applet；
- 为 Magisk 应用和 adb shell 写入允许策略；
- 如果 Magisk 进程存在，将 `/data/adb/m` 绑定到其 `/debug_ramdisk`；
- 输出版本和 `su -c id` 结果。

预期输出包含：

```text
30.7:MAGISK:R
uid=0(root) gid=0(root) groups=0(root) context=u:r:kernel:s0
```

## 重建应用

只有在需要修改应用时才执行以下步骤。

```bash
git clone --depth 1 --branch v30.7 https://github.com/topjohnwu/Magisk.git magisk-src
cd magisk-src
git apply /path/to/honor-aap-an00-root-skill/patches/magisk-refresh.patch

python3 /path/to/honor-aap-an00-root-skill/scripts/patch_magisk_binary.py \
  /path/to/official/libmagisk.so /tmp/libmagisk-patched.so
python3 /path/to/honor-aap-an00-root-skill/scripts/prepare_magisk_native.py \
  --apk /path/to/Magisk-v30.7.apk --source "$PWD" \
  --patched-magisk /tmp/libmagisk-patched.so

export JAVA_HOME=/path/to/jdk21
cd app
./gradlew :apk:assembleDebug
```

`prepare_magisk_native.py` 从官方 APK 提取各 ABI 的原生二进制，避免单独构建 Rust/C++ 原生项目。输出为 `app/apk/build/outputs/apk/debug/apk-debug.apk`。

## 重启与持久化边界

重启后需要重新执行：

```bash
./scripts/run_root.sh
./scripts/start_magisk.sh
```

`/data/adb/m` 中的二进制和应用数据可以保留，但 stock init 不会在该目录执行启动脚本。`/data` 的 `nosuid` 挂载也排除了 setuid 自启动方式。当前没有已验证的独立开机自启动持久化方案；修改 boot/init_boot/vbmeta 在锁定 AVB 下不能直接用于持久化。
