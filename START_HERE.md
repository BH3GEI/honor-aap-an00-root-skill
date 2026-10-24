# 新手复现指南：荣耀 500 AAP-AN00 临时 root + Magisk

按以下步骤执行即可，不需要理解漏洞细节，也不需要手动下载镜像或二进制。

## 1. 准备电脑和手机

电脑需要 macOS 或 Linux，并安装：

- `adb`
- `python3`
- `git`
- USB 数据线

macOS 可执行：

```bash
brew install android-platform-tools python git
```

手机必须匹配：

```text
型号：AAP-AN00
系统：AAP-AN00 10.0.0.175(C00E170R104P3)
内核：6.12.38-android16-5-gfde7767f6ef6-abogki481467632-4k
```

在手机开发者选项中开启 USB 调试，连接电脑并允许调试授权。提前备份数据；漏洞尝试可能导致自动重启。

## 2. 下载仓库

```bash
git clone https://github.com/BH3GEI/honor-aap-an00-root-skill.git
cd honor-aap-an00-root-skill
```

## 3. 设置手机序列号

查看序列号：

```bash
adb devices
```

设置：

```bash
export HONOR_SERIAL=<这里替换成你的序列号>
```

## 4. 一键复现

```bash
./scripts/reproduce.sh
```

脚本自动完成：

1. 从 GitHub Release 下载全部镜像和 Magisk 物料；
2. 对每个文件执行 SHA-256 校验；
3. 检查手机型号、系统版本和内核；
4. 运行一次临时 root；
5. 独立验证 `uid=0`；
6. 安装 Magisk 应用并启动 daemon；
7. 在手机的“下载”目录生成 `honor-root-proof.txt`。

不需要安装 NDK。仓库和 Release 已包含预编译 payload、全部镜像、Magisk 应用和 daemon。

如果 Magisk 应用提示签名不兼容，执行一次：

```bash
./scripts/reproduce.sh --replace-magisk
```

该参数会卸载旧 Magisk 应用后重新安装，应用自身的设置会被清除。

## 5. 检查结果

电脑上执行：

```bash
./scripts/verify_root.sh
./scripts/start_magisk.sh
```

应看到：

```text
uid=0(root) gid=0(root) groups=0(root) context=u:r:kernel:s0
30.7:MAGISK:R
```

进入 root shell：

```bash
adb -s "$HONOR_SERIAL" shell
/data/local/tmp/su
```

使用 Magisk 命令：

```bash
PATH=/data/adb/m:$PATH su -c id
```

手机上打开“文件管理 → 下载 → honor-root-proof.txt”，也能看到 root 验证内容。

## 6. 重启后

临时 root 和 daemon 会在重启后失效，镜像和应用程序仍然保留。再次执行：

```bash
./scripts/reproduce.sh
```

当前没有已验证的独立开机自启动持久化方案。不要手动刷写 boot、init_boot 或 vbmeta。

## 7. 常见问题

### 手机桌面没有 Magisk 图标

确认 `./scripts/reproduce.sh` 已执行到安装 Magisk 应用阶段。也可以打开：

```text
文件管理 → 下载 → honor-root-proof.txt
```

### Magisk 应用打不开

提权后新启动的应用可能遇到系统级 SharedMemory 异常。命令行 root 不受影响。可以先重启并打开 Magisk，再运行 `./scripts/reproduce.sh`，让应用进程在提权前已经存在。

### 下载失败或哈希不匹配

删除对应的 `.part` 临时文件后重新运行脚本。不要继续使用校验失败的文件。

### 只想提权，不安装 Magisk

```bash
./scripts/reproduce.sh --no-magisk
```

## Release 内容

- `payload-vote6b`：带 boot_id 与 tracefs 基址一致性校验的预编译 payload。
- `firmware-v175`：boot、未压缩 Image、init_boot、vendor_boot、dtbo、vbmeta 和 SHA-256。
- `magisk-runtime-v1`：Magisk 应用、daemon 和 SHA-256。

发布页：https://github.com/BH3GEI/honor-aap-an00-root-skill/releases
