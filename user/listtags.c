#include "kernel/types.h"
#include "user/user.h"

#define MAX_TAG_BUF 128

int
main(int argc, char *argv[])
{
  char buf[MAX_TAG_BUF];

  if(argc != 2){
    fprintf(2, "usage: listtags <path>\n");
    exit(1);
  }

  memset(buf, 0, sizeof(buf));
  if(listtags(argv[1], buf, sizeof(buf)) < 0){
    fprintf(2, "listtags: failed for %s\n", argv[1]);
    exit(1);
  }

  printf("%s\n", buf);
  exit(0);
}

