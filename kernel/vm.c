#include "param.h"
#include "types.h"
#include "memlayout.h"
#include "elf.h"
#include "riscv.h"
#include "defs.h"
#include "spinlock.h"
#include "proc.h"
#include "fs.h"
#include "mru.h"
#include "swapfile.h"
#include "config.h"

/*
 * the kernel's page table.
 */
pagetable_t kernel_pagetable;

extern char etext[]; // kernel.ld sets this to end of kernel code.

extern char trampoline[]; // trampoline.S

static uint64 pt_pages_allocated = 0;    // Total page table pages allocated
static uint64 pt_leaf_mappings = 0;      // Total leaf mappings (actual memory pages mapped)
static uint64 pt_superpage_mappings = 0; // Super page mappings

void 
k_mapaligned(pagetable_t pagetable, uint64 va, uint64 size, int perm);
pte_t *
k_walk(pagetable_t pagetable, uint64 va, int alloc, int levels);
int 
k_mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, int perm, int level);
void print_pagetable_stats(void);

// Make a direct-map page table for the kernel.
pagetable_t
kvmmake(void)
{
  pagetable_t kpgtbl;

  kpgtbl = (pagetable_t)kalloc();
  memset(kpgtbl, 0, PGSIZE);
  debug("kpgtbl allocated at: %p\n", &kpgtbl);

  // uart registers
  kvmmap(kpgtbl, UART0, UART0, PGSIZE, PTE_R | PTE_W, 0);
  debug("UART mapped\n");

  // virtio mmio disk interface
  kvmmap(kpgtbl, VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W, 0);
  debug("VIRTIO mapped\n");

  // PLIC
  kvmmap(kpgtbl, PLIC, PLIC, 0x4000000, PTE_R | PTE_W, 0);
  debug("PLIC mapped\n");

  // map kernel text executable and read-only.
  debug("Mapping text: KERNBASE=%ld, etext=%ld, size=%ld\n",
         KERNBASE, (uint64)etext, (uint64)etext - KERNBASE);
  k_mapaligned(kpgtbl, KERNBASE, (uint64)etext - KERNBASE, PTE_R | PTE_X);
  debug("Text mapped successfully\n");

  // map kernel data and the physical RAM we'll make use of.
  debug("Mapping data: etext=%ld, PHYSTOP=%ld, size=%ld\n",
         (uint64)etext, PHYSTOP, PHYSTOP - (uint64)etext);
  k_mapaligned(kpgtbl, (uint64)etext, PHYSTOP - (uint64)etext, PTE_R | PTE_W);

  // map the trampoline
  kvmmap(kpgtbl, TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X, 0);
  debug("Trampoline mapped\n");

  // allocate and map a kernel stack for each process.
  proc_mapstacks(kpgtbl);
  debug("Process stacks mapped\n");

  // print_pagetable_stats();
  return kpgtbl;
}

// add a mapping to the kernel page table.
// only used when booting.
// does not flush TLB or enable paging.
/// @param use_superpg whether to use superpage table or not for the given va
void kvmmap(pagetable_t kpgtbl, uint64 va, uint64 pa, uint64 sz, int perm, int use_superpg)
{
  if (k_mappages(kpgtbl, va, sz, pa, perm, 2 - use_superpg) != 0)
    panic("kvmmap");
}

// Initialize the kernel_pagetable, shared by all CPUs.
void kvminit(void)
{
  kernel_pagetable = kvmmake();
}

// Switch the current CPU's h/w page table register to
// the kernel's page table, and enable paging.
void kvminithart()
{
  // wait for any previous writes to the page table memory to finish.
  sfence_vma();

  w_satp(MAKE_SATP(kernel_pagetable));

  // flush stale entries from the TLB.
  sfence_vma();
}

// Return the address of the PTE in page table pagetable
// that corresponds to virtual address va.  If alloc!=0,
// create any required page-table pages.
//
// The risc-v Sv39 scheme has three levels of page-table
// pages. A page-table page contains 512 64-bit PTEs.
// A 64-bit virtual address is split into five fields:
//   39..63 -- must be zero.
//   30..38 -- 9 bits of level-2 index.
//   21..29 -- 9 bits of level-1 index.
//   12..20 -- 9 bits of level-0 index.
//    0..11 -- 12 bits of byte offset within the page.
pte_t *
walk(pagetable_t pagetable, uint64 va, int alloc)
{
  return k_walk(pagetable, va, alloc, 2);
}

