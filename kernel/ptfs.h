#ifndef _PTFS_H_
#define _PTFS_H_

#include "fs.h"

struct inode;

#define MAX_TAG_TABLE 50
#define PTFS_BASE_PRIORITY 50

struct tag_priority {
  char tag[TAG_LENGTH];
  int priority;
};

int get_tag_priority(char *tag);
void parse_tag_config(void);
void calculate_priority(struct inode *ip);
void reorder_files(void);
void ptfs_reorder_trigger(void);
int add_tag(struct inode *ip, char *tag);
int remove_tag(struct inode *ip, char *tag);

#endif
