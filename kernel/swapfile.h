#include "types.h"

// Public interface for the swap file system
void swap_init(void);
int  swap_out(char *page_data, int pid, uint64 va,int refcnt);
int  swap_in(char *buffer, int *pid_out, uint64 *va_out, uint offset,int* refcnt);
void swap_free(int start_blockno);