#include "contrast/zobrist.hpp"
#include <random>

using namespace contrast;

Zobrist::Zobrist() {
  std::mt19937_64 rng(0x12345678);
  hash_ = rng();
}
