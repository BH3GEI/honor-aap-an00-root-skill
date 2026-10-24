# 荣耀 500 AAP-AN00 临时 root 完整复现指南

## 1. 适用范围

本指南仅适用于以下设备与固件：

| 项目 | 必须匹配的值 |
|---|---|
| 型号 | `AAP-AN00` |
| 设备名 | `HNAAP` |
| 系统版本 | `AAP-AN00 10.0.0.175(C00E170R104P3)` |
| 内核 | `6.12.38-android16-5-gfde7767f6ef6-abogki481467632-4k` |
| SoC | Qualcomm SM8850 |

已在该环境实测获得 `uid=0(root)`、`u:r:kernel:s0`，SELinux 为 `Permissive`。

本方案属于运行时临时 root。未修改 boot/init_boot/vbmeta，未解锁 bootloader，重启后不保留 root，也不提供 Magisk 授权界面。

## 2. 风险与准备

1. 漏洞尝试可能导致内核 panic 和手机自动重启，先备份重要数据。
2. 不要同时运行多个发射脚本。
3. 失败后不要自动循环重试，先检查 `runs/` 和设备上的 `sy_*.log`。
4. 不要更换手机型号或固件后直接运行；偏移和调用点表与精确内核绑定。
5. 发射期间存在 ADB 静默窗口，不要插拔 USB 或执行其他 ADB 命令。

## 3. 主机依赖

需要：

- macOS 或 Linux
- `adb`
- `python3`
- `git`
- 仅源码重编译时需要 Android NDK r29；使用预编译 payload 不需要 NDK

macOS 可执行：

```bash
brew install android-platform-tools python git
```

手机开启开发者选项和 USB 调试，连接后完成授权。

## 4. 获取仓库与检查设备

```bash
git clone https://github.com/BH3GEI/honor-aap-an00-root-skill.git
cd honor-aap-an00-root-skill
export HONOR_SERIAL=<你的adb序列号>
./scripts/check_env.sh --require-target
```

必须看到：

```text
[target] exact supported firmware
```

脚本会逐项检查型号、系统版本和内核，不匹配时拒绝继续。

## 5. 方式一：使用已验证的预编译 payload

### 5.1 获取上游签名材料

```bash
./scripts/fetch_upstream.sh
```

该命令将上游项目固定版本克隆到 `vendor/linuxoid/`。发射器仅从本地读取签名材料，不会打印其内容，公开仓库中也没有副本。

`run_root.sh` 在缺少该目录时会自动执行这一步。

### 5.2 发射一次

```bash
./scripts/run_root.sh
```

脚本自动完成：

1. 检查精确设备与固件。
2. 检查当前是否已有可用 root，避免重复发射。
3. 核对预编译文件 SHA-256。
4. 推送 `/data/local/tmp/preload_honor.so`。
5. 完成挑战响应，进入 ADB 静默窗口。
6. 保存完整日志到 `runs/`。
7. 成功报告后使用新的 ADB 命令独立复验。

预编译 payload 的 SHA-256：

```text
d97f486356914a10f3b7059fea9c39ed85e53fb295098da85aa414003d32b741
```

成功结果包含：

```text
direct-root-summary root=1
RESULT: ROOT OK
```

## 6. 方式二：从源码重编译

设置 NDK r29：

```bash
export ANDROID_NDK_HOME=/absolute/path/to/android-ndk-r29
./scripts/build.sh
```

构建产物：

```text
payload/build/bin/preload.so
```

使用新构建的版本发射：

```bash
./scripts/run_root.sh --build
```

仓库已包含目标偏移和 KASLR 调用点表，精确固件的重编译不需要原始 boot 镜像。

## 7. 独立验证

```bash
./scripts/verify_root.sh
```

关键输出：

```text
=== root identity ===
uid=0(root) gid=0(root) groups=0(root) context=u:r:kernel:s0
=== SELinux ===
Permissive
```

普通 `adb shell` 仍显示 `uid=2000(shell)`，这是当前 su 使用方式的正常现象。

## 8. 使用 root

### 8.1 交互式 root shell

```bash
adb -s "$HONOR_SERIAL" shell
/data/local/tmp/su
```

出现以下提示符后，当前命令以 root 运行：

```text
HNAAP:/ #
```

输入 `exit` 离开 root shell。

### 8.2 执行单条 root 命令

```bash
adb -s "$HONOR_SERIAL" shell '/data/local/tmp/su -c "id"'
adb -s "$HONOR_SERIAL" shell '/data/local/tmp/su -c "getenforce"'
```

### 8.3 在手机文件管理中查看证明

```bash
./scripts/write_root_proof.sh
```

然后打开：

```text
文件管理 → 下载 → honor-root-proof.txt
```

该文件包含实际执行 `su -c id` 和 `su -c getenforce` 的输出。

## 9. 重启后的处理

重启会清除当前提权状态和临时 daemon。需要再次使用时：

```bash
./scripts/check_env.sh --require-target
./scripts/run_root.sh
```

脚本会拒绝并发发射，但不会替用户判断失败原因。失败后先阅读故障排查文档。

## 10. 日志与证据

| 路径 | 内容 |
|---|---|
| `runs/darkrun_*.log` | 本次主机发射输出 |
| `runs/exploit_*.full.log` | 设备完整输出 |
| 设备 `/data/local/tmp/sy_*.log` | O_SYNC 同步日志，panic 后用于定位 |
| `evidence/root_success_full.txt` | 成功 vote5 的完整输出 |
| `evidence/root_success_sy.txt` | 成功 vote5 的同步日志 |
| `evidence/root_verification.txt` | 独立 ADB 复验记录 |

## 11. 原理摘要

荣耀该内核使用内联 futex 调用链，waiter 编码需要 `shift=12`。传统 pselect 输入位图只有 15 个全局 qword，无法容纳 13-word waiter。

本实现使用：

- ex 输入位图保存低位字段；
- pselect 返回后的 res_in/res_out 位图保存其余字段；
- `/dev/null` 的读/写就绪状态控制 res 位；
- 零超时 pselect 立即返回；
- 线程在用户态停留，consumer 再触发 `sched_setattr`。

KASLR 使用 400 个 `schedule/io_schedule` 返回站点进行投票，至少两个不同站点给出相同基址后才接受，并由 slide 的 boot_id 回读再次确认。

详细结构见 `honor-aap-an00-root/reference.md`。

## 12. 常见问题

- **手机桌面没有图标**：本方案未安装 Magisk 管理器，使用 `/data/local/tmp/su`。
- **`adb shell id` 仍为 2000**：继续执行 `/data/local/tmp/su -c id` 判断 root。
- **普通 App 没有授权弹窗**：当前没有 App 级授权管理器。
- **重启后 su 失效**：临时 root 已随重启清除，需要重新执行一次。
- **其他报错**：按 `docs/TROUBLESHOOTING.md` 对照日志处理。
