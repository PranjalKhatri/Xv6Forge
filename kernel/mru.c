#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"
#include "proc.h"
#include "mru.h"
#include "swapfile.h"
#include "config.h"

int req_pages;
int mru_map_len;
char *map_start;
static struct mru_node **mru_map;
static struct mru_node *head, *end;

struct spinlock mru_lock;

void *mru_init(void *pa_start, int num_pages, struct mru_node *map[])
{
    debug("mru init start\n");
    initlock(&mru_lock, "mru_lock");
    req_pages = (sizeof(struct mru_node) * num_pages + PGSIZE - 1) / PGSIZE;
    mru_map = map;
    mru_map_len = num_pages - req_pages;
    
    char *p = (char *)PGROUNDUP((uint64)pa_start,PGSIZE);
    char *pa = p + req_pages * PGSIZE;
    map_start = pa;
    // use the req pages to store the mappings and start after it.
    for (int i = req_pages; i < num_pages; i++, p += sizeof(struct mru_node), pa += PGSIZE)
    {
        int j = i - req_pages;
        map[j] = (struct mru_node *)(p); // allocate it in the required page
        map[j]->pid = -1;
        map[j]->pa = pa;
        map[j]->va = 0;
        map[j]->ref_cnt=0;
        map[j]->prev = map[j]->next = 0;
        mru_map[j]->is_swappable=1;
        mru_map[j]->is_kernel=0;
        if (j != 0)
        {
            map[j]->prev = map[j - 1];
            map[j - 1]->next = map[j];
        }
    }
    head = map[0];
    end = map[num_pages - req_pages - 1];
    debug("mru init done with req pages : %d\n",req_pages);
    debug("mru init end\n");
    return (char *)PGROUNDUP((uint64)pa_start,PGSIZE) + req_pages * PGSIZE;
}
int PA2IDX(void *pa)
{
    int idx = (int)((char *)pa - map_start) / PGSIZE;
    if (idx >= mru_map_len || idx < 0){
        debug("idx: %d",idx);
        panic("PA2IDX");
    }
    return idx;
}
void map_neighbors(int idx)
{
    if (mru_map[idx] == 0)
        return; 
    if (mru_map[idx]->prev)
        mru_map[idx]->prev->next = mru_map[idx]->next;
    if (mru_map[idx]->next)
        mru_map[idx]->next->prev = mru_map[idx]->prev;
    mru_map[idx]->prev = 0;
    mru_map[idx]->next = 0;
}

void __move_to_end_unlocked(void *pa)
{
    int idx = PA2IDX(pa);
    if (mru_map[idx] == end)
        return;
    if (mru_map[idx] == head)
        head = head->next;
    map_neighbors(idx);
    end->next = mru_map[idx];
    mru_map[idx]->next = 0;
    mru_map[idx]->prev = end;
    end = mru_map[idx];
}
void move_to_end(void *pa)
{
    acquire(&mru_lock);
    int idx = PA2IDX(pa);
    mru_map[idx]->pid = -1;
    mru_map[idx]->va = 0;
    __move_to_end_unlocked(pa);
    release(&mru_lock);
}
// only to be used for kernel pages
void mark_non_swappable(void *pa)
{
    acquire(&mru_lock);
    mru_map[PA2IDX(pa)]->is_swappable=0;
    release(&mru_lock);
}

void mark_kernel(void *pa)
{
    acquire(&mru_lock);
    mru_map[PA2IDX(pa)]->is_kernel=1;
    mru_map[PA2IDX(pa)]->is_swappable=0;
    release(&mru_lock);
}

void move_to_head(void *pa)
{
    int idx = PA2IDX(pa);
    if (mru_map[idx] == head)
    {
        return;
    }
    if (mru_map[idx] == end)
        end = end->prev;
    map_neighbors(idx);
    mru_map[idx]->prev = 0;
    mru_map[idx]->next = head;
    head->prev = mru_map[idx];
    head = mru_map[idx];
}

void move_to_head_and_set(void *pa, int pid, uint64 va)
{
    if(va >= MAXVA){
        panic("invalid va in move to head and set\n");
    }
    acquire(&mru_lock);
    move_to_head(pa);
    head->pid = pid;
    head->va = va;
    release(&mru_lock);
}

void set_only(void *pa, int pid, uint64 va,int refs)
{
    acquire(&mru_lock);
    int idx = PA2IDX(pa);
    mru_map[idx]->pid = pid;
    mru_map[idx]->va = va;   
    mru_map[idx]->ref_cnt = refs;
    release(&mru_lock);
}

