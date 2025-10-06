#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

struct pagestat {
  int num_page_faults;
  int num_swap_ins;
  int num_swap_outs;
};
struct pagestat stats;

void print_stats(const char* event, struct pagestat* stats) {
  printf("%s: Faults=%d, SwapOuts=%d, SwapIns=%d\n", 
         event, stats->num_page_faults, stats->num_swap_outs, stats->num_swap_ins);
}

// cowtest: a simple program to test copy-on-write fork.
int
main(void)
{
  printf("Starting COW test...\n");

  // Allocate one page of memory
  char *mem = sbrk(4096);
  if(mem == (char*)-1){
    printf("sbrk failed\n");
    exit(1);
  }

  // Write an initial value to the page
  *mem = 'A';
  printf("Parent (pid %d): Wrote initial value 'A' to address %p\n", getpid(), mem);

  int pid = fork();
  if(pid < 0){
    printf("fork failed\n");
    exit(1);
  }

  if(pid == 0){
    // --- Child Process ---
    printf("Child (pid %d): Value at address %p is '%c' (inherited from parent)\n", getpid(), mem, *mem);

    // This write should trigger a page fault and a copy-on-write.
    printf("Child (pid %d): Attempting to write 'B'...\n", getpid());
    *mem = 'B';
    printf("Child (pid %d): Write successful. The new value is '%c'\n", getpid(), *mem);

    printf("Child (pid %d): Exiting.\n", getpid());
    
    printf("child page stats\n");
    getpagestat(getpid(),&stats);
    print_stats("", &stats);
    
    exit(0);
  } else {
    // --- Parent Process ---
    printf("Parent (pid %d): Waiting for child (pid %d) to finish.\n", getpid(), pid);
    wait(0);
    printf("Parent (pid %d): Child finished.\n", getpid());

    // Check if our page was modified. It should not be.
    printf("Parent (pid %d): Reading from address %p...\n", getpid(), mem);
    
    if(*mem == 'A'){
      printf("\nSUCCESS: Parent still sees the original value 'A'. COW works!\n");
    } else {
      printf("\nFAILURE: Parent sees value '%c', but expected 'A'. COW is broken.\n", *mem);
    }
  }

  exit(0);
}