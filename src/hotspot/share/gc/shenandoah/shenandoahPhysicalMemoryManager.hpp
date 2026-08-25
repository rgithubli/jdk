// TODO: copyright
#ifndef SHARE_GC_SHENANDOAH_SHENANDOAHPHYSICALMEMORYMANAGER_HPP
#define SHARE_GC_SHENANDOAH_SHENANDOAHPHYSICALMEMORYMANAGER_HPP

#include "memory/allocation.hpp"

class ShenandoahPhysicalMemoryManager : AllStatic {
  public:
    static bool remap_virt(char* old_virt_addr, char* new_virt_addr, size_t length);
    static bool is_supported();
};
#endif
