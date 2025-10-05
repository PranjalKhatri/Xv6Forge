#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"
#include "proc.h"
#include "mru.h"
#include "swapfile.h"

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
        if (j != 0)
        {
            map[j]->prev = map[j - 1];
            map[j - 1]->next = map[j];
        }
    }
    head = map[0];
    end = map[num_pages - req_pages - 1];
    printf("mru init done with req pages : %d\n",req_pages);
    debug("mru init end\n");
    return (char *)PGROUNDUP((uint64)pa_start,PGSIZE) + req_pages * PGSIZE;
}
int PA2IDX(void *pa)
{
    int idx = (int)((char *)pa - map_start) / PGSIZE;
    if (idx >= mru_map_len || idx < 0)
        panic("PA2IDX");
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

void move_to_head_and_set(void *pa, int pid, int va)
{
    acquire(&mru_lock);
    move_to_head(pa);
    head->pid = pid;
    head->va = va;
    release(&mru_lock);
}

void *
mru_swapout()
{
    void *victim_pa;
    int victim_pid;
    uint64 victim_va;
    struct proc *victim_proc;
    int offset;
    acquire(&mru_lock);
    if (head == 0)
    {
        release(&mru_lock);
        return 0;
    }
    victim_pa = head->pa;
    victim_pid = head->pid;
    victim_va = head->va;
    release(&mru_lock);
    uint64 idx = PA2IDX(victim_pa);
    offset = swap_out(victim_pa, victim_pid, victim_va,mru_map[idx]->ref_cnt);
    if (offset < 0)
    {
        return 0;
    }

    victim_proc = find_proc(victim_pid);
    if (victim_proc)
    {
        pte_t *victim_ptep = walk(victim_proc->pagetable, victim_va, 0);
        // Ensure the PTE still exists and belongs to the page we swapped out
        acquire(&mru_lock);
        if (victim_ptep && (*victim_ptep & PTE_V) && PTE2PA(*victim_ptep) == (uint64)victim_pa)
        {
            *victim_ptep = PTE_SWAP_SET_OFFSET(offset);
            victim_proc->pst.num_swap_outs++;
        }
        release(&mru_lock);
    }
    acquire(&mru_lock);
    __move_to_end_unlocked(victim_pa); // Use the unlocked helper to update the list
    release(&mru_lock);

    return victim_pa;
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
    static int cnt;
    void *victim_pa = 0;
    int victim_pid = -1;
    uint64 victim_va = 0;
    struct proc *victim_proc;
    int offset;

    acquire(&mru_lock);
    if (cnt == 0)
    {
        quarantine_reserved_pages();
        cnt = 1;
    }
    if (end == 0)
    {
        release(&mru_lock);
        return 0;
    }

    struct mru_node *cur = end;
    struct mru_node *start = head;
    struct mru_node *cand = 0;
    do
    {
        if (cur->pid >= 3)
        { // eligible user-mapped page
            cand = cur;
            break;
        }
        cur = cur->prev;
    } while (cur && cur != start);

    if (!cand)
    {
        release(&mru_lock);
        return 0;
    }
    victim_pa = cand->pa;
    victim_pid = cand->pid;
    victim_va = cand->va;
    release(&mru_lock);

    // debug("lru swapout: \n");
    // debug("lru swapout: VICTIM : pid : %d , va : %ld, pa: %p\n", victim_pid, victim_va, victim_pa);
    uint64 idx = PA2IDX(victim_pa);
    offset = swap_out(victim_pa, victim_pid, victim_va,mru_map[idx]->ref_cnt);
    if (offset < 0)
    {
        // debug("lru swapout : swapout failed\n");
        return 0;
    }
    acquire(&mru_lock);
    // debug("lru swapout successful; OFFSET: %d\n",offset);
    victim_proc = find_proc(victim_pid);
    if (victim_proc)
    {
        // debug("successfully found the victim proc\n");
        pte_t *victim_ptep = walk(victim_proc->pagetable, victim_va, 0);
        if (victim_ptep && (*victim_ptep & PTE_V) && PTE2PA(*victim_ptep) == (uint64)victim_pa)
        {
            *victim_ptep = PTE_SWAP_SET_OFFSET(offset);
            victim_proc->pst.num_swap_outs++;
        }
        // debug("updated ptes\n");
    }
    __move_to_end_unlocked(victim_pa);
    release(&mru_lock);

    return victim_pa;
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
            printf("PID: %d  | VA: %d  | PA: %p \n", tmp->pid, tmp->va, tmp->pa);
            tmp = tmp->next;
            i++;
        }
    }
    else
    {
        tmp = end;
        while (i < n && tmp)
        {
            printf("PID: %d  | VA: %d  | PA: %p \n", tmp->pid, tmp->va, tmp->pa);
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
  if (idx >= NUM_PAGES || mru_map[idx]->ref_cnt < 1) {
    release(&mru_lock);
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