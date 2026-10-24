# Honor AAP-AN00 Technical Reference

## 1. Target and exploit status

The working target is the Honor 500 AAP-AN00 / HNAAP with Qualcomm SM8850 and the 6.12.38 Android 16 GKI kernel. The bootloader remains locked. The included solution is a runtime credential and SELinux modification chain with a temporary su daemon.

The successful vote5 run proved all of the following in one boot:

1. Runtime KASLR calibration from tracefs.
2. Independent slide confirmation through `random_boot_id_data`.
3. Direct reads of `per_cpu_offset` and `__entry_task`.
4. `real_cred` and `cred` replacement with `init_cred`.
5. SELinux disable and policy reload.
6. Embedded su installation and daemon startup.

The included logs are acceptance evidence, not a guarantee that every attempt succeeds. A previous attempt ended in `AP_S_PANIC`; do not use an automatic retry loop.

## 2. Inline futex geometry and the original capacity failure

On this kernel, `__arm64_sys_futex` calls `futex_wait_requeue_pi` directly; `do_futex` is absent from the active call chain. Correcting the stack-depth calculation gives:

```text
PSELECT_WAITER_WORD_SHIFT = 12
```

The fake `rt_mutex_waiter` spans 13 qwords. With `nfds=320`, `core_sys_select` uses five 64-bit words per fd set and 15 words across `in`, `out`, and `ex`. A conventional input-only encoding needs global words 12 through 24, exceeding that capacity.

Increasing `nfds` moves the fd sets to an allocation path and no longer preserves the required stack layout. The working implementation instead uses both input and result bitmap storage in the same stack region.

## 3. res-encode pselect layout

For waiter word `w`, global word `g = 12 + w` is encoded as follows:

| Waiter word | Field | Global word | Storage |
|---|---|---:|---|
| w0 | tree parent / value | 12 | ex input word 2 |
| w1 | tree right | 13 | zero |
| w2 | tree left / target | 14 | ex input word 4 |
| w3 | tree priority | 15 | res_in word 0 |
| w4 | tree deadline | 16 | zero |
| w5 | PI parent | 17 | res_in word 2 |
| w6 | PI right | 18 | zero |
| w7 | PI left / target | 19 | res_in word 4 |
| w8 | PI priority | 20 | res_out word 0 |
| w9 | PI deadline | 21 | zero |
| w10 | task | 22 | res_out word 2 |
| w11 | lock | 23 | res_out word 3 |
| w12 | wake state = 3 | 24 | res_out word 4 |

The implementation sets each required input bit and assigns an independent open `/dev/null` descriptor to that fd number. `/dev/null` reports read and write readiness, so `res_in` and `res_out` reproduce the controlled input bitmaps after `do_select` evaluates them.

### Timing

1. Build `in`, `out`, and `ex` using res encoding.
2. Call `pselect` with a zero timeout so it returns immediately after producing result bitmaps.
3. Keep the waiter thread in a userspace atomic loop. This preserves the encoded kernel-stack contents.
4. Set the consumer flag only after `pselect` returns.
5. The consumer issues `sched_setattr` against the parked thread, driving the futex/rt-mutex walk while the fake waiter is present.

Both slide and direct paths use this arrangement. Earlier direct code used a blocking pselect and could not place `tree_prio` at global word 15.

## 4. KASLR calibration with call-site voting

### Trace source

The shell can enable the sched trace class through `atrace`. Binary `trace_pipe_raw` records for `sched_blocked_reason` contain:

- event ID 110 at byte 0;
- pid at byte 8;
- raw caller pointer at byte 16.

The original fixed background anchors did not appear reliably. The implementation uses the kernel image itself to generate a larger anchor set.

### Generated table

`payload/gen_ktext_sites.py` scans the exact uncompressed kernel image for every direct:

```text
bl schedule
bl io_schedule
```

It stores each architectural return-address offset. Image_175 produces 400 entries:

- 387 `schedule` sites;
- 13 `io_schedule` sites.

For each observed caller `c` and site `s`, a candidate base is `c - s`. A candidate is accepted after two different sites agree on the same 2 MiB-aligned base. Repeats of one site do not constitute independent confirmation.

There is no artificial upper bound on the runtime base. A legal boot produced `0xffffffeeed200000`; an earlier upper-bound check incorrectly rejected it.

### Independent slide confirmation

The slide stage writes the `nfulnl_logger` pointer through the boot_id observation surface. The userland read then derives:

```text
stext = leaked_nfulnl_logger - (SLIDE_NFULNL_LOGGER_IMAGE - KIMAGE_TEXT_BASE)
```

