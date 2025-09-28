#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

// This struct must match the one you define in the kernel.
// A good place for the kernel-side definition is kernel/proc.h
struct pagestat {
  int num_page_faults;
  int num_swap_ins;
  int num_swap_outs;
};

// A simple pseudo-random number generator for picking pages
uint random(uint seed) {
  return (seed * 1664525 + 1013904223);
}

int
main(int argc, char *argv[])
{
  // Define the size of memory to allocate.
  // This should be significantly larger than the physical RAM you've
  // configured in memlayout.h to ensure swapping occurs.
  // 200 pages = 200 * 4096 bytes = 819,200 bytes (~800 KB)
  if(argc != 2){
    printf("USAGE: mrumem [numpages]\n");
    return 1;
  }
  int num_pages = atoi(argv[1]);
  const int page_size = 4096;
  const int alloc_size = num_pages * page_size;

  char *memory;
  int mypid = getpid();
  struct pagestat stats;

  printf("mrumem: Starting memory stress test...\n");

  // Allocate a large chunk of memory
  memory = sbrklazy(alloc_size);
  if (memory == (char*)-1) {
    printf("mrumem: sbrk failed to allocate %d bytes\n", alloc_size);
    exit(1);
  }
  printf("mrumem: Successfully allocated %d pages (%d bytes).\n", num_pages, alloc_size);
  
  // =================================================================
  // PHASE 1: Write to every page to force allocation and swapping.
  // =================================================================
  printf("\n--- Phase 1: Writing to all pages ---\n");
  printf("This will trigger page faults and swap-outs if memory is full.\n");

  for (int i = 0; i < num_pages; i++) {
    // Write a unique value to the first byte of each page.
    // The value is based on the page number to verify correctness later.
    char *page_addr = memory + (i * page_size);
    *page_addr = (char)(i % 256); // Store page index (mod 256)
    
    // Periodically print stats
    if (i > 0 && i % 20 == 0) {
      getpagestat(mypid, &stats);
      printf("After writing to page %d:\n", i);
      printf("  Page Faults: %d, Swap-Outs: %d, Swap-Ins: %d\n", 
             stats.num_page_faults, stats.num_swap_outs, stats.num_swap_ins);
    }
  }

  printf("\n--- Phase 1 Complete ---\n");
  printf("All pages have been touched. Let's look at the MRU list.\n");
  dumpmru(5); // Show the MRU list after sequential access
  
  // =================================================================
  // PHASE 2: Read from pages randomly to verify data integrity.
  // =================================================================
  printf("\n--- Phase 2: Reading from pages randomly ---\n");
  printf("This will trigger swap-ins for pages that were evicted.\n");

  int errors = 0;
  uint seed = uptime(); 

  for (int i = 0; i < num_pages; i++) {
    seed = random(seed);
    int page_to_check = seed % num_pages;
    
    char *page_addr = memory + (page_to_check * page_size);
    char expected_value = (char)(page_to_check % 256);
    char actual_value = *page_addr;

    // Verify that the data is still correct
    if (actual_value != expected_value) {
      printf("!!! DATA CORRUPTION ERROR on page %d: expected %d, got %d\n", 
             page_to_check, expected_value, actual_value);
      errors++;
    }

    // Periodically print stats
    if (i > 0 && i % 20 == 0) {
      getpagestat(mypid, &stats);
      printf("After randomly reading page %d:\n", page_to_check);
      printf("  Page Faults: %d, Swap-Outs: %d, Swap-Ins: %d\n", 
             stats.num_page_faults, stats.num_swap_outs, stats.num_swap_ins);
      printf("  Let's see the MRU list now:\n");
      dumpmru(5);
    }
  }

  printf("\n--- Phase 2 Complete ---\n");
  
  // =================================================================
  // Final Report
  // =================================================================
  printf("\n--- Test Report ---\n");
  if (errors == 0) {
    printf("SUCCESS: All pages contained the correct data.\n");
  } else {
    printf("FAILURE: Found %d pages with corrupted data.\n", errors);
  }

  getpagestat(mypid, &stats);
  printf("\nFinal Paging Statistics for pid %d:\n", mypid);
  printf("  Total Page Faults: %d\n", stats.num_page_faults);
  printf("  Total Swap-Outs: %d\n", stats.num_swap_outs);
  printf("  Total Swap-Ins: %d\n", stats.num_swap_ins);

  exit(0);
}