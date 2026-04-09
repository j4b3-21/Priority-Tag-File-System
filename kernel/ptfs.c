#include "ptfs.h"
#include "buf.h"
#include "defs.h"
#include "file.h"
#include "fs.h"
#include "param.h"
#include "riscv.h"
#include "sleeplock.h"
#include "spinlock.h"
#include "stat.h"
#include "fs.h"
#include "buf.h"
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

static void
ptfs_init_lock(void)
{
  if(ptfs_lock_ready)
    return;
  initlock(&ptfs_lock, "ptfs");
  ptfs_lock_ready = 1;
}

int
get_tag_priority(char *tag)
{
  int i;

  if(tag == 0)
    return 0;

  ptfs_init_lock();
  acquire(&ptfs_lock);
  for(i = 0; i < tag_table_count; i++){
    if(strncmp(tag_table[i].tag, tag, TAG_LENGTH) == 0){
      int pr = tag_table[i].priority;
      release(&ptfs_lock);
      return pr;
    }
  }
  release(&ptfs_lock);
  return 0;
}

void
parse_tag_config(void)
{
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
  if(ip == 0){
    end_op();
    return;
  }

  ilock(ip);
  n = ip->size;
  if(n > BSIZE - 1)
    n = BSIZE - 1;
  if(n > 0)
    n = readi(ip, 0, (uint64)buf, 0, n);
  else
    n = 0;
  if(n < 0)
    n = 0;
  buf[n] = 0;
  iunlockput(ip);
  end_op();

  i = 0;
  while(i < n){
    char tag[TAG_LENGTH];
    int tlen = 0;
    int value = 0;
    int have_digits = 0;
    int sign = 1;

    while(i < n && (buf[i] == ' ' || buf[i] == '\t' || buf[i] == '\r' || buf[i] == '\n'))
      i++;
    if(i >= n)
      break;

    memset(tag, 0, sizeof(tag));
    while(i < n && buf[i] != ':' && buf[i] != '\n' && buf[i] != '\r'){
      if(tlen < TAG_LENGTH - 1)
        tag[tlen++] = buf[i];
      i++;
    }

    if(i >= n || buf[i] != ':'){
      while(i < n && buf[i] != '\n')
        i++;
      continue;
    }
    i++;

    if(i < n && buf[i] == '-'){
      sign = -1;
      i++;
    }
    while(i < n && buf[i] >= '0' && buf[i] <= '9'){
      have_digits = 1;
      value = value * 10 + (buf[i] - '0');
      i++;
    }

    while(i < n && buf[i] != '\n')
      i++;
    if(i < n && buf[i] == '\n')
      i++;

    if(tlen == 0 || !have_digits)
      continue;

    acquire(&ptfs_lock);
    if(tag_table_count < MAX_TAG_TABLE){
      memset(tag_table[tag_table_count].tag, 0, TAG_LENGTH);
      strncpy(tag_table[tag_table_count].tag, tag, TAG_LENGTH - 1);
      tag_table[tag_table_count].priority = value * sign;
      tag_table_count++;
    }
    release(&ptfs_lock);
  }
}

void
calculate_priority(struct inode *ip)
{
  int i;
  int sum = PTFS_BASE_PRIORITY;

  if(ip == 0)
    return;

  if(ip->totalTags > MAX_TAG)
    ip->totalTags = MAX_TAG;

  for(i = 0; i < ip->totalTags; i++)
    sum += get_tag_priority(ip->tag[i]);

  sum += ip->accessCount;
  if(sum < 0)
    sum = 0;

  ip->priority = sum;
}

static int
is_single_block_file(struct inode *ip)
{
  if(ip == 0 || ip->type != T_FILE)
    return 0;
  if(ip->size > BSIZE)
    return 0;
  if(ip->addrs[0] == 0)
    return 0;
  for(int k = 1; k < NDIRECT; k++){
    if(ip->addrs[k] != 0)
      return 0;
  }
  if(ip->addrs[NDIRECT] != 0)
    return 0;
  return 1;
}

static void
swap_single_block_files(struct inode *a, struct inode *b)
{
  struct inode *first = a;
  struct inode *second = b;
  struct buf *ba;
  struct buf *bb;
  char tmp[BSIZE];
  uint block_a;
  uint block_b;

  if(a == 0 || b == 0 || a == b)
    return;

  if(a->inum > b->inum){
    first = b;
    second = a;
  }

  begin_op();
  ilock(first);
  ilock(second);

  if(!is_single_block_file(first) || !is_single_block_file(second)){
    iunlock(second);
    iunlock(first);
    end_op();
    return;
  }

  block_a = a->addrs[0];
  block_b = b->addrs[0];

  ba = bread(a->dev, block_a);
  bb = bread(b->dev, block_b);
  memmove(tmp, ba->data, BSIZE);
  memmove(ba->data, bb->data, BSIZE);
  memmove(bb->data, tmp, BSIZE);
  log_write(ba);
  log_write(bb);
  brelse(bb);
  brelse(ba);

  a->addrs[0] = block_b;
  b->addrs[0] = block_a;
  iupdate(a);
  iupdate(b);

  iunlock(second);
  iunlock(first);
  end_op();
}

