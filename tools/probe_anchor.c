/* probe_anchor: 自诱导 sched_blocked_reason 锚点探针
 * 线程 A: O_SYNC 写 + fsync 循环（TASK_UNINTERRUPTIBLE 阻塞，触发 type-110 事件）
 * 主线程: 打印 A 的 tid，睡眠等待外部 atrace 捕获。
 * 用法: atrace sched --async_start; ./probe_anchor &; ...; atrace --async_stop
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/syscall.h>
#include <stdatomic.h>

static atomic_int a_tid;
static atomic_int a_stop;

static void *writer_thread(void *arg) {
  (void)arg;
  int tid = (int)syscall(SYS_gettid);
  atomic_store(&a_tid, tid);
  int fd = open("/data/local/tmp/probe_sync.bin", O_WRONLY | O_CREAT | O_SYNC | O_TRUNC, 0600);
  if (fd < 0) {
    perror("open");
    _exit(2);
  }
  static char buf[4096];
  memset(buf, 0x5a, sizeof(buf));
  while (!atomic_load(&a_stop)) {
    if (write(fd, buf, sizeof(buf)) < 0) {
      perror("write");
      break;
    }
    fsync(fd);
    lseek(fd, 0, SEEK_SET);
  }
  close(fd);
  return NULL;
}

int main(void) {
  pthread_t t;
  if (pthread_create(&t, NULL, writer_thread, NULL) != 0) {
    return 1;
  }
  while (!atomic_load(&a_tid)) {
    usleep(1000);
  }
  printf("probe_anchor pid=%d writer_tid=%d\n", getpid(), atomic_load(&a_tid));
  fflush(stdout);
  sleep(6);
  atomic_store(&a_stop, 1);
  pthread_join(t, NULL);
  return 0;
}