// walk levels down.
// design choice: used levels instead of a boolean for super page to allow to increase page size even more in future
pte_t *
k_walk(pagetable_t pagetable, uint64 va, int alloc, int _levels)
{
  if (va >= MAXVA)
    panic("walk");
  if (_levels < 0)
    _levels = 0;
  if (_levels > 3)
    _levels = 3;

  pte_t *pte;
  for (int level = 2; level > 0; level--)
  {
    pte = &pagetable[PX(level, va)];
    if (2-level == _levels)
      return pte;
    if (*pte & PTE_V)
    {
      pagetable = (pagetable_t)PTE2PA(*pte);
    }
    else
    {
      if (!alloc || (pagetable = (pde_t *)kalloc()) == 0)
        return 0;
      memset(pagetable, 0, PGSIZE);
      *pte = PA2PTE(pagetable) | PTE_V;
      pt_pages_allocated++;
    }
  }

  return &pagetable[PX(0, va)];
}

// Look up a virtual address, return the physical address,
// or 0 if not mapped.
// Can only be used to look up user pages.
uint64
walkaddr(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  uint64 pa;

  if (va >= MAXVA)
    return 0;

  pte = walk(pagetable, va, 0);
  if (pte == 0)
    return 0;
  if ((*pte & PTE_V) == 0)
    return 0;
  if ((*pte & PTE_U) == 0)
    return 0;
  pa = PTE2PA(*pte);
  return pa;
}

// Create PTEs for virtual addresses starting at va that refer to
// physical addresses starting at pa.
// va and size MUST be page-aligned.
// Returns 0 on success, -1 if walk() couldn't
// allocate a needed page-table page.
int mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, int perm)
{
  return k_mappages(pagetable, va, size, pa, perm, 2);
}

int k_mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, int perm, int level)
{
  uint64 a, last;
  pte_t *pte;
  uint64 alignment_sz = (level == 2) ? PGSIZE : SUPER_PGSIZE;
  if ((va % alignment_sz) != 0)
    panic("mappages: va not aligned");

  if ((size % alignment_sz) != 0)
    panic("mappages: size not aligned");

  if (size == 0)
    panic("mappages: size");

  a = va;
  last = va + size - alignment_sz;
  // debug("mappages: mapping from %ld to %ld\n", va, last);
  for (;;)
  {
    if ((pte = k_walk(pagetable, a, 1, level)) == 0)
      return -1;
    if (*pte & PTE_V)
      panic("mappages: remap");
    *pte = PA2PTE(pa) | perm | PTE_V;

    // Count mappings
    if (level == 1)
      pt_superpage_mappings++;
    else
      pt_leaf_mappings++;

    if (a == last)
      break;
    a += alignment_sz;
    pa += alignment_sz;
  }
  return 0;
}

// create an empty user page table.
// returns 0 if out of memory.
pagetable_t
uvmcreate()
{
  pagetable_t pagetable;
  pagetable = (pagetable_t)kalloc();
  if (pagetable == 0)
    return 0;
  memset(pagetable, 0, PGSIZE);
  return pagetable;
}

// Remove npages of mappings starting from va. va must be
// page-aligned. It's OK if the mappings don't exist.
// Optionally free the physical memory.
void uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free)
{
  uint64 a;
  pte_t *pte;

  if ((va % PGSIZE) != 0)
    panic("uvmunmap: not aligned");

  for (a = va; a < va + npages * PGSIZE; a += PGSIZE)
  {
    if ((pte = walk(pagetable, a, 0)) == 0) // leaf page table entry allocated?
      continue;
    if(PTE_IS_SWAPPED(*pte)){
      debug("swapped pte in uvmunmap\n");
      if(do_free) {
        uint64 pa = vmfault(pagetable, a, 0, *pte & PTE_X);
        if(pa == 0) {
          // some error in vmfault
          printf("uvmunmap swapfree!\n");
          *pte = 0;
          continue;
        }else {
          kfree((void*)pa);
          debug("freed a swap page after faulting");
        }
      }
      *pte = 0;
      continue;
    }
    if ((*pte & PTE_V) == 0) // has physical page been allocated?
      continue;
    if (do_free)
    {
      uint64 pa = PTE2PA(*pte);
      kfree((void *)pa);
    }
    *pte = 0;
  }
}

