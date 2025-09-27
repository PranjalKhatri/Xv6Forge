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
char *map_start;
static struct mru_node **mru_map;
static struct mru_node *head, *end;
struct spinlock mru_lock;

void *mru_init(void *pa_start, int num_pages, struct mru_node *map[])
{
    printf("mru init start\n");
    initlock(&mru_lock, "mru_lock");
    req_pages = (sizeof(struct mru_node) * num_pages + PGSIZE - 1) / PGSIZE;
    mru_map = map;
    
    char *p = (char *)PGROUNDUP((uint64)pa_start);
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
        map[j]->prev = map[j]->next = 0;
        if (j != 0)
        {
            map[j]->prev = map[j - 1];
            map[j - 1]->next = map[j];
        }
    }
    head = map[0];
    end = map[num_pages - req_pages - 1];
    
    printf("mru init end\n");
    return (char *)PGROUNDUP((uint64)pa_start) + req_pages * PGSIZE;
}
int P2I(void *pa)
{
    return (int)((char *)pa - map_start) / PGSIZE;
}
void map_neighbors(int idx)
{
    if (mru_map[idx]->prev)
        mru_map[idx]->prev->next = mru_map[idx]->next;
    if (mru_map[idx]->next)
        mru_map[idx]->next->prev = mru_map[idx]->prev;
}

void __move_to_end_unlocked(void* pa)
{
    int idx = P2I(pa);
    if(mru_map[idx] == end) return;
    if(mru_map[idx] == head) head=head->next;
    map_neighbors(idx);
    mru_map[idx]->next=0;
    end->next = mru_map[idx];
    mru_map[idx]->prev=end;
    end = mru_map[idx];
}
void move_to_end(void* pa)
{
    acquire(&mru_lock);
    int idx = P2I(pa);
    mru_map[idx]->pid = -1;
    mru_map[idx]->va = 0;
    __move_to_end_unlocked(pa);
    release(&mru_lock);
}

void move_to_head(void *pa)
{
    int idx = P2I(pa);
    if (mru_map[idx] == head)
    {
        release(&mru_lock);
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

void* mru_swapout()
{
    acquire(&mru_lock);
    void* victim_pa = head->pa;
    int victim_pid = head->pid;
    uint64 victim_va = head->va;
    int offset = swap_out(victim_pa, victim_pid, victim_va);
    if(offset >= 0){
        pte_t* victim_ptep = walk(find_proc(victim_pid)->pagetable, victim_va, 0);
        if(victim_ptep)
            *victim_ptep = PTE_SWAP_SET_OFFSET(offset);
    }
    __move_to_end_unlocked(victim_pa);

    release(&mru_lock);
    return victim_pa;
}

struct mru_node *mru_get_end()
{
    return end;
}