Vote5 produced:

```text
tracefs base       = 0xffffffd7a7200000
boot_id-derived base = 0xffffffd7a7200000
```

This cross-check guards against accepting a false tracefs vote before the direct stage.

## 5. Runtime image addresses and physical aliases

The target header contains two kinds of constants:

- `SLIDE_*_IMAGE`: linked kernel-image virtual addresses;
- `SLIDE_*`: older `P0_DATA_ALIAS_CONST` physical/direct-map aliases derived using a compile-time physical delta.

`rt_image()` accepts only the first kind. Applying it to an already-computed alias performs a second translation and generates invalid addresses. One experiment produced `0xffffff93...` targets and immediately panicked.

The current slide uses `rt_image(SLIDE_*_IMAGE)`. The direct boot_id observation address is also computed from `SLIDE_RANDOM_BOOT_ID_DATA_IMAGE` and checked with `is_kernel_ptr`. The successful chain therefore does not require the old physical-alias delta for these targets.

The groom page, fake lock, fake waiter, fake task, and leaked task pointers remain direct-map addresses leaked at runtime. Address-domain checks are intentionally different for image pointers and direct-map pointers.

## 6. Direct-stage sequence

After slide releases its reclaim generation, the direct stage performs:

1. Read `__per_cpu_offset[direct_root_cpu]` using the boot_id sidecar oracle.
2. Compute `entry_slot = runtime(__entry_task) + percpu_delta`.
3. Read the current task pointer from that slot.
4. Read the current SELinux enforcing state.
5. Replace `task->real_cred` and `task->cred` with runtime `init_cred` addresses.
6. Disable SELinux and reload the policy.
7. Install the embedded su binary to `/data/local/tmp/su` and `/apex/com.android.virt/bin/su`.
8. Start the su daemon on `/data/local/tmp/temp_su.sock`.

The boot_id oracle validates both a sidecar qword and one raw byte before accepting a read. This prevents treating a primitive miss as valid leaked data.

## 7. Build artifacts and determinism

`payload/Makefile` compiles the preload shared object for AArch64 Android API 35. `src/su_blob.S` embeds `payload/embed/su_daemon_aarch64_pie` using `.incbin`.

The supplied `src/target.h` and `src/ktext_sites.h` mean an exact-firmware rebuild does not need the original boot image. `prebuilt/preload_honor_vote5.so` preserves the exact tested artifact with SHA-256:

```text
d97f486356914a10f3b7059fea9c39ed85e53fb295098da85aa414003d32b741
```

## 8. Regenerating the KASLR table

Only perform this when the input is the exact supported uncompressed Image_175:

```bash
python3 payload/gen_ktext_sites.py --image /absolute/path/to/Image_175
```

The generator prints the image SHA-256 and refuses unexpected call-site counts. Expected values:

```text
image SHA-256: 09447d275e3f66d9bd70f68893757464e416d82dc4cbb16c544a3a7b9023cfd0
schedule sites: 387
io_schedule sites: 13
```

`tools/generate_target.py` is included for target-offset research. A new firmware requires independent BTF/disassembly verification of all structure offsets, call-chain geometry, and symbols; regenerating only the KASLR table is insufficient.

When using `vmlinux-to-elf` or `objdump`, account for ThinLTO symbol-layout anomalies. Validate important call sites directly against the raw image bytes rather than trusting a symbol name alone.

## 9. Source map

| File | Responsibility |
|---|---|
| `payload/src/slide.c` | tracefs voting, slide res encoding, boot_id confirmation |
| `payload/src/fops.c` | direct res encoding and userspace-park timing |
| `payload/src/main.c` | direct read/write sequence, root and SELinux finalization |
| `payload/src/pipe.c` | direct child lifecycle and synchronous progress markers |
| `payload/src/util.c` | runtime image translation, groom payload construction |
| `payload/src/target.h` | exact Honor symbols and structure offsets |
| `payload/src/ktext_sites.h` | generated 400-entry call-site table |
| `scripts/run_exploit.py` | challenge response and ADB-dark execution protocol |

## 10. Evidence map

- `evidence/root_success_full.txt`: complete successful payload output.
- `evidence/root_success_sy.txt`: synchronous log retained for panic-sensitive stages.
- `evidence/root_verification.txt`: independent ADB root and SELinux verification.
- `docs/SUCCESS.zh-CN.md`: Chinese success report.

See `docs/TROUBLESHOOTING.md` for failure-specific next actions.
