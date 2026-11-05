/*
 * probe_stop: freeze a target process the instant it appears, then test the
 * ashmem device inside that process's mount namespace.
 *
 * usage: probe_stop <cmdline-substring> [timeout-ms]
 *        probe_stop '*' <timeout-ms>   -- debug: print every new process
 *
 * Build (NDK r29, static):
 *   aarch64-linux-android35-clang -O2 -static -o probe_stop probe_stop.c
 * Push to /data/local/tmp, then run as root before launching the target app.
 *
 * App processes get their argv[0] only after the zygote child has been set up,
 * so a single cmdline read at discovery time can come back empty or as
 * "zygote64". Every pid that appeared after startup is therefore re-read until
 * its name is final, instead of being discarded after one failed read.
 */
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define ASHMEM_GET_SIZE _IO(0x77, 0x04)
#define MAX_TRACK 4096

static int is_numeric(const char *s) {
  if (!*s) return 0;
  for (const char *p = s; *p; p++)
    if (*p < '0' || *p > '9') return 0;
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

static void dump_file(const char *fmt_path, int pid, const char *label,
                      const char *filter) {
  char path[64];
  snprintf(path, sizeof(path), fmt_path, pid);
  FILE *f = fopen(path, "re");
  if (!f) {
    printf("%s: <unreadable: %s>\n", label, strerror(errno));
    return;
  }
  char line[512];
  while (fgets(line, sizeof(line), f)) {
    line[strcspn(line, "\n")] = '\0';
    if (!filter || strstr(line, filter)) printf("%s: %s\n", label, line);
  }
  fclose(f);
}

/* Run in a child: enter the target's mount namespace, then test ashmem. */
static void test_in_namespace(int pid, unsigned long app_uid) {
  char nspath[64];
  snprintf(nspath, sizeof(nspath), "/proc/%d/ns/mnt", pid);
  int nsfd = open(nspath, O_RDONLY | O_CLOEXEC);
  if (nsfd < 0) {
    printf("setns: open %s failed: %s\n", nspath, strerror(errno));
    _exit(1);
  }
  if (setns(nsfd, CLONE_NEWNS) != 0) {
    printf("setns: failed: %s\n", strerror(errno));
    _exit(1);
  }
  close(nsfd);

  int fd = open("/dev/ashmem", O_RDWR | O_CLOEXEC);
  if (fd < 0) {
    printf("ns-open /dev/ashmem: FAILED errno=%d (%s)\n", errno, strerror(errno));
  } else {
    struct stat st;
    fstat(fd, &st);
    printf("ns-open /dev/ashmem: OK fd=%d rdev=%#llx mode=%#o\n", fd,
           (unsigned long long)st.st_rdev, st.st_mode);
    size_t sz = 0;
    long r = ioctl(fd, ASHMEM_GET_SIZE, &sz);
    printf("ns-ioctl ASHMEM_GET_SIZE: ret=%ld errno=%d (%s)\n", r, errno,
           r < 0 ? strerror(errno) : "ok");
    close(fd);
  }

  /* Repeat the same open with the app's own uid/gid. This separates a path
   * problem from an identity-dependent failure. */
  if (app_uid) {
    if (setresgid(app_uid, app_uid, app_uid) != 0)
      printf("setresgid(%lu): %s\n", app_uid, strerror(errno));
    if (setresuid(app_uid, app_uid, app_uid) != 0)
      printf("setresuid(%lu): %s\n", app_uid, strerror(errno));
    int f2 = open("/dev/ashmem", O_RDWR | O_CLOEXEC);
    if (f2 < 0)
      printf("appuid-open /dev/ashmem: FAILED uid=%lu errno=%d (%s)\n", app_uid,
             errno, strerror(errno));
    else {
      printf("appuid-open /dev/ashmem: OK uid=%lu fd=%d\n", app_uid, f2);
      size_t sz2 = 0;
      long r2 = ioctl(f2, ASHMEM_GET_SIZE, &sz2);
      printf("appuid-ioctl GET_SIZE: ret=%ld errno=%d\n", r2, errno);
      close(f2);
    }
  }

  /* Reproduce the exact libcutils path: "/dev/ashmem" + trimmed boot_id. */
  char bootid[128] = "";
  FILE *bf = fopen("/proc/sys/kernel/random/boot_id", "re");
  if (bf) {
    if (!fgets(bootid, sizeof(bootid), bf)) bootid[0] = '\0';
    fclose(bf);
  }
  bootid[strcspn(bootid, "\r\n \t")] = '\0';
  char devpath[256];
  snprintf(devpath, sizeof(devpath), "/dev/ashmem%s", bootid);
  printf("computed-path: %s\n", devpath);
  int f3 = open(devpath, O_RDWR | O_CLOEXEC);
  printf("computed-open: %s errno=%d (%s)\n", f3 < 0 ? "FAILED" : "OK", errno,
         f3 < 0 ? strerror(errno) : "ok");
  if (f3 >= 0) close(f3);

  DIR *dd = opendir("/dev");
  if (dd) {
    struct dirent *de;
    while ((de = readdir(dd)))
      if (strstr(de->d_name, "ashmem")) printf("ns-/dev entry: %s\n", de->d_name);
    closedir(dd);
  } else {
    printf("ns-opendir /dev failed: %s\n", strerror(errno));
  }

  char p[64];
  snprintf(p, sizeof(p), "/proc/%d/root/dev/ashmem", pid);
  struct stat rs;
  if (stat(p, &rs) == 0)
    printf("ns-stat %s: rdev=%#llx mode=%#o\n", p, (unsigned long long)rs.st_rdev,
           rs.st_mode);
  else
    printf("ns-stat %s: FAILED errno=%d (%s)\n", p, errno, strerror(errno));
  _exit(0);
}

static int track_pid[MAX_TRACK];
static unsigned char track_pending[MAX_TRACK];
static int ntrack;


/* Read the cached ashmem path out of the target's libcutils .bss.
 * The path is a std::string at library_base + 0x150b8 on this build. */
#define CUTILS_PATH_OFF 0x150b8
static unsigned long find_lib_base(int pid, const char *libname) {
  char mp[64];
  snprintf(mp, sizeof(mp), "/proc/%d/maps", pid);
  FILE *f = fopen(mp, "re");
  if (!f) return 0;
  char line[512];
  unsigned long base = 0;
  while (fgets(line, sizeof(line), f)) {
    if (!strstr(line, libname)) continue;
    unsigned long start, off;
    if (sscanf(line, "%lx-%*lx %*4s %lx", &start, &off) == 2 && off == 0) {
      base = start;
      break;
    }
  }
  fclose(f);
  return base;
}

static int read_mem(int pid, unsigned long addr, void *buf, size_t len) {
  char mp[64];
  snprintf(mp, sizeof(mp), "/proc/%d/mem", pid);
  int fd = open(mp, O_RDONLY | O_CLOEXEC);
  if (fd < 0) return -1;
  ssize_t n = pread(fd, buf, len, (off_t)addr);
  close(fd);
  return n == (ssize_t)len ? 0 : -1;
}

static void dump_ashmem_path(int pid) {
  unsigned long base = find_lib_base(pid, "libcutils.so");
  printf("libcutils base: %#lx\n", base);
  if (!base) return;
  unsigned char str[32];
  if (read_mem(pid, base + CUTILS_PATH_OFF, str, sizeof(str)) != 0) {
    printf("path-read: failed (%s)\n", strerror(errno));
    return;
  }
  unsigned char sso = str[0];
  char out[256];
  if (sso & 1) {
    unsigned long size, ptr;
    memcpy(&size, str + 8, 8);
    memcpy(&ptr, str + 16, 8);
    printf("cached-path(long): size=%lu ptr=%#lx\n", size, ptr);
    if (size > 200) size = 200;
    if (read_mem(pid, ptr, out, size + 1) != 0) {
      printf("path-data-read: failed (%s)\n", strerror(errno));
      return;
    }
    out[size] = '\0';
  } else {
    unsigned size = sso >> 1;
    if (size > 22) size = 22;
    memcpy(out, str + 1, size);
    out[size] = '\0';
  }
  printf("cached-path: '%s'\n", out);
  printf("raw32:");
  for (int i = 0; i < 32; i++) printf(" %02x", str[i]);
  printf("\n");
}


/* Scan writable mappings for "/dev/ashmem" strings. Avoids relying on the
 * library base/offset math when reading another process. */
static void dump_dev_strings(int pid) {
  char mp[64];
  snprintf(mp, sizeof(mp), "/proc/%d/maps", pid);
  FILE *f = fopen(mp, "re");
  if (!f) {
    printf("maps: %s\n", strerror(errno));
    return;
  }
  char mem[64];
  snprintf(mem, sizeof(mem), "/proc/%d/mem", pid);
  int mfd = open(mem, O_RDONLY | O_CLOEXEC);
  if (mfd < 0) {
    printf("mem: %s\n", strerror(errno));
    fclose(f);
    return;
  }
  static unsigned char buf[65536 + 64];
  char line[512];
  int found = 0;
  while (fgets(line, sizeof(line), f) && found < 12) {
    unsigned long a, b;
    char perms[8];
    if (sscanf(line, "%lx-%lx %7s", &a, &b, perms) != 3) continue;
    if (perms[0] != 'r' || perms[1] != 'w') continue;
    unsigned long size = b - a;
    if (size == 0 || size > (16UL << 20)) continue;
    for (unsigned long off = 0; off < size; off += 65536) {
      size_t want = size - off > 65536 ? 65536 : size - off;
      ssize_t n = pread(mfd, buf, want + 32 > sizeof(buf) ? 65536 : want + 32,
                        (off_t)(a + off));
      if (n <= 0) break;
      for (ssize_t i = 0; i + 12 < n; i++) {
        if (memcmp(buf + i, "/dev/ashmem", 11) != 0) continue;
        char out[160];
        int k = 0;
        while (k < 120 && i + k < n && buf[i + k] >= 0x20 && buf[i + k] < 0x7f) {
          out[k] = buf[i + k];
          k++;
        }
        out[k] = '\0';
        printf("mem[%#lx+%#lx]: '%s'\n", a, off + (unsigned long)i, out);
        found++;
        if (found >= 12) break;
      }
    }
  }
  if (!found) printf("mem: no /dev/ashmem string found\n");
  close(mfd);
  fclose(f);
}

static void freeze_and_report(int pid, const char *cmd, unsigned long app_uid) {
  if (kill(pid, SIGSTOP) != 0) return;
  printf("=== FROZEN pid=%d cmd=%s\n", pid, cmd);
  dump_ashmem_path(pid);
  dump_dev_strings(pid);
  dump_file("/proc/%d/status", pid, "status", NULL);
  dump_file("/proc/%d/attr/current", pid, "domain", NULL);
  dump_file("/proc/%d/mountinfo", pid, "mount", " /dev ");
  dump_file("/proc/%d/mountinfo", pid, "mount-ashmem", "ashmem");

  pid_t child = fork();
  if (child == 0) test_in_namespace(pid, app_uid);
  int st = 0;
  waitpid(child, &st, 0);

  kill(pid, SIGKILL);
  printf("=== DONE\n");
}

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: %s <cmdline-substring> [timeout-ms]\n", argv[0]);
    return 2;
  }
  setvbuf(stdout, NULL, _IOLBF, 0);
  if (strcmp(argv[1], "--dump") == 0 && argc > 2) {
    dump_ashmem_path(atoi(argv[2]));
    return 0;
  }
  const char *needle = argv[1];
  int timeout_ms = argc > 2 ? atoi(argv[2]) : 20000;
  int delay_ms = argc > 3 ? atoi(argv[3]) : 0;

  /* Record the pids that already exist so the hot loop only inspects new ones. */
  static unsigned char seen[1 << 21];
  int max_seen = 0;
  DIR *d = opendir("/proc");
  if (!d) return 1;
  struct dirent *e;
  while ((e = readdir(d))) {
    if (!is_numeric(e->d_name)) continue;
    int pid = atoi(e->d_name);
    if (pid > 0 && pid < (1 << 21)) {
      seen[pid] = 1;
      if (pid > max_seen) max_seen = pid;
    }
  }
  closedir(d);

  struct timespec start, now;
  clock_gettime(CLOCK_MONOTONIC, &start);
  long scanned = 0;

  for (;;) {
    clock_gettime(CLOCK_MONOTONIC, &now);
    long elapsed = (now.tv_sec - start.tv_sec) * 1000L +
                   (now.tv_nsec - start.tv_nsec) / 1000000L;
    if (elapsed > timeout_ms) {
      printf("NO MATCH after %ld ms, %ld scans, max_seen=%d, tracked=%d\n",
             elapsed, scanned, max_seen, ntrack);
      return 1;
    }

    d = opendir("/proc");
    if (!d) continue;
    while ((e = readdir(d))) {
      if (!is_numeric(e->d_name)) continue;
      int pid = atoi(e->d_name);
      if (pid <= 0 || pid >= (1 << 21)) continue;
      if (seen[pid]) continue;
      seen[pid] = 1;
      if (pid > max_seen) max_seen = pid;
      if (ntrack < MAX_TRACK) {
        track_pid[ntrack] = pid;
        track_pending[ntrack] = 1;
        ntrack++;
      }
    }
    closedir(d);

    for (int i = 0; i < ntrack; i++) {
      if (!track_pending[i]) continue;
      int pid = track_pid[i];
      char cmd[1024];
      if (read_cmdline(pid, cmd, sizeof(cmd)) != 0) {
        char pp[64];
        snprintf(pp, sizeof(pp), "/proc/%d", pid);
        if (access(pp, F_OK) != 0) track_pending[i] = 0; /* exited */
        continue;
      }
      if (needle[0] == '*') {
        printf("NEW pid=%d cmd=%.120s\n", pid, cmd);
        track_pending[i] = 0;
        continue;
      }
      if (strncmp(cmd, needle, strlen(needle)) != 0) {
        /* A zygote child still carries its placeholder name; keep polling it. */
        if (!strstr(cmd, "zygote") && !strstr(cmd, "app_process") && cmd[0])
          track_pending[i] = 0;
        continue;
      }

      /* A freshly forked zygote child still runs as uid 0 in the zygote
       * domain. Wait until it has been specialized into the app uid/domain,
       * otherwise the namespace test describes the wrong stage. */
      char uidline[256] = "";
      char upath[64];
      snprintf(upath, sizeof(upath), "/proc/%d/status", pid);
      FILE *uf = fopen(upath, "re");
      if (uf) {
        char l[256];
        while (fgets(l, sizeof(l), uf)) {
          if (strncmp(l, "Uid:", 4) == 0) {
            snprintf(uidline, sizeof(uidline), "%s", l);
            break;
          }
        }
        fclose(uf);
      }
      unsigned long uid0 = 0;
      if (sscanf(uidline, "Uid: %lu", &uid0) != 1 || uid0 == 0) continue;

      if (delay_ms > 0) {
        /* Let it run up to the point where libcutils has built the path. */
        struct timespec ts = {delay_ms / 1000, (delay_ms % 1000) * 1000000L};
        nanosleep(&ts, NULL);
      }
      freeze_and_report(pid, cmd, uid0);
      return 0;
    }
    scanned++;
  }
}
