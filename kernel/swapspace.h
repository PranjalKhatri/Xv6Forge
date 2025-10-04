#include "types.h"

void init_memorymap(uint64 block_size,uint64 block_count);
int allocate_block(uint64 block_count);
void free_block(int start_block, int block_count);