void *
mru_swapout()
{
    void *victim_pa;
    int victim_pid,victim_refcnt;
    uint64 victim_va;
    struct proc *victim_proc;
    int offset;
    if (head == 0)
        return 0;
    acquire(&mru_lock);
    
    struct mru_node* node=head;
    victim_pa = node->pa;
    victim_pid = node->pid;
    victim_va = node->va;
    victim_refcnt = node->ref_cnt;
    while(node && node != end){
        //dont swap trampoline and trapfram
        if(!node->is_swappable ||  (node->va >= TRAPFRAME && node->va < MAXVA)){
            node = node->next;
            continue;
        }
        else if( COW_SWAP_ENABLED || node->ref_cnt == 1){
            victim_pa = node->pa;
            victim_pid = node->pid;
            victim_va = node->va;
            victim_refcnt = node->ref_cnt;
            break;
        }
        node = node->next;
    }
    if( !COW_SWAP_ENABLED && node->ref_cnt > 1){
        panic("mru_swapout: no victim with refcnt 1 found");
    }
    release(&mru_lock);
    offset = swap_out(victim_pa, victim_pid, victim_va,victim_refcnt);
    if (offset < 0)
    {
        return 0;
    }
    void *return_pa = 0;
    victim_proc = find_proc(victim_pid);
    acquire(&mru_lock);
    if (victim_proc)
    {
        release(&mru_lock);
        pte_t *victim_ptep = walk(victim_proc->pagetable, victim_va, 0);
        acquire(&mru_lock);
        // Ensure the PTE still exists and belongs to the page we swapped out
        if (victim_ptep && (*victim_ptep & PTE_V) && PTE2PA(*victim_ptep) == (uint64)victim_pa)
        {
            *victim_ptep = PTE_SWAP_SET_OFFSET(offset);
            victim_proc->pst.num_swap_outs++;
            __move_to_end_unlocked(victim_pa); // Use the unlocked helper to update the list
            uint64 idx = PA2IDX(victim_pa);
            mru_map[idx]->pid = -1;
            mru_map[idx]->va = 0;
            mru_map[idx]->ref_cnt=0;
            return_pa = victim_pa;
        }else{
            swap_free(offset);
        }
    }else{
        swap_free(offset);
    }
    release(&mru_lock);

    return return_pa;
}

void quarantine_reserved_pages(void)
{
    // debug("quarantining!\n");
    struct mru_node *cur = head;
    struct mru_node *orig_end = end; // remember original end
    struct mru_node *next;
    while (cur != orig_end)
    {
        next = cur->next;
        if (cur->pid <= 2)
        {
            if (cur == head)
                head = head->next;
            map_neighbors(PA2IDX(cur->pa));
            __move_to_end_unlocked(cur->pa);
        }
        cur = next;
    }
    end = cur->prev;
    // debug("end now points to %d\n", end->pid);
}

void *
lru_swapout()
{
    return mru_swapout();
    /* void *victim_pa = 0;
    int victim_pid = -1,victim_refcnt;
    uint64 victim_va = 0;
    struct proc *victim_proc;
    int offset;
    if (end == 0)
        return 0;
    acquire(&mru_lock);

    struct mru_node *node = end;
    struct mru_node *start = head;
    do
    {
        //dont swap trampoline and trapfram
        if(!node->is_swappable || (node->va >= TRAPFRAME && node->va < MAXVA) 
            || node->pid == -1 || node->ref_cnt == 0){
            node = node->prev;
            continue;
        }
        else if ( (COW_SWAP_ENABLED || node->ref_cnt == 1))
        { // eligible user-mapped page
            victim_pa = node->pa;
            victim_pid = node->pid;
            victim_va = node->va;
            victim_refcnt = node->ref_cnt;
            break;
        }
        node = node->prev;
    } while (node && node != start);

    if( !COW_SWAP_ENABLED && node->ref_cnt > 1){
        panic("mru_swapout: no victim with refcnt 1 found");
    }
    release(&mru_lock);

    offset = swap_out(victim_pa, victim_pid, victim_va,victim_refcnt);
    if (offset < 0)
    {
        debug("lru: offset less than 0 returned \n");
        return 0;
    }
    void *return_pa = 0;
    victim_proc = find_proc(victim_pid);
    if (victim_proc)
    {
        pte_t *victim_ptep = walk(victim_proc->pagetable, victim_va, 0);
        acquire(&mru_lock);
        if (victim_ptep && (*victim_ptep & PTE_V) && PTE2PA(*victim_ptep) == (uint64)victim_pa)
        {
            *victim_ptep = PTE_SWAP_SET_OFFSET(offset);
            victim_proc->pst.num_swap_outs++;
            __move_to_end_unlocked(victim_pa);
            uint64 idx = PA2IDX(victim_pa);
            mru_map[idx]->pid=-1;
            mru_map[idx]->va=0;
            mru_map[idx]->ref_cnt=0;
            return_pa = victim_pa;
        }else{
            debug("victim pte is not set , pte: %lx\n",*victim_ptep);
            swap_free(offset);
        }
        release(&mru_lock);
    }else{
        debug("cant find vicitim proc, victim pid: %d\n",victim_pid);
        swap_free(offset);
    }
    return return_pa; */
}

