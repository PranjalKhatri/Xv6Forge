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

void freerange(void *pa_start, void *pa_end);

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

void
kinit()
{
  kalloc_cnt = 1;
  initlock(&kmem.lock, "kmem");
  kmem.free_mem_start= mru_init(end,NUM_PAGES,page_to_mru_map);
  freerange(kmem.free_mem_start, (void*)PHYSTOP);
  printf("NumPages: %d\n",NUM_PAGES);
  printf("end : %p\n",kmem.free_mem_start);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < (char*)kmem.free_mem_start || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  move_to_end(pa);
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;
  
  acquire(&kmem.lock);
  printf("kalloc called %d\n",kalloc_cnt);
  kalloc_cnt++;
  r = kmem.freelist;
  if(r){
    kmem.freelist = r->next;
  }else{
    printf("swap out\n");
    r=(struct run*)mru_swapout();
  } 
  release(&kmem.lock);
    if(r)
    memset((char*)r, 5, PGSIZE); // Fill with junk after lock is released.

  return (void*)r;
}

void* kernel_swapin(int offset){
  void* pa = kalloc();
  if(pa == 0)
    return 0; 
  int pid_dummy;
  uint64 va_dummy;
  if(swap_in((char*)pa, &pid_dummy, &va_dummy, offset) != 0){
    kfree(pa); // Read failed, so free the page we just allocated.
    return 0;
  }

  return pa;
}