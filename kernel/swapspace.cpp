#include "types.h"
#include "param.h"
#include "defs.h"
#include "swapspace.h"
#include "spinlock.h"

uint64 mem_block_size;
uint64 mem_block_count;
uint64 bit_map[2*FSSIZE/(8*sizeof(uint64))];
int mp_dsz = sizeof(bit_map[0]);
uint64 start_offset;

static struct spinlock swapspace_lock;


void init_memorymap(uint64 block_size, uint64 block_count,uint64 data_block_start)
{
    initlock(&swapspace_lock, "swapspace_lock");
    mem_block_size = block_size;
    mem_block_count = block_count;
    start_offset = data_block_start;
}

inline int 
is_allocated(uint64 idx)
{
    return (bit_map[idx / mp_dsz] & (1 << (idx % mp_dsz))) != 0;
}

inline int 
set_bit_map(uint64 idx){
    bit_map[idx/mp_dsz] |= 1ull<<(idx%mp_dsz);
}

inline void 
clear_bit_map(uint64 idx)
{
    uint64 word = idx / 64;
    uint64 bit  = idx % 64;
    bit_map[word] &= ~(1ULL << bit);
}
// Allocate 'block_count' contiguous free blocks
// Returns starting block index, or -1 if not enough space
int allocate_block(int block_count)
{
    int start = -1;
    int consec = 0;
    acquire(&swapspace_lock);
    for (int i = 0; i < mem_block_count; i++) {
        if (!is_allocated(i)) {
            if (consec == 0)
                start = i;
            consec++;
            if (consec == block_count) {
                for (int j = start; j < start + block_count; j++)
                    set_bit_map(j);
                release(&swapspace_lock);
                return start_offset+start;
            }
        } else {
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
    acquire(&swapspace_lock);
    for (int i = start_block; i < start_block + block_count; i++)
        clear_bit_map(i);
    release(&swapspace_lock);
}