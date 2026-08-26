/*
 * Dedicated microbenchmark (gtest) for ShenandoahPhysicalMemoryManager::remap_virt
 * (mremap + re-mmap of the source hole) vs Copy::aligned_conjoint_words (the
 * memmove path used by full-GC compaction).
 *
 * Runs the primitives directly -- does NOT depend on the (WIP) humongous slide
 * logic or a running heap. Uses an A<->B ping-pong so there is zero untimed
 * setup per iteration and pages stay resident.
 *
 * Run only this benchmark:
 *   make test TEST="gtest:ShenandoahRemapVirtBench*"
 * or from the built image:
 *   gtestLauncher --gtest_filter='ShenandoahRemapVirtBench*'
 *
 * Linux-only: remap_virt is implemented via mremap in the linux os layer.
 */

#include "utilities/globalDefinitions.hpp"

#ifdef LINUX

#include "gc/shenandoah/shenandoahPhysicalMemoryManager.hpp"
#include "memory/allocation.hpp"
#include "runtime/os.hpp"
#include "utilities/copy.hpp"
#include "utilities/ostream.hpp"
#include "unittest.hpp"

#include <sys/mman.h>
#include <stdlib.h>

// ---- helpers ---------------------------------------------------------------

static void touch_pages(char* p, size_t n) {
  const size_t pg = (size_t)os::vm_page_size();
  for (size_t i = 0; i < n; i += pg) {
    ((volatile char*)p)[i] = (char)1;
  }
}

static int cmp_jlong(const void* a, const void* b) {
  jlong x = *(const jlong*)a, y = *(const jlong*)b;
  return (x < y) ? -1 : (x > y) ? 1 : 0;
}

static jlong median_ns(jlong* s, int count) {
  qsort(s, count, sizeof(jlong), cmp_jlong);
  return s[count / 2];
}

// Bound total work: fewer iterations for bigger regions. Always odd for median.
static int iters_for(size_t n) {
  const size_t MiB = 1024 * 1024;
  int it = (int)((256 * MiB) / n);
  if (it < 21)  it = 21;
  if (it > 201) it = 201;
  return (it % 2 == 0) ? it + 1 : it;
}

// ---- remap_virt ping-pong --------------------------------------------------

