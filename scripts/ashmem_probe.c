#define _GNU_SOURCE
/* Directly exercise the libcutils entry point that fails inside apps. */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

extern int ashmem_get_size_region(int fd);
extern int ashmem_valid(int fd);
extern int ashmem_create_region(const char *name, size_t size);

int main(void) {
  char bootid[128] = "";
  FILE *f = fopen("/proc/sys/kernel/random/boot_id", "re");
  if (f) {
    if (!fgets(bootid, sizeof(bootid), f)) bootid[0] = '\0';
    fclose(f);
  }
  bootid[strcspn(bootid, "\r\n \t")] = '\0';
  printf("boot_id=%s\n", bootid);

  int mfd = memfd_create("probe", 0);
  printf("memfd=%d errno=%d\n", mfd, errno);
  if (mfd >= 0) {
    ftruncate(mfd, 4096);
    errno = 0;
    int r = ashmem_valid(mfd);
    printf("ashmem_valid(memfd)=%d errno=%d (%s)\n", r, errno, strerror(errno));
    errno = 0;
    int sz = ashmem_get_size_region(mfd);
    printf("ashmem_get_size_region(memfd)=%d errno=%d (%s)\n", sz, errno,
           strerror(errno));
  }

  char path[256];
  snprintf(path, sizeof(path), "/dev/ashmem%s", bootid);
  int dfd = open(path, O_RDWR | O_CLOEXEC);
  printf("open(%s)=%d errno=%d (%s)\n", path, dfd, errno, strerror(errno));
  if (dfd >= 0) {
    errno = 0;
    int sz = ashmem_get_size_region(dfd);
    printf("ashmem_get_size_region(dev)=%d errno=%d (%s)\n", sz, errno,
           strerror(errno));
  }
  errno = 0;
  int cr = ashmem_create_region("probe_region", 4096);
  printf("ashmem_create_region=%d errno=%d (%s)\n", cr, errno, strerror(errno));
  if (cr >= 0) {
    int sz = ashmem_get_size_region(cr);
    printf("  get_size(new region)=%d errno=%d\n", sz, errno);
  }

  int plain = open("/dev/ashmem", O_RDWR | O_CLOEXEC);
  printf("open(/dev/ashmem)=%d errno=%d\n", plain, errno);
  if (plain >= 0) {
    int sz = ashmem_get_size_region(plain);
    printf("ashmem_get_size_region(plain)=%d errno=%d\n", sz, errno);
  }
  return 0;
}
