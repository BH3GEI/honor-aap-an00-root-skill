// trace_open.c — ptrace-based syscall tracer for a freshly spawned Android app.
// v2: bitmap-snapshot finder (fast, like probe_stop) + SIGSTOP freeze +
// PTRACE_SEIZE, then syscall-trace every openat/openat2 with path + return.
//
// usage: trace_open <cmdline-substring> [timeout-ms] [--all]
//   --all  also logs non-ashmem/boot_id opens
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <signal.h>
#include <time.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <sys/user.h>
#include <linux/ptrace.h>
#include <linux/elf.h>

#ifndef PTRACE_SEIZE
#define PTRACE_SEIZE 0x4206
#endif
#ifndef PTRACE_INTERRUPT
#define PTRACE_INTERRUPT 0x4207
#endif

static long now_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static int is_numeric(const char *s) {
  if (!*s) return 0;
  for (const char *p = s; *p; p++)
    if (!isdigit((unsigned char)*p)) return 0;
  return 1;
}

static int read_cmdline(int pid, char *buf, size_t len) {
  char path[64];
  snprintf(path, sizeof(path), "/proc/%d/cmdline", pid);
  int fd = open(path, O_RDONLY | O_CLOEXEC);
  if (fd < 0) return -1;
  ssize_t n = read(fd, buf, len - 1);
  close(fd);
  if (n <= 0) return -1;
  for (ssize_t i = 0; i < n; i++)
    if (buf[i] == '\0') buf[i] = ' ';
  buf[n] = '\0';
  return 0;
}

static int read_str(pid_t pid, unsigned long addr, char *buf, size_t len) {
  size_t off = 0;
  while (off + 8 <= len) {
    errno = 0;
    long word = ptrace(PTRACE_PEEKDATA, pid, (void *)(addr + off), NULL);
    if (errno != 0) return -1;
    memcpy(buf + off, &word, 8);
    for (size_t i = 0; i < 8; i++)
      if (buf[off + i] == '\0') return (int)(off + i);
    off += 8;
  }
  if (len) buf[len - 1] = '\0';
  return (int)off;
}

#define MAX_NEW 64
/* Returns number of new pids found; fills out[]. */
static int scan_new_pids(unsigned char *seen, int *out, int max_out) {
  DIR *d = opendir("/proc");
  if (!d) return 0;
  struct dirent *e;
  int n = 0;
  while ((e = readdir(d))) {
    if (!is_numeric(e->d_name)) continue;
    int pid = atoi(e->d_name);
    if (pid <= 0 || pid >= (1 << 21)) continue;
    if (seen[pid]) continue;
    seen[pid] = 1;
    if (pid == getpid() || pid == getppid()) continue;
    if (n < max_out) out[n++] = pid;
  }
  closedir(d);
  return n;
}

static int trace_pid(int pid, long timeout, int log_all, long t0);

/* Trace a child that is ALREADY auto-attached via PTRACE_O_TRACEFORK. */
static void dump_status(int pid, const char *tag, long t0) {
  char path[64], buf[2048];
  snprintf(path, sizeof(path), "/proc/%d/status", pid);
  int fd = open(path, O_RDONLY | O_CLOEXEC);
  if (fd < 0) { printf("[%ldms] %s: /proc/%d/status gone (%s)\n", now_ms() - t0, tag, pid, strerror(errno)); return; }
  ssize_t n = read(fd, buf, sizeof(buf) - 1);
  close(fd);
  if (n <= 0) return;
  buf[n] = 0;
  char *line = buf;
  while (line) {
    char *nl = strchr(line, '\n');
    if (nl) *nl = 0;
    if (!strncmp(line, "State:", 6) || !strncmp(line, "Pid:", 4) ||
        !strncmp(line, "TracerPid:", 10) || !strncmp(line, "SigBlk:", 7) ||
        !strncmp(line, "SigIgn:", 7) || !strncmp(line, "SigCgt:", 7))
      printf("[%ldms] %s: %s\n", now_ms() - t0, tag, line);
    line = nl ? nl + 1 : NULL;
  }
}