// Allocate PTEs and physical memory to grow a process from oldsz to
// newsz, which need not be page aligned.  Returns new size or 0 on error.
uint64
uvmalloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz, int xperm)
{
  char *mem;
  uint64 a;

  if (newsz < oldsz)
    return oldsz;

  oldsz = PGROUNDUP(oldsz, PGSIZE);
  for (a = oldsz; a < newsz; a += PGSIZE)
  {
    mem = kalloc();
    if (mem == 0)
    {
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
    memset(mem, 0, PGSIZE);
    if (mappages(pagetable, a, PGSIZE, (uint64)mem, PTE_R | PTE_U | xperm) != 0)
    {
      kfree(mem);
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
    move_to_head_and_set(mem, myproc()->pid, a);
  }
  return newsz;
}

// Deallocate user pages to bring the process size from oldsz to
// newsz.  oldsz and newsz need not be page-aligned, nor does newsz
// need to be less than oldsz.  oldsz can be larger than the actual
// process size.  Returns the new process size.
uint64
uvmdealloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
  if (newsz >= oldsz)
    return oldsz;

  if (PGROUNDUP(newsz, PGSIZE) < PGROUNDUP(oldsz, PGSIZE))
  {
    int npages = (PGROUNDUP(oldsz, PGSIZE) - PGROUNDUP(newsz, PGSIZE)) / PGSIZE;
    uvmunmap(pagetable, PGROUNDUP(newsz, PGSIZE), npages, 1);
  }

  return newsz;
}

// Recursively free page-table pages.
// All leaf mappings must already have been removed.
void freewalk(pagetable_t pagetable)
{
  // there are 2^9 = 512 PTEs in a page table.
  for (int i = 0; i < 512; i++)
  {
    pte_t pte = pagetable[i];
    if ((pte & PTE_V) && (pte & (PTE_R | PTE_W | PTE_X)) == 0)
    {
      // this PTE points to a lower-level page table.
      uint64 child = PTE2PA(pte);
      freewalk((pagetable_t)child);
      pagetable[i] = 0;
    }
    else if (pte & PTE_V)
    {
      panic("freewalk: leaf");
    }
  }
  kfree((void *)pagetable);
}

// Free user memory pages,
// then free page-table pages.
void uvmfree(pagetable_t pagetable, uint64 sz)
{
  if (sz > 0)
    uvmunmap(pagetable, 0, PGROUNDUP(sz, PGSIZE) / PGSIZE, 1);
  freewalk(pagetable);
}

// Given a parent process's page table, copy
// its memory into a child's page table.
// Copies both the page table and the
// physical memory.
// returns 0 on success, -1 on failure.
// frees any allocated pages on failure.
int uvmcopy(pagetable_t old, pagetable_t new, uint64 sz, int child_pid)
{
  pte_t *pte;
  uint64 pa, i;
  uint flags;
  char *mem;

  for (i = 0; i < sz; i += PGSIZE)
  {
    if ((pte = walk(old, i, 0)) == 0)
      continue; // page table entry hasn't been allocated
    if ((*pte & PTE_V) == 0)
      continue; // physical page hasn't been allocated
    pa = PTE2PA(*pte);
    flags = PTE_FLAGS(*pte);
    if ((mem = kalloc()) == 0)
      goto err;
    memmove(mem, (char *)pa, PGSIZE);
    if (mappages(new, i, PGSIZE, (uint64)mem, flags) != 0)
    {
      kfree(mem);
      goto err;
    }
    move_to_head_and_set(mem, child_pid, i);
  }

  return 0;

err:
  uvmunmap(new, 0, i / PGSIZE, 1);
  return -1;
}

// maps parent page table in child and increment refcnts
int cowuvmcopy(pagetable_t old, pagetable_t new, uint64 sz, int child_pid)
{
  pte_t *pte;
  uint64 pa, i;
  uint flags;

  for (i = 0; i < sz; i += PGSIZE)
  {
    if ((pte = walk(old, i, 0)) == 0)
      continue; // page table entry hasn't been allocated

    if(PTE_IS_SWAPPED(*pte)){
      debug("in cow swap\n");
      if(!COW_SWAP_ENABLED)panic("found swapped cow page when cow swap was disabled");
        pte_t *npte = walk(new,i,1);
        if(npte == 0)goto err;
        *npte = *pte;
        //TODO: swapped refcnts;
        continue;
    }

    if ((*pte & PTE_V) == 0)
      continue; // physical page hasn't been allocated
    pa = PTE2PA(*pte);
    flags = PTE_FLAGS(*pte);
    
    // If page is writable, make it COW in both parent and child
    if (flags & PTE_W)
    {
      flags = (flags & ~PTE_W) | PTE_COW;
      *pte = PA2PTE(pa) | flags;
      // CRITICAL: Flush TLB on parent PTE modification
      sfence_vma();
    }
    if (mappages(new, i, PGSIZE, (uint64)pa, flags) != 0)
      goto err;
    mru_incref_helper(pa);
    move_to_head_and_set((void*)pa,child_pid,i);
    sfence_vma();
  }
  return 0;

err:
  uvmunmap(new, 0, i / PGSIZE, 1);
  return -1;
}

// mark a PTE invalid for user access.
// used by exec for the user stack guard page.
void uvmclear(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;

  pte = walk(pagetable, va, 0);
  if (pte == 0)
    panic("uvmclear");
  *pte &= ~PTE_U;
}

// Copy from kernel to user.
// Copy len bytes from src to virtual address dstva in a given page table.
// Return 0 on success, -1 on error.
int copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len)
{
  uint64 n, va0, pa0;
  pte_t *pte;

  while (len > 0)
  {
    va0 = PGROUNDDOWN(dstva, PGSIZE);
    if (va0 >= MAXVA)
      return -1;

    pa0 = walkaddr(pagetable, va0);
    if (pa0 == 0)
    {
      if ((pa0 = vmfault(pagetable, va0, 0, 0)) == 0)
      {
        return -1;
      }
    }

    pte = walk(pagetable, va0, 0);
    if (pte == 0)
      return -1;
    // forbid copyout over read-only user text pages.
    if ((*pte & PTE_W) == 0){
      if (PTE_IS_COW(*pte)){
        if (cowhandler(pagetable, va0) < 0){
          printf("copyout: COW handler failed\n");
          return -1;
        }
        sfence_vma();
        pa0 = walkaddr(pagetable, va0);
        if (pa0 == 0){
          printf("copyout: remap failed after COW\n");
          return -1;
        }
      }else{
        printf("copyout forbid\n");
        return -1;
      }
    }
    n = PGSIZE - (dstva - va0);
    if (n > len)
      n = len;
    memmove((void *)(pa0 + (dstva - va0)), src, n);

    len -= n;
    src += n;
    dstva = va0 + PGSIZE;
  }
  return 0;
}

