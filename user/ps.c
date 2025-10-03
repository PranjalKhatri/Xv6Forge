#include "kernel/types.h"
#include "user/user.h"
#include "kernel/procinfo.h"

static const char *states[] = {
  [0] "unused",
  [1] "embryo",
  [2] "sleeping",
  [3] "runnable",
  [4] "running",
  [5] "zombie"
};

int
main(int argc, char *argv[])
{
  struct procinfo p_table[64];
  int num_procs;
  num_procs = getprocinfo(p_table);
  if (num_procs < 0) {
    fprintf(2, "ps: getprocinfo failed\n");
    exit(1);
  }
  printf("PID\tPPID\tSTATE\t\tSIZE\tNAME\tCPU\n");

  for (int i = 0; i < num_procs; i++) {
    if (p_table[i].state >= 0 && p_table[i].state < 4&& states[p_table[i].state]) {
      printf("%d\t%d\t%s\t%d\t%s\t%d\n",
        p_table[i].pid,
        p_table[i].ppid,
        states[p_table[i].state],
        p_table[i].sz,
        p_table[i].name,
        p_table[i].cputicks);
    }
  }
  exit(0);
}