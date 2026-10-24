# 荣耀 500（AAP-AN00）CVE-2026-43499 临时 root 成功报告

日期：2026-10-24
设备：荣耀 500 AAP-AN00（SM8850 / canoe，Android 16，内核 6.12.38-android16）
结果：**vote5 已完成运行时临时 root，并通过独立 adb 会话复核。**

## 独立复验

普通 adb shell 保持原身份：

```text
uid=2000(shell) gid=2000(shell) context=u:r:shell:s0
```

通过新安装的 su 客户端执行：

```text
$ /data/local/tmp/su -c id
uid=0(root) gid=0(root) groups=0(root) context=u:r:kernel:s0

$ /data/local/tmp/su -c getenforce
Permissive
```

su 文件与 daemon：

```text
/data/local/tmp/su
/apex/com.android.virt/bin/su
/data/local/tmp/temp_su.sock
```

复验原始记录：`evidence/root_verification.txt`。

## vote5 执行链

1. tracefs `sched_blocked_reason` 多调用点投票得到运行时基址 `0xffffffd7a7200000`。
2. slide 的 boot_id 回读得到 `nfulnl_logger=0xffffffd7a95e2198`，派生基址同为 `0xffffffd7a7200000`。
3. direct 使用 res-encode pselect，成功读取 `per_cpu_offset` 和 `entry_task`。
4. 写入 `init_cred` 后，`uid/euid/gid/egid` 均为 0。
5. SELinux 从 enforcing=1 转为 0，策略重载完成，`id` 上下文为 `u:r:kernel:s0`。
6. embedded su 安装并启动 daemon，`direct-root-summary root=1`，进程退出码 0。

完整输出：`evidence/root_success_full.txt`。
O_SYNC 日志：`evidence/root_success_sy.txt`。

## 成功 payload

```text
prebuilt/preload_honor_vote5.so
/data/local/tmp/preload_honor.so
SHA-256 d97f486356914a10f3b7059fea9c39ed85e53fb295098da85aa414003d32b741
```

关键修正：

- 从精确匹配的 `Image_175` 生成 400 个 `schedule/io_schedule` 返回站点，用两个不同站点的一致投票校准 KASLR。
- 仅对 `SLIDE_*_IMAGE` 原始镜像地址执行运行时换算，消除别名双重换算。
- direct 的 boot_id 观察地址改用运行时 image 地址。
- direct waiter 使用 res-encode：ex 输入字、res_in、res_out 共同承载 shift=12 的 13-word waiter。
- pselect 立即返回，线程在用户态停留，consumer 随后触发 `sched_setattr`。

## 状态边界

当前结果为运行时临时 root。重启后的持久化未实现，锁定 AVB 的状态未改变。
