#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "fs.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "buf.h"
#include "swapspace.h"
#include "config.h"

#define PAGE_META_BLOCKS 20
#define BLOCKS_PER_PAGE (PGSIZE / BSIZE)

uint64 mem_block_size;
uint64 mem_block_count;
uint64 bit_map[2 * FSSIZE / (8 * sizeof(uint64))];
int mp_dsz;
uint64 start_offset;

static struct spinlock swapspace_lock;

void swapspace_init()
{
    debug("swapspace_init");
    initlock(&swapspace_lock, "swapspace_lock");
    mp_dsz = sizeof(bit_map[0]);
    mem_block_size = BSIZE;
    start_offset = PAGE_META_BLOCKS+2+LOGBLOCKS+1 + FSSIZE / BPB + 4*NINODE/IPB+1 + 10; // check the layout in mkfs to get the number
    mem_block_count = FSSIZE-start_offset;
    DEBUG_PRINT(SWAP_SPACE, "start offset is %ld", start_offset);
}

// Get the metadata block number and offset within that block
// for a given page's start_blockno
// meta_sz: size of metadata structure in bytes
// Returns 0 on success
int get_meta_offset(int start_blockno, int *meta_blockno, int *meta_offset, int meta_sz)
{
    // Convert absolute block number to relative index
    int relative_blockno = start_blockno - start_offset;
    // Each page occupies BLOCKS_PER_PAGE blocks
    // Calculate which page this is (page index)
    int page_index = relative_blockno / BLOCKS_PER_PAGE;
    // Calculate how many metadata entries fit in one block
    int entries_per_block = BSIZE / meta_sz;
    // Find which metadata block contains this page's metadata
    int meta_block_index = page_index / entries_per_block;
    // Find offset within that metadata block
    int entry_in_block = page_index % entries_per_block;
    // Calculate the absolute metadata block number
    *meta_blockno = start_offset-PAGE_META_BLOCKS + meta_block_index;
    // Calculate byte offset within the metadata block
    *meta_offset = entry_in_block * meta_sz;
    
    if (meta_block_index >= PAGE_META_BLOCKS) {
        printf("get_meta_offset: metadata block index %d exceeds PAGE_META_BLOCKS %d\n", 
               meta_block_index, PAGE_META_BLOCKS);
        return -1;
    }
    
    return 0;
}

inline int
is_allocated(uint64 idx)
{
    return (bit_map[idx / mp_dsz] & (1 << (idx % mp_dsz))) != 0;
}

inline void
set_bit_map(uint64 idx)
{
    bit_map[idx / mp_dsz] |= 1ull << (idx % mp_dsz);
}

inline void
clear_bit_map(uint64 idx)
{
    uint64 word = idx / 64;
    uint64 bit = idx % 64;
    bit_map[word] &= ~(1ULL << bit);
}
// Allocate 'block_count' contiguous free blocks
// Returns starting block index, or -1 if not enough space
int allocate_block(int block_count)
{
    int start = -1;
    int consec = 0;
    acquire(&swapspace_lock);
    for (int i = 0; i < mem_block_count; i++)
    {
        if (!is_allocated(i))
        {
            if (consec == 0)
                start = i;
            consec++;
            if (consec == block_count)
            {
                for (int j = start; j < start + block_count; j++)
                    set_bit_map(j);
                release(&swapspace_lock);
                DEBUG_PRINT(SWAP_SPACE,"allocated start block %ld\n",start+start_offset);
                return start_offset + start;
            }
        }
        else
        {
            consec = 0;
            start = -1;
        }
    }
    release(&swapspace_lock);

    return -1;
}

// Free 'block_count' blocks starting at start_block
void free_block(int start_block, int block_count)
{
    start_block -= start_offset;
    DEBUG_PRINT(SWAP_SPACE,"freed %d till %d\n",start_block,start_block+block_count-1);
    acquire(&swapspace_lock);
    for (int i = start_block; i < start_block + block_count; i++)
        clear_bit_map(i);
    release(&swapspace_lock);
}