struct mru_node *mru_get_end()
{
    return end;
}

void mru_dump(int n)
{
    struct mru_node *tmp = head;
    int i = 0;
    if (n > 0)
    {
        while (i < n && tmp)
        {
            printf("PID: %d  | VA: %ld  | PA: %p  | IS_KERNEL: %d\n", tmp->pid, tmp->va, tmp->pa,tmp->is_kernel);
            tmp = tmp->next;
            i++;
        }
    }
    else
    {
        tmp = end;
        n=-n;
        while (i < n && tmp)
        {
            printf("PID: %d  | VA: %ld  | PA: %p  | IS_KERNEL: %d\n", tmp->pid, tmp->va, tmp->pa,tmp->is_kernel);
            tmp = tmp->prev;
            i++;
        }
    
    }
}

// Returns the new reference count.
int mru_incref_helper(uint64 pa)
{
  if (((uint64)pa % PGSIZE) != 0)
    panic("mru_incref_helper: not aligned");

  uint64 idx = PA2IDX((void*)pa);
  int count;

  acquire(&mru_lock);
  if (idx >= NUM_PAGES) {
    release(&mru_lock);
    panic("mru_incref_helper: index out of bounds");
  }
  
  if (mru_map[idx]->ref_cnt < 0) {
      release(&mru_lock);
      panic("mru_incref_helper: negative ref count");
  }
  
  count = ++mru_map[idx]->ref_cnt;
  release(&mru_lock);
  
  return count;
}


// Returns 1 if ref_cnt is 0 (page is ready to be returned to freelist), 0 otherwise.
int mru_decref_helper(uint64 pa)
{
  if (((uint64)pa % PGSIZE) != 0)
    panic("mru_decref_helper: invalid pa");

  uint64 idx = PA2IDX((void*)pa);
  int c;
  int is_free = 0;

  acquire(&mru_lock);
  if (idx >= NUM_PAGES || (!mru_map[idx]->is_kernel && mru_map[idx]->ref_cnt < 1)) {
    printf("idx: %ld, refcnt: %d, pid : %d,pa %p\n",idx,mru_map[idx]->ref_cnt,mru_map[idx]->pid,mru_map[idx]->pa);
    panic("mru_decref_helper: index out of bounds or already zero");
  }
    
  c = --mru_map[idx]->ref_cnt;
  
  if (c == 0)
  {
      mru_map[idx]->pid = -1; 
      mru_map[idx]->va = 0;
      __move_to_end_unlocked((void*)pa);
      is_free = 1;
    }
    
    release(&mru_lock);
    
    return is_free;
}
void 
mru_set_refcnt(uint64 pa, int refcnt)
{
  if (((uint64)pa % PGSIZE) != 0)
    panic("mru_set_refcnt: not aligned");

  uint64 idx = PA2IDX((void*)pa);

  acquire(&mru_lock);

  if (idx >= NUM_PAGES) {
    release(&mru_lock);
    panic("mru_set_refcnt: index out of bounds");
  }
  
  if (refcnt < 0) {
    release(&mru_lock);
    panic("mru_set_refcnt: negative ref count");
  }
  
  mru_map[idx]->ref_cnt = refcnt;
  
  release(&mru_lock);
}
int
get_refcnt(uint64 pa)
{
  if (((uint64)pa % PGSIZE) != 0 || pa >= PHYSTOP)
    return -1;
  uint64 idx = PA2IDX((void*)pa);
  if (idx >= NUM_PAGES)
    return -1;
  int count;
  acquire(&mru_lock);
  count = mru_map[idx]->ref_cnt;
  release(&mru_lock);
  return count;
}