static jlong bench_remap_virt(size_t n) {
  const int iters = iters_for(n);
  const int warmup = iters / 4;

  // One 2*n reservation -> two non-overlapping slots A (low) and B (high).
  char* base = (char*)mmap(nullptr, 2 * n, PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  EXPECT_NE(base, (char*)MAP_FAILED);
  if (base == (char*)MAP_FAILED) return -1;
  char* A = base;
  char* B = base + n;
  touch_pages(A, n);

  jlong* samples = NEW_C_HEAP_ARRAY(jlong, iters, mtTest);
  char* cur = A; char* other = B;
  for (int it = 0; it < warmup + iters; ++it) {
    jlong t0 = os::javaTimeNanos();
    bool ok = ShenandoahPhysicalMemoryManager::remap_virt(cur, other, n);
    jlong t1 = os::javaTimeNanos();
    EXPECT_TRUE(ok);
    char* tmp = cur; cur = other; other = tmp;   // swap slots
    if (it >= warmup) samples[it - warmup] = t1 - t0;
  }
  jlong med = median_ns(samples, iters);
  FREE_C_HEAP_ARRAY(samples);  // placeholder to keep uniqueness
  munmap(base, 2 * n);
  return med;
}

// ---- Copy::aligned_conjoint_words (memmove) ping-pong ----------------------

static jlong bench_memmove(size_t n) {
  const int iters = iters_for(n);
  const int warmup = iters / 4;

  char* A = (char*)mmap(nullptr, n, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  char* B = (char*)mmap(nullptr, n, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  EXPECT_NE(A, (char*)MAP_FAILED);
  EXPECT_NE(B, (char*)MAP_FAILED);
  if (A == (char*)MAP_FAILED || B == (char*)MAP_FAILED) return -1;
  touch_pages(A, n);
  touch_pages(B, n);

  const size_t words = n / HeapWordSize;
  jlong* samples = NEW_C_HEAP_ARRAY(jlong, iters, mtTest);
  char* cur = A; char* other = B;
  for (int it = 0; it < warmup + iters; ++it) {
    jlong t0 = os::javaTimeNanos();
    Copy::aligned_conjoint_words((HeapWord*)cur, (HeapWord*)other, words);
    jlong t1 = os::javaTimeNanos();
    char* tmp = cur; cur = other; other = tmp;
    if (it >= warmup) samples[it - warmup] = t1 - t0;
  }
  jlong med = median_ns(samples, iters);
  FREE_C_HEAP_ARRAY(samples);
  munmap(A, n);
  munmap(B, n);
  return med;
}

// ---- ring variant: defeat cache warmth -------------------------------------
//
// Ping-pong keeps the whole 2*n working set resident in L2/L3 for small n, so
// memmove looks artificially fast. Instead cycle a (src, dst) pair through K
// slots spanning a footprint far larger than the LLC, holding the two ends half
// a ring apart. Any slot is only revisited every K/2 iterations, by which point
// it has been evicted -> memmove sees a COLD source & destination (the real GC
// case). remap_virt is unaffected (it never reads the data), which is the point.

static const size_t RING_FOOTPRINT = 256 * 1024 * 1024;  // >> any LLC

// Number of n-sized slots so K*n ~= RING_FOOTPRINT; even, >= 4, capped.
static int slots_for(size_t n) {
  int k = (int)(RING_FOOTPRINT / n);
  if (k < 4)    k = 4;
  if (k > 2048) k = 2048;
  return (k % 2 == 0) ? k : k + 1;
}

static jlong bench_remap_virt_ring(size_t n) {
  const int K = slots_for(n);
  const int half = K / 2;
  const int iters = iters_for(n);
  const int warmup = 5;

  char* arena = (char*)mmap(nullptr, (size_t)K * n, PROT_READ | PROT_WRITE,
                            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  EXPECT_NE(arena, (char*)MAP_FAILED);
  if (arena == (char*)MAP_FAILED) return -1;
  touch_pages(arena, (size_t)K * n);

  jlong* samples = NEW_C_HEAP_ARRAY(jlong, iters, mtTest);
  for (int it = 0; it < warmup + iters; ++it) {
    char* src = arena + (size_t)(it % K) * n;
    char* dst = arena + (size_t)((it + half) % K) * n;
    jlong t0 = os::javaTimeNanos();
    bool ok = ShenandoahPhysicalMemoryManager::remap_virt(src, dst, n);
    jlong t1 = os::javaTimeNanos();
    EXPECT_TRUE(ok);
    if (it >= warmup) samples[it - warmup] = t1 - t0;
  }
  jlong med = median_ns(samples, iters);
  FREE_C_HEAP_ARRAY(samples);
  munmap(arena, (size_t)K * n);
  return med;
}

static jlong bench_memmove_ring(size_t n) {
  const int K = slots_for(n);
  const int half = K / 2;
  const int iters = iters_for(n);
  const int warmup = 5;

  char* arena = (char*)mmap(nullptr, (size_t)K * n, PROT_READ | PROT_WRITE,
                            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  EXPECT_NE(arena, (char*)MAP_FAILED);
  if (arena == (char*)MAP_FAILED) return -1;
  touch_pages(arena, (size_t)K * n);

  const size_t words = n / HeapWordSize;
  jlong* samples = NEW_C_HEAP_ARRAY(jlong, iters, mtTest);
  for (int it = 0; it < warmup + iters; ++it) {
    char* src = arena + (size_t)(it % K) * n;
    char* dst = arena + (size_t)((it + half) % K) * n;
    jlong t0 = os::javaTimeNanos();
    Copy::aligned_conjoint_words((HeapWord*)src, (HeapWord*)dst, words);
    jlong t1 = os::javaTimeNanos();
    if (it >= warmup) samples[it - warmup] = t1 - t0;
  }
  jlong med = median_ns(samples, iters);
  FREE_C_HEAP_ARRAY(samples);
  munmap(arena, (size_t)K * n);
  return med;
}

// ---- the benchmark ---------------------------------------------------------

TEST_VM(ShenandoahRemapVirtBench, memmove_vs_remap_ring) {
  const size_t KiB = 1024, MiB = 1024 * KiB;
  const size_t sizes[] = {
    256 * KiB, 512 * KiB, 1 * MiB, 2 * MiB, 4 * MiB,
    8 * MiB, 16 * MiB, 32 * MiB, 64 * MiB
  };

  tty->print_cr("");
  tty->print_cr("ShenandoahRemapVirtBench  (median ns, %d-MiB ring, cache-COLD)",
                (int)(RING_FOOTPRINT / MiB));
  tty->print_cr("%12s %8s %14s %16s %12s",
                "size", "slots", "memmove(ns)", "remap_virt(ns)", "speedup");
  for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i) {
    size_t n = sizes[i];
    jlong mv = bench_memmove_ring(n);
    jlong rv = bench_remap_virt_ring(n);
    double sp = (rv > 0) ? (double)mv / (double)rv : 0.0;
    char buf[32];
    if (n >= MiB) (void)os::snprintf(buf, sizeof(buf), "%zu MiB", n / MiB);
    else          (void)os::snprintf(buf, sizeof(buf), "%zu KiB", n / KiB);
    tty->print_cr("%12s %8d %14ld %16ld %10.1fx",
                  buf, slots_for(n), (long)mv, (long)rv, sp);
  }
  tty->print_cr("");
}

TEST_VM(ShenandoahRemapVirtBench, memmove_vs_remap) {
  const size_t KiB = 1024, MiB = 1024 * KiB;
  const size_t sizes[] = {
    256 * KiB, 512 * KiB, 1 * MiB, 2 * MiB, 4 * MiB,
    8 * MiB, 16 * MiB, 32 * MiB, 64 * MiB
  };

  tty->print_cr("");
  tty->print_cr("ShenandoahRemapVirtBench  (median ns, A<->B ping-pong, cache-warm)");
  tty->print_cr("%12s %14s %16s %12s", "size", "memmove(ns)", "remap_virt(ns)", "speedup");
  for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i) {
    size_t n = sizes[i];
    jlong mv = bench_memmove(n);
    jlong rv = bench_remap_virt(n);
    double sp = (rv > 0) ? (double)mv / (double)rv : 0.0;
    char buf[32];
    if (n >= MiB) (void)os::snprintf(buf, sizeof(buf), "%zu MiB", n / MiB);
    else          (void)os::snprintf(buf, sizeof(buf), "%zu KiB", n / KiB);
    tty->print_cr("%12s %14ld %16ld %10.1fx", buf, (long)mv, (long)rv, sp);
  }
  tty->print_cr("");
}

#endif // LINUX
