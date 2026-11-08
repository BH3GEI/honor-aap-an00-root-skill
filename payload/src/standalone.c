#include "common.h"

int main(int argc, char **argv) {
  if (argc != 2 || strcmp(argv[1], "--run") != 0) {
    fprintf(stderr, "usage: %s --run\n", argv[0]);
    return 2;
  }
  return run_exploit(argc - 1, argv + 1);
}