// Copy from user to kernel.
// Copy len bytes to dst from virtual address srcva in a given page table.
// Return 0 on success, -1 on error.
int copyin(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len)
{
  uint64 n, va0, pa0;

  while (len > 0)
  {
    va0 = PGROUNDDOWN(srcva, PGSIZE);
    pa0 = walkaddr(pagetable, va0);
    if (pa0 == 0)
    {
      if ((pa0 = vmfault(pagetable, va0, 0, 0)) == 0)
      {
        return -1;
      }
    }
    n = PGSIZE - (srcva - va0);
    if (n > len)
      n = len;
    memmove(dst, (void *)(pa0 + (srcva - va0)), n);

    len -= n;
    dst += n;
    srcva = va0 + PGSIZE;
  }
  return 0;
}

// Copy a null-terminated string from user to kernel.
// Copy bytes to dst from virtual address srcva in a given page table,
// until a '\0', or max.
// Return 0 on success, -1 on error.
int copyinstr(pagetable_t pagetable, char *dst, uint64 srcva, uint64 max)
{
  uint64 n, va0, pa0;
  int got_null = 0;

  while (got_null == 0 && max > 0)
  {
    va0 = PGROUNDDOWN(srcva, PGSIZE);
    pa0 = walkaddr(pagetable, va0);
    if (pa0 == 0)
      return -1;
    n = PGSIZE - (srcva - va0);
    if (n > max)
      n = max;

    char *p = (char *)(pa0 + (srcva - va0));
    while (n > 0)
    {
      if (*p == '\0')
      {
        *dst = '\0';
        got_null = 1;
        break;
      }
      else
      {
        *dst = *p;
      }
      --n;
      --max;
      p++;
      dst++;
    }

    srcva = va0 + PGSIZE;
  }
  if (got_null)
  {
    return 0;
  }
  else
  {
    return -1;
  }
}

