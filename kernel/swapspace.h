#include "types.h"
void swapspace_init();
int allocate_block(int block_count);
void free_block(int start_block, int block_count);