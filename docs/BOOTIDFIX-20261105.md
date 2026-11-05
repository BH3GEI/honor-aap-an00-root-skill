# CVE-2026-43499 boot_id 修复与真机复核

日期：2026-11-05。设备与固件与仓库支持目标一致。

## 结果

`preload_honor_vote6b_bootidfix.so` 已完成真机临时 root，独立复核为：

```text
uid=0(root) gid=0(root) groups=0(root) context=u:r:kernel:s0
SELinux Permissive
su daemon 正常运行
```

SHA-256：

```text
ecdcdfcfdc5ea9d5a56286b1f5367315d521d0fe738c03a63ceaa9cebaf658e2
```

完整日志：`evidence/root_bootidfix_success.txt`、`evidence/root_bootidfix_summary.txt`。

## affinity 处理

上一版在 `restore_initial_affinity()` 返回 `EPERM` 时直接放弃后续 root 验证和 su 安装。
现改为记录错误并继续执行。该变化包含在 bootidfix 载荷中。本次运行未复现 affinity 错误。

## boot_id 问题定位

直接读原语使用 `random_table` 的 boot_id `data` 字段作为回读位置。同版内核的初始化表明确包含：

```text
0xffffffc08250b558 -> 0xffffffc082687770 (sysctl_bootid)
```

oracle 读取完成后，该指针被改为 per-CPU `entry_task` 存储。后续读取 boot_id 时，
UUID 包含当前任务指针，因此不同进程和不同读取时刻可以得到不同值。

`evidence/app_bootid_probe.txt` 记录了：

- shell 补节点时使用的 UUID；
- 应用进程内缓存的另一个 UUID；
- probe 再读取得到的第三个 UUID；
- 应用命名空间中按 shell 观察值创建的节点可以打开，但该节点与应用实际路径不同。

这解释了此前单次补节点仍出现 `ENOENT` 的结果。旧恢复补丁把 16 字节 UUID 写到
`ctl_table.data` 及相邻字段，目标对象不正确。

## 修复

在最后一次 oracle 读取之后，payload 将 `data` 恢复为
`canon_addr(SYSCTL_BOOTID_IMAGE)`，其中 `SYSCTL_BOOTID_IMAGE=0xffffffc082687770`。
生成器 `tools/generate_target.py` 也已加入该符号。

shape=1 原语同时把源地址的前 8 字节改为 `target-8`，因此恢复后 UUID 是稳定的新值，
并非原始 boot UUID。随后 `scripts/fix_ashmem_node.sh` 为这个稳定值创建一个节点。

本次恢复后的值为：

```text
50b5b02a-d4ff-ffff-a537-3a9411882d39
```

连续读取一致。创建对应节点后，夸克连续两次冷启动成功；第二次没有新的
`Unable to open ashmem device` 或 `FATAL EXCEPTION` 记录。独立 `su -c id` 仍返回 uid=0。

## 当前边界

- 结果仍为运行时临时 root，重启后失效。
- 原始 boot UUID 没有恢复；当前使用稳定的新 UUID 和匹配节点。
- Bootloader、AVB 和 verity 状态未改变，未刷写任何分区。
- `scripts/run_root.sh` 默认使用 bootidfix 载荷；`prebuilt/SHA256SUMS.txt` 已记录哈希。
