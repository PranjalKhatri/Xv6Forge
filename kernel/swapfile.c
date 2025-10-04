#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "fs.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "buf.h"
#include "swapspace.h"

#define BLOCKS_PER_PAGE (PGSIZE / BSIZE)

static struct spinlock swap_lock;
static uint next_swap_block; // The next free block number on disk

void
swap_init(void)
{
  debug("swap_init()\n");
  initlock(&swap_lock, "swap_lock");
  next_swap_block = 0;
}

// Writes a 4096-byte page to four 1024-byte disk blocks.
// Returns the starting block number.
int
swap_out(char *page_data, int pid, uint64 va)
{
  int start_blockno = allocate_block(BLOCKS_PER_PAGE);  
  if (start_blockno < 0) {
    printf("swap_out: out of swap space.\n");
    return -1;
  }
  // Write the page to disk, one block at a time.
  for (int i = 0; i < BLOCKS_PER_PAGE; i++) {
    struct buf *b = bread(ROOTDEV, start_blockno + i);
    if (!b) {
      printf("swap_out: error reading buffer for block %d. Freeing allocated blocks.\n", start_blockno + i);
      free_block(start_blockno, BLOCKS_PER_PAGE);
      return -1;
    }
    memmove(b->data, page_data + (i * BSIZE), BSIZE);
    bwrite(b);
    brelse(b);
  }
  
  return start_blockno;
}

// Releases the blocks previously allocated by swap_out back to the pool.
void
swap_free(int start_blockno)
{
  if (start_blockno < 0) {
    return; 
  }
  free_block(start_blockno, BLOCKS_PER_PAGE);
}

// Reads four 1024-byte disk blocks into a 4096-byte page.
// Returns 0 on success, -1 on error.
int
swap_in(char *buffer, int *pid_out, uint64 *va_out, uint start_blockno)
{
  // Read the page from disk, one block at a time.
  for (int i = 0; i < BLOCKS_PER_PAGE; i++) {
    struct buf *b = bread(ROOTDEV, start_blockno + i);
    if (!b) {
      printf("swap_in: error reading block %d\n", start_blockno + i);
      return -1;
    }
    memmove(buffer + (i * BSIZE), b->data, BSIZE);
    brelse(b);
  }

  // Return default metadata values
  *pid_out = -1;
  *va_out = 0;
  swap_free(start_blockno);
  return 0;
}
