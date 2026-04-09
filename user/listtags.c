// Starting tag-based file system utilities
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
// Buffer-based approach for storing tag output