static int trace_forked_child(int child, long timeout, int log_all, long t0) {
  int status;
  printf("[%ldms] fork-child=%d (auto-traced)\n", now_ms() - t0, child);
  if (waitpid(child, &status, __WALL) < 0) { printf("child wait failed: %s\n", strerror(errno)); return -1; }
  printf("[%ldms] child first stop status=%x sig=%d event=%d\n", now_ms() - t0, status,
         WSTOPSIG(status), (unsigned)status >> 16);
  dump_status(child, "child-status", t0);
  /* If the child somehow resumed, re-stop it. */
  if (ptrace(PTRACE_SEIZE, child, 0, PTRACE_O_TRACESYSGOOD) == 0) {
    printf("[%ldms] child seized fresh\n", now_ms() - t0);
    ptrace(PTRACE_INTERRUPT, child, 0, 0);
    if (waitpid(child, &status, __WALL) < 0) { printf("re-interrupt wait failed\n"); return -1; }
    printf("[%ldms] child re-stopped status=%x\n", now_ms() - t0, status);
  } else {
    printf("[%ldms] child already tracee (SEIZE errno=%s)\n", now_ms() - t0, strerror(errno));
  }
  return trace_pid(child, timeout, log_all, t0);
}

static int trace_pid(int pid, long timeout, int log_all, long t0) {
  char cmd[256];
  if (read_cmdline(pid, cmd, sizeof(cmd)) != 0) cmd[0] = 0;
  printf("[%ldms] trace pid=%d cmd=%.60s\n", now_ms() - t0, pid, cmd);

  int status;
  char path[512];
  int open_count = 0, hits = 0, prints = 0;
  long tend = now_ms() + timeout;
  /* first resume: fork-attached newborn may still be on the run queue
     (wait_task_inactive fails -> ESRCH); retry until it settles */
  int tries = 0;
  for (;;) {
    if (ptrace(PTRACE_SYSCALL, pid, 0, 0) == 0) break;
    if (errno == ESRCH && tries < 50) { usleep(1000); tries++; continue; }
    printf("[%ldms] SYSCALL(first) err after %d tries: %s\n", now_ms() - t0, tries, strerror(errno));
    return -1;
  }
  if (tries) printf("[%ldms] first resume took %d ms of retries\n", now_ms() - t0, tries);
  for (;;) {
    if (waitpid(pid, &status, __WALL) < 0) {
      printf("[%ldms] wait err: %s\n", now_ms() - t0, strerror(errno));
      break;
    }
    if (WIFEXITED(status)) { printf("[%ldms] exited code=%d\n", now_ms() - t0, WEXITSTATUS(status)); break; }
    if (WIFSIGNALED(status)) { printf("[%ldms] killed sig=%d\n", now_ms() - t0, WTERMSIG(status)); break; }
    if (!WIFSTOPPED(status)) break;
    int sig = WSTOPSIG(status);
    if (prints < 60)
      printf("[%ldms] stop status=%x sig=%d event=%d\n", now_ms() - t0, status, sig, (unsigned)status >> 16);
    if (sig == (SIGTRAP | 0x80)) {
      struct ptrace_syscall_info info;
      memset(&info, 0, sizeof(info));
      if (ptrace(PTRACE_GET_SYSCALL_INFO, pid, (void *)sizeof(info), &info) < 0) {
        printf("GET_SYSCALL_INFO err: %s\n", strerror(errno));
        break;
      }
      if (info.op == PTRACE_SYSCALL_INFO_ENTRY) {
        unsigned long nr = info.entry.nr;
        if (nr == 56 /*openat*/ || nr == 437 /*openat2*/) {
          open_count++;
          if (read_str(pid, info.entry.args[1], path, sizeof(path)) >= 0) {
            int hit = (strstr(path, "ashmem") || strstr(path, "boot_id")) != NULL;
            if (hit) {
              hits++;
              printf("[%ldms] openat(%s)\n", now_ms() - t0, path);
              prints++;
            } else if ((log_all || open_count <= 40) && prints < 400) {
              printf("[%ldms] openat(%s)\n", now_ms() - t0, path);
              prints++;
            }
          }
        }
      } else if (info.op == PTRACE_SYSCALL_INFO_EXIT) {
        if (info.exit.is_error && prints < 400) {
          printf("[%ldms]   -> ret=%lld errno=%lld\n", now_ms() - t0,
                 info.exit.rval, -info.exit.rval);
          prints++;
        }
      }
      sig = 0;
    } else if (sig == SIGTRAP || sig == SIGSTOP) {
      sig = 0;
    }
    /* single resume per stop, with retry (kernel 6.12 returns ESRCH if the
       tracee has not fully settled into TASK_TRACED off the run queue) */
    int rtries = 0;
    for (;;) {
      if (ptrace(PTRACE_SYSCALL, pid, 0, (void *)(long)sig) == 0) break;
      if (errno == ESRCH && rtries < 200) { usleep(500); rtries++; continue; }
      printf("[%ldms] SYSCALL err (rtries=%d): %s\n", now_ms() - t0, rtries, strerror(errno));
      goto out;
    }
    if (now_ms() > tend) { printf("[%ldms] timeout, detaching\n", now_ms() - t0); break; }
  }
out:
  printf("total openat=%d ashmem/boot_id hits=%d\n", open_count, hits);
  ptrace(PTRACE_DETACH, pid, 0, 0);
  return hits;
}

