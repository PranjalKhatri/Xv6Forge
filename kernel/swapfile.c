#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "fs.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "buf.h"

// Define where on the disk our swap space begins.
// This must be after the space used by the file system.
// You might need to adjust this value. 2000 is a safe starting guess.
#define SWAP_START_BLK 1000
#define SWAP_SIZE_IN_BLOCKS 750 // Reserve 2000 blocks for swap
#define BLOCKS_PER_PAGE (PGSIZE / BSIZE)

static struct spinlock swap_lock;
static uint next_swap_block; // The next free block number on disk

void
swap_init(void)
{
  initlock(&swap_lock, "swap_lock");
  next_swap_block = 0;
}

// Writes a 4096-byte page to four 1024-byte disk blocks.
// Returns the starting block number.
int
swap_out(char *page_data, int pid, uint64 va)
{
  uint start_blockno;

  acquire(&swap_lock);
  
  // Check if there are enough free blocks for one full page
  if((next_swap_block + BLOCKS_PER_PAGE) > SWAP_SIZE_IN_BLOCKS) {
    printf("swap_out: out of swap space\n");
    release(&swap_lock);
    return -1;
  }

  start_blockno = SWAP_START_BLK + next_swap_block;
  next_swap_block += BLOCKS_PER_PAGE; // Reserve 4 blocks
  
  release(&swap_lock);

  // Write the page to disk, one block at a time.
  for (int i = 0; i < BLOCKS_PER_PAGE; i++) {
    struct buf *b = bread(ROOTDEV, start_blockno + i);
    memmove(b->data, page_data + (i * BSIZE), BSIZE);
    bwrite(b);
    brelse(b);
  }
  
  return start_blockno;
}

// Reads four 1024-byte disk blocks into a 4096-byte page.
// Returns 0 on success, -1 on error.
int
swap_in(char *buffer, int *pid_out, uint64 *va_out, uint start_blockno)
{
  // Read the page from disk, one block at a time.
  for (int i = 0; i < BLOCKS_PER_PAGE; i++) {
    struct buf *b = bread(ROOTDEV, start_blockno + i);
    memmove(buffer + (i * BSIZE), b->data, BSIZE);
    brelse(b);
  }

  // Return default metadata values
  *pid_out = -1;
  *va_out = 0;

  return 0;
}