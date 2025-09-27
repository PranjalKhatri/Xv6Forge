#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "fs.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "file.h"
#include "stat.h"
#include "swapfile.h"


// Defines the block structure written to the swap file.
struct swap_block {
  int pid;
  uint64 va;
  char page_data[PGSIZE];
};


static struct inode *swap_inode;
static struct spinlock swap_lock;
static uint next_swap_offset;

// Must be called after fsinit()
void
swap_init(void)
{
  printf("swap init start\n");
  initlock(&swap_lock, "swap_lock");
  next_swap_offset = 0;

  begin_op();
  swap_inode = namei("/_swap");
  if (swap_inode == 0) {
    swap_inode = ialloc(ROOTDEV, T_FILE);
    if (swap_inode == 0)
      panic("swap_init: ialloc failed");
    ilock(swap_inode);
    iupdate(swap_inode);
    iput(swap_inode);
  }
  end_op();
  printf("swap init done\n");
}

// Writes a block (metadata + page) to the swap file.
// Returns the offset in the file, or -1 on error.
int
swap_out(char *page_data, int pid, uint64 va)
{
  struct swap_block block;
  int offset;

  // Prepare the block on the stack
  block.pid = pid;
  block.va = va;
  memmove(block.page_data, page_data, PGSIZE);

  acquire(&swap_lock);
  offset = next_swap_offset;
  next_swap_offset += sizeof(struct swap_block);
  
  if (next_swap_offset >= MAXFILE * BSIZE) {
    release(&swap_lock);
    return -1; // Swap file is full
  }
  release(&swap_lock);

  // Write the entire block to the file
  ilock(swap_inode);
  if (writei(swap_inode, 0, (uint64)&block, offset, sizeof(struct swap_block)) != sizeof(struct swap_block)) {
    iunlock(swap_inode);
    return -1;
  }
  iunlock(swap_inode);
  
  return offset;
}

// Reads a block from the swap file.
// Fills the buffer with page data and pid_out/va_out with metadata.
// Returns 0 on success, -1 on error.
int
swap_in(char *buffer, int *pid_out, uint64 *va_out, uint offset)
{
  struct swap_block block;

  // Read the entire block from the file
  ilock(swap_inode);
  if (readi(swap_inode, 0, (uint64)&block, offset, sizeof(struct swap_block)) != sizeof(struct swap_block)) {
    iunlock(swap_inode);
    return -1;
  }
  iunlock(swap_inode);

  // Copy the data out to the provided buffers
  memmove(buffer, block.page_data, PGSIZE);
  *pid_out = block.pid;
  *va_out = block.va;

  return 0;
}