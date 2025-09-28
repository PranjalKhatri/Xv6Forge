#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/kalloc.h"

struct pagestat {
  int num_page_faults;
  int num_swap_ins;
  int num_swap_outs;
};

uint random(uint seed) {
  return (seed * 1664525 + 1013904223);
}

int dump_pages = 5;
int current_policy = MRU_POLICY;  // Default to MRU

void print_banner(const char* title) {
  printf("\n=== %s ===\n", title);
}

void print_policy_info() {
  if (current_policy == LRU_POLICY) {
    printf("Replacement Policy: LRU (Least Recently Used)\n");
    printf("-> Evicts pages that haven't been used for the longest time\n");
  } else {
    printf("Replacement Policy: MRU (Most Recently Used)\n");
    printf("-> Evicts pages that were used most recently\n");
  }
}

void print_stats(const char* event, struct pagestat* stats) {
  printf("%s: Faults=%d, SwapOuts=%d, SwapIns=%d\n", 
         event, stats->num_page_faults, stats->num_swap_outs, stats->num_swap_ins);
}

void print_mru_list() {
  if (current_policy == LRU_POLICY) {
    printf("\nPage Order (LRU->MRU, showing last %d pages):\n", dump_pages);
    dumpmru(-dump_pages);  // Print last N pages for LRU
  } else {
    printf("\nPage Order (MRU->LRU, showing first %d pages):\n", dump_pages);
    dumpmru(dump_pages);   // Print first N pages for MRU
  }
}

int main(int argc, char *argv[]) {
  if(argc < 2){
    printf("USAGE: mrumem [numpages] [[dumps=5]] [[rep_policy=MRU/LRU]]\n");
    printf("\nExamples:\n");
    printf("  mrumem 50           # Test with 50 pages, show 5 pages, use MRU\n");
    printf("  mrumem 100 10       # Test with 100 pages, show 10 pages, use MRU\n");
    printf("  mrumem 75 8 LRU     # Test with 75 pages, show 8 pages, use LRU\n");
    return 1;
  }

  int num_pages = atoi(argv[1]);
  
  if(argc >= 3){
    dump_pages = atoi(argv[2]);
  }
  
  if(argc >= 4){
    if(strcmp(argv[3],"LRU") == 0){
      current_policy = LRU_POLICY;
      setreplacement_policy(LRU_POLICY);
    } else {
      current_policy = MRU_POLICY;
      setreplacement_policy(MRU_POLICY);
    }
  }

  const int page_size = 4096;
  const int alloc_size = num_pages * page_size;
  char *memory;
  int mypid = getpid();
  struct pagestat stats;

  print_banner("MEMORY STRESS TEST");
  printf("Test Configuration:\n");
  printf("  Number of pages: %d (%d KB)\n", num_pages, alloc_size/1024);
  printf("  Pages to display: %d\n", dump_pages);
  printf("  Process ID: %d\n", mypid);
  print_policy_info();

  printf("\nAllocating virtual memory...\n");
  memory = sbrklazy(alloc_size);
  if (memory == (char*)-1) {
    printf("ERROR: sbrk failed to allocate %d bytes\n", alloc_size);
    exit(1);
  }
  printf("SUCCESS: Allocated %d pages (%d KB)\n", num_pages, alloc_size/1024);

  // =================================================================
  // PHASE 1: Write to every page
  // =================================================================
  print_banner("PHASE 1: SEQUENTIAL WRITE TEST");
  printf("Writing unique values to all pages...\n");
  printf("This will trigger page faults and potential swap-outs.\n\n");
  
  for (int i = 1; i <= num_pages; i++) {
    char *page_addr = memory + ((i-1) * page_size);
    *page_addr = (char)((i-1) % 256);
    
    if (i % 25 == 0 || i == num_pages) {
      getpagestat(mypid, &stats);
      printf("After writing page %d: ", i);
      print_stats("", &stats);
      
      if (i % 50 == 0 || i == num_pages) {
        print_mru_list();
        printf("\n");
      }
    }
  }

  printf("PHASE 1 COMPLETE: All %d pages written\n", num_pages);

  // =================================================================
  // PHASE 2: Random read test
  // =================================================================
  print_banner("PHASE 2: RANDOM READ VERIFICATION");
  printf("Reading from pages randomly to verify data integrity...\n");
  printf("This will trigger swap-ins for evicted pages.\n\n");

  int errors = 0;
  uint seed = uptime(); 

  for (int i = 1; i <= num_pages; i++) {
    seed = random(seed);
    int page_to_check = seed % num_pages;
    
    char *page_addr = memory + (page_to_check * page_size);
    char expected_value = (char)(page_to_check % 256);
    char actual_value = *page_addr;

    if (actual_value != expected_value) {
      printf("ERROR: Data corruption on page %d: expected %d, got %d\n", 
             page_to_check, expected_value, actual_value);
      errors++;
    }
    
    if (i % 25 == 0 || i == num_pages) {
      getpagestat(mypid, &stats);
      printf("After %d random reads: ", i);
      print_stats("", &stats);
      
      if (i % 50 == 0 || i == num_pages) {
        print_mru_list();
        printf("\n");
      }
    }
  }

  printf("PHASE 2 COMPLETE: %d random reads performed\n", num_pages);

  // =================================================================
  // Final Report
  // =================================================================
  print_banner("FINAL TEST RESULTS");
  
  if (errors == 0) {
    printf("SUCCESS: All pages contained correct data!\n");
    printf("No data corruption detected during swap operations.\n");
  } else {
    printf("FAILURE: Found %d pages with corrupted data!\n", errors);
    printf("This indicates a problem with the swapping mechanism.\n");
  }

  getpagestat(mypid, &stats);
  printf("\nFinal Statistics:\n");
  printf("  Process ID:        %d\n", mypid);
  printf("  Policy:            %s\n", (current_policy == LRU_POLICY) ? "LRU" : "MRU");
  printf("  Pages Tested:      %d\n", num_pages);
  printf("  Total Page Faults: %d\n", stats.num_page_faults);
  printf("  Total Swap-Outs:   %d\n", stats.num_swap_outs);
  printf("  Total Swap-Ins:    %d\n", stats.num_swap_ins);

  print_mru_list();
  
  printf("\nTest completed!\n");

  exit(errors == 0 ? 0 : 1);
}