int find_zygote(void) {
  DIR *d = opendir("/proc");
  if (!d) return -1;
  struct dirent *e;
  int found = -1;
  while ((e = readdir(d))) {
    if (!is_numeric(e->d_name)) continue;
    int pid = atoi(e->d_name);
    char cmd[256];
    if (read_cmdline(pid, cmd, sizeof(cmd)) != 0) continue;
    if (strncmp(cmd, "zygote", 6) == 0 && strncmp(cmd, "zygote64\0", 6) != 99) { found = pid; break; }
  }
  closedir(d);
  return found;
}

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: %s <cmdline-substring> [timeout-ms] [--all]\n", argv[0]);
    return 2;
  }
  setvbuf(stdout, NULL, _IOLBF, 0);
  const char *needle = argv[1];
  long timeout = argc > 2 ? atol(argv[2]) : 5000;
  int log_all = (argc > 3 && strcmp(argv[3], "--all") == 0);
  long t0 = now_ms();

  int zp = find_zygote();
  if (zp <= 0) { printf("zygote not found\n"); return 1; }
  printf("[%ldms] zygote pid=%d, seizing with TRACEFORK\n", now_ms() - t0, zp);

  printf("[%ldms] tracer pid=%d\n", now_ms() - t0, getpid());
  if (ptrace(PTRACE_SEIZE, zp, 0, PTRACE_O_TRACEFORK | PTRACE_O_TRACESYSGOOD) < 0) {
    printf("SEIZE zygote failed: %s\n", strerror(errno));
    return 1;
  }

  int child = -1;
  long deadline = t0 + timeout;
  while (now_ms() < deadline) {
    int status;
    pid_t w = waitpid(zp, &status, __WALL);
    if (w < 0) {
      if (errno == EINTR) continue;
      printf("waitpid zygote err: %s\n", strerror(errno));
      break;
    }
    if (!WIFSTOPPED(status)) break;
    int event = (unsigned int)status >> 16;
    if (event == PTRACE_EVENT_FORK) {
      unsigned long msg = 0;
      if (ptrace(PTRACE_GETEVENTMSG, zp, 0, &msg) == 0) { child = (int)msg; }
      printf("[%ldms] fork event -> child=%d\n", now_ms() - t0, child);
      /* stop auto-attaching future forks, keep zygote seized for now */
      ptrace(PTRACE_SETOPTIONS, zp, 0, (void *)(long)PTRACE_O_TRACESYSGOOD);
      break;
    }
    /* other stops: let zygote run on */
    ptrace(PTRACE_CONT, zp, 0, WSTOPSIG(status) == SIGTRAP ? 0 : WSTOPSIG(status));
  }
  /* resume zygote so the (v)fork completes; detach later */
  ptrace(PTRACE_SYSCALL, zp, 0, 0);
  printf("[%ldms] zygote resumed (still seized)\n", now_ms() - t0);
  if (child <= 0) { printf("no fork caught\n"); return 1; }

  trace_forked_child(child, timeout > 8000 ? 8000 : timeout, log_all, t0);
  return 0;
}
