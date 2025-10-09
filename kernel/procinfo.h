#include "param.h"
struct procinfo {
  int pid;
  int ppid;
  int state;
  int sz;  //in bytes
  char name[PROC_NAME_SZ];
  int cputicks;           // Total CPU time in ticks 
};
