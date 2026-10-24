#ifndef SYNCLOG_H
#define SYNCLOG_H

#include <stdarg.h>
#include <stdio.h>
#include <unistd.h>

/*
 * 同步日志（panic 后现场可查）：
 * - 无额外线程：进程保持单线程（多线程会破坏 slide 竞态，红魔 2/2 panic）；
 * - slogf 直接 write 到 O_SYNC 文件（每次写都落盘，可承受 journal 回滚）；
 * - g_sync_quiet=1 时静音：仅在竞态调用点（slide / direct_pselect_write_*）
 *   内部置位，日志先落盘再进竞态窗口。
 */
extern int g_sync_log_fd;
extern volatile int g_sync_quiet;

static inline void slogf(const char *fmt, ...) {
  if (g_sync_log_fd < 0 || g_sync_quiet)
    return;
  char buf[1024];
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  if (n > 0) {
    ssize_t r = write(g_sync_log_fd, buf, (size_t)n);
    (void)r;
  }
}

#endif
