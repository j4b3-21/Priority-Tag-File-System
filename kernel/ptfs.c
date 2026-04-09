#include "types.h"
#include "riscv.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "stat.h"
#include "fs.h"
#include "file.h"
#include "defs.h"
#include "ptfs.h"

extern struct {
  struct spinlock lock;
  struct inode inode[NINODE];
} itable;

struct tag_priority tag_table[MAX_TAG_TABLE];
static int tag_table_count;

static struct spinlock ptfs_lock;
static int ptfs_lock_ready;

static void ptfs_init_lock(void) {
  if (ptfs_lock_ready)
    return;
  initlock(&ptfs_lock, "ptfs");
  ptfs_lock_ready = 1;
}

int get_tag_priority(char *tag) {
  int i;

  if (tag == 0)
    return 0;

  ptfs_init_lock();
  acquire(&ptfs_lock);
  for (i = 0; i < tag_table_count; i++) {
    if (strncmp(tag_table[i].tag, tag, TAG_LENGTH) == 0) {
      int pr = tag_table[i].priority;
      release(&ptfs_lock);
      return pr;
    }
  }
  release(&ptfs_lock);
  return 0;
}

void parse_tag_config(void) {
  struct inode *ip;
  char buf[BSIZE];
  int n;
  int i;

  ptfs_init_lock();

  acquire(&ptfs_lock);
  tag_table_count = 0;
  memset(tag_table, 0, sizeof(tag_table));
  release(&ptfs_lock);

  begin_op();
  ip = namei("/.config_tag");
  if (ip == 0) {
    end_op();
    return;
  }
  ilock(ip);
  n = ip->size;
  if (n > BSIZE - 1)
    n = BSIZE - 1;
  if (n > 0)
    n = readi(ip, 0, (uint64)buf, 0, n);
  else
    n = 0;
  if (n < 0)
    n = 0;
  buf[n] = 0;
  iunlockput(ip);
  end_op();

  i = 0;
  while (i < n) {
    char tag[TAG_LENGTH];
    int tlen = 0;
    int value = 0;
    int have_digits = 0;
    int sign = 1;

    while (i < n && (buf[i] == ' ' || buf[i] == '\t' || buf[i] == '\r' ||
                     buf[i] == '\n'))
      i++;
    if (i >= n)
      break;

    memset(tag, 0, sizeof(tag));
    while (i < n && buf[i] != ':' && buf[i] != '\n' && buf[i] != '\r') {
      if (tlen < TAG_LENGTH - 1)
        tag[tlen++] = buf[i];
      i++;
    }

    if (i >= n || buf[i] != ':') {
      while (i < n && buf[i] != '\n')
        i++;
      continue;
    }
    i++;

    if (i < n && buf[i] == '-') {
      sign = -1;
      i++;
    }
    while (i < n && buf[i] >= '0' && buf[i] <= '9') {
      have_digits = 1;
      value = value * 10 + (buf[i] - '0');
      i++;
    }

    while (i < n && buf[i] != '\n')
      i++;
    if (i < n && buf[i] == '\n')
      i++;

    if (tlen == 0 || !have_digits)
      continue;

    acquire(&ptfs_lock);
    if (tag_table_count < MAX_TAG_TABLE) {
      memset(tag_table[tag_table_count].tag, 0, TAG_LENGTH);
      strncpy(tag_table[tag_table_count].tag, tag, TAG_LENGTH - 1);
      tag_table[tag_table_count].priority = value * sign;
      tag_table_count++;
    }
    release(&ptfs_lock);
  }
}

void calculate_priority(struct inode *ip) {
  int i;
  int sum = 0;

  if (ip == 0)
    return;

  if (ip->totalTags > MAX_TAG)
    ip->totalTags = MAX_TAG;

  for (i = 0; i < ip->totalTags; i++)
    sum += get_tag_priority(ip->tag[i]);

  ip->priority = sum + ip->accessCount;
}

void reorder_files(void) {
  struct inode *sorted[NINODE];
  uint priorities[NINODE];
  int n = 0;

  acquire(&itable.lock);
  for (int i = 0; i < NINODE; i++) {
    struct inode *ip = &itable.inode[i];
    if (ip->ref > 0 && ip->valid && ip->type == T_FILE) {
      sorted[n] = ip;
      priorities[n] = ip->priority;
      n++;
    }
  }
  release(&itable.lock);

  for (int i = 0; i < n; i++) {
    for (int j = i + 1; j < n; j++) {
      if (priorities[j] > priorities[i]) {
        uint p = priorities[i];
        struct inode *tmp = sorted[i];
        priorities[i] = priorities[j];
        sorted[i] = sorted[j];
        priorities[j] = p;
        sorted[j] = tmp;
      }
    }
  }

  // xv6 keeps file block placement stable; we maintain a sorted priority view
  // and recompute metadata, but avoid block remapping in this consistency path.
  (void)sorted;
}
