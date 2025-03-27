
#include "nmt/nMemoryLimitPrinter.hpp"
#include "utilities/vmError.hpp"

bool NMemoryLimitPrinter::total_limit_reached(size_t s, size_t so_far, const nmMemlimit* limit, const NMemType type) {

  const char* typeStr = NMemLimitHandler::NMemTypeToString(type);

  #define FORMATTED \
  "%sLimit: reached global limit (triggering allocation size: " PROPERFMT ", allocated so far: " PROPERFMT ", limit: " PROPERFMT ") ", \
  typeStr, PROPERFMTARGS(s), PROPERFMTARGS(so_far), PROPERFMTARGS(limit->sz)

  // If we hit the limit during error reporting, we print a short warning but otherwise ignore it.
  // We don't want to risk recursive assertion or torn hs-err logs.
  if (VMError::is_error_reported()) {
    // Print warning, but only the first n times to avoid flooding output.
    static int stopafter = 10;
    if (stopafter-- > 0) {
      log_warning(nmt)(FORMATTED);
    }
    return false;
  }

  if (limit->mode == NMemLimitMode::trigger_fatal) {
    fatal(FORMATTED);
  } else {
    log_warning(nmt)(FORMATTED);
  }
#undef FORMATTED

  return true;
}

