#include "kernel/types.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  if(argc != 3){
    fprintf(2, "usage: addtag <path> <tag>\n");
    exit(1);
  }

  if(addtag(argv[1], argv[2]) < 0){
    fprintf(2, "addtag: failed for %s\n", argv[1]);
    exit(1);
  }

  exit(0);
}

