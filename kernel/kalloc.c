// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"
#include "mru.h"
#include "swapfile.h"
#include "kalloc.h"

int replacement_policy;
uint64 numfreepages;
int freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
  void *free_mem_start;
} kmem;

struct mru_node* page_to_mru_map[NUM_PAGES];
int kalloc_cnt;
int refcnt[NUM_PAGES]={0};

#define PA_TO_IDX(pa) (((uint64)(pa) - (uint64)kmem.free_mem_start) >> PGSHIFT)
uint64
pa_to_index(uint64 pa)
{
  return PA_TO_IDX(pa);
}

void incref(uint64 pa);
void decref(uint64 pa);
int
get_refcnt(uint64 pa)
{
  if (((uint64)pa % PGSIZE) != 0 || (char *)pa < (char *)kmem.free_mem_start || pa >= PHYSTOP)
    return -1;
  uint64 idx = PA_TO_IDX(pa);
  if (idx >= NUM_PAGES)
    return -1;
  int count;
  acquire(&kmem.lock);
  count = refcnt[idx];
  release(&kmem.lock);
  return count;
}
void
kinit()
{
  kalloc_cnt = 1;
  replacement_policy = MRU_POLICY;
  initlock(&kmem.lock, "kmem");
  kmem.free_mem_start= mru_init(end,NUM_PAGES,page_to_mru_map);
  numfreepages = freerange(kmem.free_mem_start, (void*)PHYSTOP);
  debug("NumPages    : %ld\nNumFreePages: %ld\n",NUM_PAGES,numfreepages);
  debug("end     : %p\nPHYSTOP : %p\n",kmem.free_mem_start,(void*)PHYSTOP);

}

int
freerange(void *pa_start, void *pa_end)
{
  char *p;
  int i=0;
  p = (char*)PGROUNDUP((uint64)pa_start,PGSIZE);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE,i++){
    memset(p, 1, PGSIZE);
    struct run* r = (struct run *)p;
    acquire(&kmem.lock);
    r->next = kmem.freelist;
    kmem.freelist = r;
    move_to_end(p);
    release(&kmem.lock);
  }
  return i;
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < (char*)kmem.free_mem_start || (uint64)pa >= PHYSTOP)
    panic("kfree");

  decref((uint64)pa);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
// Corrected kalloc function
void *
kalloc(void)
{
  struct run *r;
  
  acquire(&kmem.lock);
  r = kmem.freelist;
  numfreepages--;
  if(r){
    kmem.freelist = r->next;
    uint64 idx = PA_TO_IDX(r);
    if (idx >= NUM_PAGES) {printf("%ld %p ",idx,r);panic("kalloc: pa index out of bounds");}
    refcnt[idx] = 1;
    release(&kmem.lock); // Release lock for the simple case
    memset((char*)r, 5, PGSIZE); // fill with junk
    return (void*)r;
  }

  // printf("kalloc: No free page in freelist\n");
  // Freelist is empty. Release the lock BEFORE calling the function
  // that will perform disk I/O.
  release(&kmem.lock);
  // printf("swapout request\n");
  // Now it is safe to call mru_swapout(), which may sleep.
  if(replacement_policy == MRU_POLICY)
    r = (struct run*)mru_swapout(refcnt);
  else 
    r = (struct run*)lru_swapout(refcnt);
  if(r) {
    acquire(&kmem.lock); 
    uint64 idx = PA_TO_IDX(r);
    if (idx >= NUM_PAGES) panic("kalloc: swapped pa index out of bounds");
    refcnt[idx] = 1; 
    release(&kmem.lock);
    // The reclaimed page also needs to be filled with junk.
    memset((char*)r, 5, PGSIZE);
  }

  return (void*)r;
}

void* kernel_swapin(int offset){
  void* pa = kalloc();
  if(pa == 0)
    return 0; 
  int pid_dummy;
  uint64 va_dummy;
  int stored_refcnt;
  if(swap_in((char*)pa, &pid_dummy, &va_dummy, offset,&stored_refcnt) != 0){
    kfree(pa); // Read failed, so free the page we just allocated.
    return 0;
  }
  acquire(&kmem.lock);
  if (stored_refcnt < 1) panic("kernel_swapin: Retrieved refcnt is less than 1");
  refcnt[PA_TO_IDX(pa)]=stored_refcnt;
  release(&kmem.lock);
  return pa;
}

void incref(uint64 pa)
{
  if (((uint64)pa % PGSIZE) != 0 || (char *)pa < (char *)kmem.free_mem_start || pa >= PHYSTOP)
    panic("incref: invalid pa");

  uint64 idx = PA_TO_IDX(pa);

  acquire(&kmem.lock);
  if (idx >= NUM_PAGES || refcnt[idx] < 1)
    panic("incref: index out of bounds or zero count");
  refcnt[idx]++;
  release(&kmem.lock);
}

void decref(uint64 pa)
{
  if (((uint64)pa % PGSIZE) != 0 || (char *)pa < (char *)kmem.free_mem_start || pa >= PHYSTOP)
    panic("decref: invalid pa");

  uint64 idx = PA_TO_IDX(pa);
  int c;

  acquire(&kmem.lock);
  if (idx >= NUM_PAGES || refcnt[idx] < 1)
    panic("decref: index out of bounds or already zero");

  c = --refcnt[idx];

  if (c == 0)
  {
    struct run *r = (struct run *)pa;
    memset((void *)pa, 1, PGSIZE);
    r->next = kmem.freelist;
    kmem.freelist = r;
    move_to_end((void *)pa);
    numfreepages++;
  }
  release(&kmem.lock);
}

uint64
sys_getfreemem(void)
{
  uint64 free_mem_bytes;
  acquire(&kmem.lock); 
  // printf("free: %ld\n",numfreepages);
  free_mem_bytes = numfreepages * PGSIZE;
  release(&kmem.lock);
  return free_mem_bytes; 
}