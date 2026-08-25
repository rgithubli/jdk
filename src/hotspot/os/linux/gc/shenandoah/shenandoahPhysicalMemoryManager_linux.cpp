// TODO: copyright
#include "gc/shenandoah/shenandoahPhysicalMemoryManager.hpp"
#include <sys/mman.h>

bool ShenandoahPhysicalMemoryManager::remap_virt(char* old_virt_addr, char* new_virt_addr, size_t length) {
  // Probably assert
  void* mremap_result = mremap((void*)old_virt_addr, length, length, MREMAP_FIXED | MREMAP_MAYMOVE, new_virt_addr);
  if (mremap_result == MAP_FAILED) {
    return false;
  }

  void* mmap_result = mmap(old_virt_addr, length, PROT_READ | PROT_WRITE, MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  return mmap_result != MAP_FAILED; // TODO: maybe just exit upon failure?
}

bool ShenandoahPhysicalMemoryManager::is_supported() { return true; }
