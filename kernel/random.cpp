#include "random.h"

// Linear Congruential Generator (LCG)
// Simple and fast PRNG suitable for xv6
static uint64 rng_state;
uint64
rdtsc(void)
{
  uint32 lo, hi;
  asm volatile("rdtsc" : "=a" (lo), "=d" (hi));
  return ((uint64)hi << 32) | lo;
}
// Initialize the RNG with a seed
void
srand(uint64 seed)
{
  rng_state = seed;
}
// Generate next random number
uint64
rand(void)
{
  // LCG parameters (same as glibc)
  rng_state = rng_state * 6364136223846793005ULL + 1442695040888963407ULL;
  return rng_state;
}

uint64
rand_range(uint64 max)
{
  return rand() % max;
}