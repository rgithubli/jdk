
#ifndef SHARE_NMT_NMEMORYLIMITPRINTER_HPP
#define SHARE_NMT_NMEMORYLIMITPRINTER_HPP

#include "memory/allStatic.hpp"
#include "nmt/nMemLimit.hpp"

class NMemoryLimitPrinter : public AllStatic {

public:
  static bool total_limit_reached(size_t s, size_t so_far, const nmMemlimit* limit, const NMemType type);
};

#endif