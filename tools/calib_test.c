/* calib_test: 复现 preload 校准链路的最小台架
 * fork usleep 子进程 → atrace sched → O_NONBLOCK slurp trace_pipe_raw →
 * 按 pid+type110 扫描 → 打印命中数与 caller 值。 */
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <sched.h>
#include "../linuxoid/source/src/ktext_sites.h"

static unsigned char *slurp(const char *path, size_t *out_len) {
  int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
  if (fd < 0) return NULL;
  size_t cap = 1 << 22, len = 0, hard = 4 << 20;
  unsigned char *buf = malloc(cap);
  for (;;) {
    if (len >= hard) break;
    ssize_t r = read(fd, buf + len, 65536);
    if (r <= 0) break;
    len += (size_t)r;
  }
  close(fd);
  *out_len = len;
  return buf;
}

int main(void) {
  int tr = system("atrace sched --async_start >/dev/null 2>&1");
  printf("atrace rc=%d\n", tr);
  pid_t child = fork();
  if (child == 0) {
    execl("/system/bin/toybox","sleep","20",(char*)0); _exit(1);
    _exit(0);
  }
  printf("child=%d\n", child);
  for (int round = 0; round < 4; round++) {
    usleep(700000);
    int hits = 0;
    int events = 0;
    int ptrs = 0;
    int site_hits = 0;
    for (int cpu = 0; cpu < 8; cpu++) {
      char path[96];
      snprintf(path, sizeof path, "/sys/kernel/tracing/per_cpu/cpu%d/trace_pipe_raw", cpu);
      size_t len = 0;
      unsigned char *buf = slurp(path, &len);
      if (!buf) { printf("round%d cpu%d slurp NULL errno=%d\n", round, cpu, errno); continue; }
      for (size_t i = 0; i + 32 <= len; i++) {
        if (buf[i] != 110 || buf[i + 1] != 0) { i++; continue; }
        int32_t p; uint64_t c;
        memcpy(&p, buf + i + 8, 4);
        memcpy(&c, buf + i + 16, 8);
        events++;
        if (events <= 4) {
          printf("round%d cpu%d event pid=%d caller=%llx\n", round, cpu,
                 p, (unsigned long long)c);
        }
        if (p == child) {
          hits++;
          if (hits <= 5) printf("round%d cpu%d hit caller=%llx\n", round, cpu, (unsigned long long)c);
        }
        if (c >= 0xffffffc000000000ULL) {
          ptrs++;
          for (size_t s = 0; s < KTEXT_CALL_SITE_COUNT; s++) {
            uint32_t site = KTEXT_CALL_SITES[s];
            if ((site & 0x1fffffu) != ((uint32_t)c & 0x1fffffu)) continue;
            uint64_t base = c - site;
            if (base < 0xffffffc000000000ULL || (base & 0x1fffff)) continue;
            site_hits++;
            if (site_hits <= 4) {
              printf("round%d cpu%d candidate base=%llx site=%x\n", round,
                     cpu, (unsigned long long)base, site);
            }
          }
        }
        i += 32;
      }
      printf("round%d cpu%d len=%zu hits_cum=%d\n", round, cpu, len, hits);
      free(buf);
    }
    printf("round%d events=%d ptrs=%d site_hits=%d child_hits=%d\n",
           round, events, ptrs, site_hits, hits);
  }
  kill(child, SIGKILL);
  waitpid(child, NULL, 0);
  system("atrace --async_stop >/dev/null 2>&1");
  return 0;
}
