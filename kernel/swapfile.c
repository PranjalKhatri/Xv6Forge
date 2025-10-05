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

struct swap_metadata {
  int pid;
  uint64 va;
  int refcnt;
  int padding; // For alignment
};

void
swap_init(void)
{
  debug("swap_init()\n");
  initlock(&swap_lock, "swap_lock");
  next_swap_block = 0;
}

static int
write_metadata(int start_blockno, int pid, uint64 va, int refcnt)
{
  struct swap_metadata meta;
  meta.pid = pid;
  meta.va = va;
  meta.refcnt = refcnt;
  meta.padding = 0;

  int meta_blockno, meta_offset;
  get_meta_offset(start_blockno, &meta_blockno, &meta_offset,sizeof(struct swap_metadata));

  struct buf *b = bread(ROOTDEV, meta_blockno);
  if (!b) {
    printf("write_metadata: error reading metadata block %d\n", meta_blockno);
    return -1;
  }

  memmove(b->data + meta_offset, &meta, sizeof(struct swap_metadata));
  bwrite(b);
  brelse(b);
  return 0;
}
static int
read_metadata(int start_blockno, int *pid, uint64 *va, int *refcnt)
{
  struct swap_metadata meta;

  int meta_blockno, meta_offset;
  get_meta_offset(start_blockno, &meta_blockno, &meta_offset,sizeof(struct swap_metadata));

  struct buf *b = bread(ROOTDEV, meta_blockno);
  if (!b) {
    printf("read_metadata: error reading metadata block %d\n", meta_blockno);
    return -1;
  }

  memmove(&meta, b->data + meta_offset, sizeof(struct swap_metadata));
  brelse(b);

  *pid = meta.pid;
  *va = meta.va;
  *refcnt = meta.refcnt;

  return 0;
}

// Writes a 4096-byte page to four 1024-byte disk blocks.
// Returns the starting block number.
int
swap_out(char *page_data, int pid, uint64 va,int refcnt)
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
  if (write_metadata(start_blockno, pid, va, refcnt) != 0) {
    printf("swap_out: error writing metadata. Freeing allocated blocks.\n");
    free_block(start_blockno, BLOCKS_PER_PAGE);
    return -1;
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
swap_in(char *buffer, int *pid_out, uint64 *va_out, uint start_blockno,int*refcnt)
{
  if (read_metadata(start_blockno, pid_out, va_out, refcnt) != 0) {
    printf("swap_in: error reading metadata\n");
    return -1;
  }
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
