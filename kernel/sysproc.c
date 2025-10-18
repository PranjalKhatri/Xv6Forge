#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "vm.h"
#include "mru.h"
#include "kalloc.h"
#include "procinfo.h"
#include "console.h"

uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
  kexit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return kfork();
}

uint64
sys_wait(void)
{
  uint64 p;
  argaddr(0, &p);
  return kwait(p);
}

uint64
sys_sbrk(void)
{
  uint64 addr;
  int t;
  int n;

  argint(0, &n);
  argint(1, &t);
  addr = myproc()->sz;

  if(t == SBRK_EAGER || n < 0) {
    if(growproc(n) < 0) {
      return -1;
    }
  } else {
    // Lazily allocate memory for this process: increase its memory
    // size but don't allocate memory. If the processes uses the
    // memory, vmfault() will allocate it.
    if(addr + n < addr)
      return -1;
    myproc()->sz += n;
  }
  return addr;
}

uint64
sys_pause(void)
{
  int n;
  uint ticks0;

  argint(0, &n);
  if(n < 0)
    n = 0;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(killed(myproc())){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  argint(0, &pid);
  return kkill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

uint64
sys_getpagestat(void)
{
  int pid;
  uint64 stat_addr; 
  struct proc *p;
  struct pagestat stats;

  argint(0, &pid); 
  argaddr(1, &stat_addr);
  
  p = find_proc(pid); 
  if(p == 0) {
    return -1;
  }

  stats = p->pst;
  if(copyout(p->pagetable, stat_addr, (char *)&stats, sizeof(stats)) < 0) {
    return -1;
  }

  return 0;
}

uint64
sys_dumpmru(void)
{
  int n;
  argint(0,&n);
  mru_dump(n);
  return 0;
}

uint64 
sys_setreplacement_policy(void){
  extern int replacement_policy;
  int policy;
  argint(0,&policy);
  if(policy != MRU_POLICY && policy != LRU_POLICY){
    return -1;
  }
  replacement_policy = policy;
  return 0;
}


/*
* The user program passes a pointer to an array of struct procinfo.
* This system call fills that array with information about all
* active processes.
*/
uint64 sys_getprocinfo(void)
{
  extern struct proc proc[NPROC];
  uint64 user_addr;
  struct procinfo k_procinfo[NPROC];
  int num_procs = 0;

  argaddr(0, &user_addr);

  for (struct proc *p = proc; p < &proc[NPROC]; p++)
  {
    acquire(&p->lock);

    if (p->state == UNUSED)
    {
      release(&p->lock);
      continue;
    }

    if (num_procs >= NPROC)
    {
      release(&p->lock);
      break;
    }

    k_procinfo[num_procs].pid = p->pid;
    k_procinfo[num_procs].sz = p->sz;
    safestrcpy(k_procinfo[num_procs].name, p->name, PROC_NAME_SZ);
    k_procinfo[num_procs].state = p->state;
    k_procinfo[num_procs].ppid = p->parent ? p->parent->pid : 0;
    k_procinfo[num_procs].cputicks = p->cputicks;
    num_procs++;
    release(&p->lock);
  }

  if (copyout(myproc()->pagetable, user_addr, (char *)k_procinfo,
              num_procs * sizeof(struct procinfo)) < 0)
    return -1;

  return num_procs;
}

uint64 
sys_setconsmode(void){
  int mode;
  argint(0,&mode);
  if(mode != CONS_RAW && mode != CONS_BUFFERED){
    return -1;
  }
  setconsMode(mode);
  return 0;
}
uint64
sys_ConsSetFlag(void){
  int flg, set;
  argint(0, &flg);
  argint(1, &set);
  ConsSetFlag(flg, set);
  return 0;
}
uint64
sys_GetConsState(void){
  uint64 state;
  struct cons_state kstate;
  argaddr(0, &state);
  if(state == 0 || state % sizeof(struct cons_state) != 0){
    return -1;
  }
  GetConsState(&kstate);
  if(copyout(myproc()->pagetable, state, (char*)&kstate, sizeof(struct cons_state)) < 0){
    return -1;
  }
  return 0;
}

uint64
sys_SetConsState(void){
  uint64 state;
  struct cons_state kstate;
  argaddr(0, &state);
  if(state == 0 || state % sizeof(struct cons_state) != 0){
    return -1;
  }
  if(copyin(myproc()->pagetable, (char*)&kstate, state, sizeof(struct cons_state)) < 0){
    return -1;
  }
  SetConsState(&kstate);
  return 0;
}