static void
collect_file_inodes(struct inode *dp, struct inode **out, int *n, int max)
{
  struct dirent de;

  if(dp == 0 || dp->type != T_DIR)
    return;

  for(uint off = 0; off < dp->size; off += sizeof(de)){
    char name[DIRSIZ + 1];
    struct inode *child;

    if(*n >= max)
      return;

    if(readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
      panic("collect_file_inodes");
    if(de.inum == 0)
      continue;

    memset(name, 0, sizeof(name));
    memmove(name, de.name, DIRSIZ);
    if(namecmp(name, ".") == 0 || namecmp(name, "..") == 0)
      continue;

    child = dirlookup(dp, name, 0);
    if(child == 0)
      continue;

    ilock(child);
    if(child->type == T_DIR){
      collect_file_inodes(child, out, n, max);
      iunlock(child);
      iput(child);
    } else if(child->type == T_FILE) {
      out[*n] = child;
      (*n)++;
      iunlock(child);
    } else {
      iunlock(child);
      iput(child);
    }
  }
}

void
reorder_files(void)
{
  struct inode *sorted[NINODE];
  struct inode *root;
  uint priorities[NINODE];
  struct inode *movable[NINODE];
  uint target_blocks[NINODE];
  int n = 0;
  int m = 0;

  begin_op();
  root = namei("/");
  if(root == 0){
    end_op();
    return;
  }
  ilock(root);
  if(root->type == T_DIR)
    collect_file_inodes(root, sorted, &n, NINODE);
  iunlockput(root);
  end_op();

  if(n < 2){
    for(int i = 0; i < n; i++)
      iput(sorted[i]);
    return;
  }

  for(int i = 0; i < n; i++)
    priorities[i] = sorted[i]->priority;

  for(int i = 0; i < n; i++){
    for(int j = i + 1; j < n; j++){
      if(priorities[j] > priorities[i]){
        uint p = priorities[i];
        struct inode *tmp = sorted[i];
        priorities[i] = priorities[j];
        sorted[i] = sorted[j];
        priorities[j] = p;
        sorted[j] = tmp;
      }
    }
  }

  for(int i = 0; i < n; i++){
    if(is_single_block_file(sorted[i]))
      movable[m++] = sorted[i];
  }

  if(m < 2){
    for(int i = 0; i < n; i++)
      iput(sorted[i]);
    return;
  }

  // Capture the available first-block slots in ascending order.
  for(int i = 0; i < m; i++)
    target_blocks[i] = movable[i]->addrs[0];
  for(int i = 0; i < m; i++){
    for(int j = i + 1; j < m; j++){
      if(target_blocks[j] < target_blocks[i]){
        uint t = target_blocks[i];
        target_blocks[i] = target_blocks[j];
        target_blocks[j] = t;
      }
    }
  }

  // Highest-priority file should occupy the lowest-numbered block.
  for(int i = 0; i < m; i++){
    if(movable[i]->addrs[0] == target_blocks[i])
      continue;
    for(int j = i + 1; j < m; j++){
      if(movable[j]->addrs[0] == target_blocks[i]){
        swap_single_block_files(movable[i], movable[j]);
        break;
      }
    }
  }

  for(int i = 0; i < n; i++)
    iput(sorted[i]);
}

void
ptfs_reorder_trigger(void)
{
  parse_tag_config();
  reorder_files();
}

int
add_tag(struct inode *ip, char *tag)
{
  int i;

  if(ip == 0 || tag == 0)
    return -1;
  if(!holdingsleep(&ip->lock))
    return -1;
  if(ip->type != T_FILE)
    return -1;

  for(i = 0; i < ip->totalTags; i++){
    if(strncmp(ip->tag[i], tag, TAG_LENGTH) == 0)
      return 0;
  }

  if(ip->totalTags >= MAX_TAG)
    return -1;

  memset(ip->tag[ip->totalTags], 0, TAG_LENGTH);
  strncpy(ip->tag[ip->totalTags], tag, TAG_LENGTH - 1);
  ip->totalTags++;
  calculate_priority(ip);
  iupdate(ip);
  return 0;
}

int
remove_tag(struct inode *ip, char *tag)
{
  int i;

  if(ip == 0 || tag == 0)
    return -1;
  if(!holdingsleep(&ip->lock))
    return -1;
  if(ip->type != T_FILE)
    return -1;

  for(i = 0; i < ip->totalTags; i++){
    if(strncmp(ip->tag[i], tag, TAG_LENGTH) == 0){
      for(int j = i; j + 1 < ip->totalTags; j++)
        memmove(ip->tag[j], ip->tag[j + 1], TAG_LENGTH);
      memset(ip->tag[ip->totalTags - 1], 0, TAG_LENGTH);
      ip->totalTags--;
      calculate_priority(ip);
      iupdate(ip);
      return 0;
    }
  }

  return -1;
}