int
cowhandler(pagetable_t pagetable,uint64 va)
{
  pte_t *pte;
  uint64 pa;
  uint flags;
  char *mem;
  if(va >= MAXVA)
    return -1;
  // Round down to page boundary
  va = PGROUNDDOWN(va,PGSIZE);
  
  // Get the PTE
  if((pte = walk(pagetable, va, 0)) == 0)
    return -1;
  if((*pte & PTE_V) == 0)
    return -1;
  if((*pte & PTE_U) == 0)
    return -1;
  
  // Check if it's a COW page
  if((*pte & PTE_COW) == 0)
    return -1;
  
  pa = PTE2PA(*pte);
  flags = PTE_FLAGS(*pte);
  
  // Check reference count
  int refcount = get_refcnt(pa);
  if(refcount < 0)
    return -1;
  
  if(refcount == 1) {
    // Only reference - just make it writable again
    *pte = PA2PTE(pa) | ((flags & ~PTE_COW) | PTE_W);
    move_to_head_and_set((void*)pa,myproc()->pid,va);
  } else {
      if((mem = kalloc()) == 0)
      return -1;
      memmove(mem, (char*)pa, PGSIZE);
      *pte = PA2PTE(mem) | ((flags & ~PTE_COW) | PTE_W);
      move_to_head_and_set(mem,myproc()->pid,va);
      kfree((void*)pa);
  }
  
  return 0;
}

int swaphandler(pagetable_t pagetable,pte_t *pte,uint64 va,uint64* mem){
  if (PTE_IS_SWAPPED(*pte))
  {
    int offset = PTE_SWAP_GET_OFFSET(*pte);
    *mem = (uint64)kernel_swapin(offset);
    if (*mem == 0){
      debug("kernel swapin returned 0");
      return -1;
    }
    return 0;
  }else{
    return -1;
  }
}

// allocate and map user memory if process is referencing a page
// that was lazily allocated in sys_sbrk().
// returns 0 if va is invalid or already mapped, or if
// out of physical memory, and physical address if successful.
uint64
vmfault(pagetable_t pagetable, uint64 va, int read, int instruction)
{
  uint64 mem;
  pte_t *pte;
  struct proc *p = myproc();
  p->pst.num_page_faults++;
  va = PGROUNDDOWN(va, PGSIZE);
  if (va == TRAPFRAME || va == TRAMPOLINE){
    goto vm_swap;
  }  else if (va >= MAXVA || va >= p->sz){
    printf("va : %ld p-> %ld\n",va,p->sz);
    return 0;
  }
  pte = walk(pagetable, va, 0);//not allocating in case its a cow fault
  if (pte && PTE_IS_COW(*pte)) {
    if (cowhandler(pagetable,va) != 0) {
      debug("cowhandler returned non zero\n");
      return 0;
    }
    // COW handled successfully, return the physical address
    pte = walk(pagetable, va, 0);
    return PTE2PA(*pte);
  }

  vm_swap:
  pte = walk(pagetable, va, 1);//not a cow fault, can allcoate
  if (pte == 0 || ismapped(pagetable, va)){
    debug("already mapped/pte is 0 pte: %p",pte);
    return 0;
  }
  if (PTE_IS_SWAPPED(*pte)) {
    if(swaphandler(pagetable,pte,va,&mem) == 0)
      p->pst.num_swap_ins++;
    else{
      printf("swap handler failed\n");
      return 0;
    }
  }
  else{
    mem = (uint64)kalloc();
    if (mem == 0){
      debug("kalloc returned 0\n");
      return 0;
    }
    memset((void *)mem, 0, PGSIZE);
  }

  int perm = PTE_U;
  if (instruction)
    perm |= PTE_R | PTE_X; // executable page
  else
    perm |= PTE_W | PTE_R;

  if (mappages(p->pagetable, va, PGSIZE, mem, perm) != 0)
  {
    debug("mappages returned non 0\n");
    kfree((void *)mem);
    return 0;
  }
  
  move_to_head_and_set((void *)mem, p->pid, va);
  return mem;
}

