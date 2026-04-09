#include "kernel/types.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  if(argc != 1){
    fprintf(2, "usage: dumpblocks\n");
    exit(1);
  }

  if(dumpblocks() < 0){
    fprintf(2, "dumpblocks: failed\n");
    exit(1);
  }

  exit(0);
}
