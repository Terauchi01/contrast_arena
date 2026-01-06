#pragma once
#include <cstdint>

namespace contrast {

class Zobrist {
public:
  Zobrist();
  uint64_t hash() const { return hash_; }
private:
  uint64_t hash_ = 0;
};

} // namespace contrast
