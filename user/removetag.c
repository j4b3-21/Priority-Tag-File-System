#include "kernel/types.h"
#include "user/user.h"

int main(int argc, char *argv[]) {
  if (argc != 3) {
    fprintf(2, "usage: removetag <path> <tag>\n");
    exit(1);
  }

  if (removetag(argv[1], argv[2]) < 0) {
    fprintf(2, "removetag: failed for %s\n", argv[1]);
    exit(1);
  }

  exit(0);
}
