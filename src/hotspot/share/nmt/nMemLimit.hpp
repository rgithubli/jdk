
#ifndef SHARE_NMT_NMEMLIMIT_HPP
#define SHARE_NMT_NMEMLIMIT_HPP

#include "memory/allStatic.hpp"
#include "nmt/memTag.hpp"
#include "utilities/debug.hpp"
#include "utilities/globalDefinitions.hpp"

enum class NMemType {
  Malloc,
  MMap
};

enum class NMemLimitMode {
  trigger_fatal = 0,
  trigger_oom   = 1
};

struct nmMemlimit { // TODO: change name to NMemLimit
  size_t sz;            // Limit size
  NMemLimitMode mode; // Behavior flags
};

// forward declaration
class outputStream;

class NMemLimitSet {
  nmMemlimit _glob;                    // global limit
  nmMemlimit _cat[mt_number_of_tags]; // per-category limit
public:
  NMemLimitSet();

  void reset();
  bool parse_n_mem_limit_option(const char* optionstring, const char** err);

  void set_global_limit(size_t s, NMemLimitMode type);
  void set_category_limit(MemTag mem_tag, size_t s, NMemLimitMode mode);

  const nmMemlimit* global_limit() const             { return &_glob; }
  const nmMemlimit* category_limit(MemTag mem_tag) const { return &_cat[(int)mem_tag]; }

  void print_on(outputStream* st) const;
};

class NMemLimitHandler : public AllStatic {
protected:
  static NMemType _type; // TODO: use this
  static NMemLimitSet _limits;
  static bool _have_limit; // shortcut

public:

  static const nmMemlimit* global_limit()             { return _limits.global_limit(); }
  static const nmMemlimit* category_limit(MemTag mem_tag) { return _limits.category_limit(mem_tag); }
  static NMemType type() { return _type; }

  static void initialize(const char* options);
  static void print_on(outputStream* st);
  static const char* NMemTypeToString(NMemType type);

  // True if there is any limit established
  static bool have_limit() { return _have_limit; }
};

#endif