int ismapped(pagetable_t pagetable, uint64 va)
{
  pte_t *pte = walk(pagetable, va, 0);
  if (pte == 0)
  {
    return 0;
  }
  if (*pte & PTE_V)
  {
    return 1;
  }
  return 0;
}

// Add this function to print statistics
void print_pagetable_stats(void)
{
  printf("\n=== Kernel Page Table Statistics ===\n");
  printf("Page table pages allocated: %ld (using %ld KB)\n", 
         pt_pages_allocated, (pt_pages_allocated * PGSIZE) / 1024);
  printf("Regular page mappings: %ld\n", pt_leaf_mappings);
  printf("Super page mappings: %ld (each covers 2MB)\n", pt_superpage_mappings);
  printf("Total memory mapped: %lld MB\n", 
         (pt_leaf_mappings * PGSIZE + pt_superpage_mappings * SUPER_PGSIZE) / (1024*1024));
  
  // Calculate theoretical pages needed without super pages
  uint64 total_memory_mapped = pt_leaf_mappings * PGSIZE + pt_superpage_mappings * SUPER_PGSIZE;
  uint64 pages_if_no_superpages = total_memory_mapped / PGSIZE;
  
  // Each 512 leaf entries needs 1 page table page
  // Plus level-1 pages (1 per 512 level-0 pages) 
  // Plus 1 root page
  uint64 level0_pages_needed = (pages_if_no_superpages + 511) / 512;
  uint64 level1_pages_needed = (level0_pages_needed + 511) / 512;
  uint64 total_pages_without_super = 1 + level1_pages_needed + level0_pages_needed;
  
  printf("\n--- Comparison ---\n");
  printf("With super pages: %ld page table pages\n", pt_pages_allocated);
  printf("Without super pages (estimated): %ld page table pages\n", total_pages_without_super);
  printf("Savings: %ld pages (%ld KB)\n", 
         total_pages_without_super - pt_pages_allocated,
         ((total_pages_without_super - pt_pages_allocated) * PGSIZE) / 1024);
  printf("====================================\n\n");
}


#ifdef NO_SUPERPAGES
void k_mapaligned(pagetable_t pagetable, uint64 va, uint64 size, int perm){
  if(size > 0)
    kvmmap(pagetable, va, va, size, perm, 0); // Force regular pages
}
#else
//  maps the va onto the direct pa
//  unaligned (regular page)
//  aligned (super page)
//  unaligned (regular page)
void k_mapaligned(pagetable_t pagetable, uint64 va, uint64 size, int perm)
{
  uint64 aligned_start = PGROUNDUP(va, SUPER_PGSIZE);
  uint64 unaligned_sz = aligned_start - va;
  // Check if we even have enough size for super pages
  if (unaligned_sz >= size)
  {
    // The entire range is smaller than one super page boundary
    // Just use regular pages for everything
    if (size > 0)
      kvmmap(pagetable, va, va, size, perm, 0);
    return;
  }

  uint64 aligned_sz = PGROUNDDOWN(size - unaligned_sz, SUPER_PGSIZE);
  uint64 rem = size - aligned_sz - unaligned_sz;

  if (unaligned_sz)
    kvmmap(pagetable, va, va, unaligned_sz, perm, 0);
  if (aligned_sz)
    kvmmap(pagetable, aligned_start, aligned_start, aligned_sz, perm, 1);
  if (rem)
    kvmmap(pagetable, aligned_start + aligned_sz, aligned_start + aligned_sz, rem, perm, 0);
}

#endif