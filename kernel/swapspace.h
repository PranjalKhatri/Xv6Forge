#include "types.h"
void swapspace_init();
int get_meta_offset(int start_blockno, int *meta_blockno, int *meta_offset,int meta_sz);
int allocate_block(int block_count);
void free_block(int start_block, int block_count);