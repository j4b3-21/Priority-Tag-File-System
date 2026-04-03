#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char *argv[])
{
    if(argc != 3){
        fprintf(2, "Usage: tag <filename> <tagname>\n");
        exit(1);
    }

    char *filename = argv[1];
    char *tagname = argv[2];

    int res = tag(filename, tagname);

    if(res < 0){
        fprintf(2, "Error: tagging failed\n");
        exit(1);
    }

    printf("Tagged %s with %s\n", filename, tagname);
    exit(0);
}