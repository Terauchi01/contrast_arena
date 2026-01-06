#pragma once
#include "contrast/move.hpp"
#include <array>
#include <cstddef>

namespace contrast {

// Fixed-size move list for performance
// Max moves: 5 pieces * 8 directions * (1 + 25 tile placements) ≈ 1040
// Using 2048 for safety margin
constexpr size_t MAX_MOVES = 2048;

struct MoveList {
  std::array<Move, MAX_MOVES> moves;
  size_t size = 0;
  
  void clear() { size = 0; }
  void push_back(const Move& m) { 
    if (size < MAX_MOVES) {
      moves[size++] = m;
    }
  }
  
  bool empty() const { return size == 0; }
  
  Move& operator[](size_t i) { return moves[i]; }
  const Move& operator[](size_t i) const { return moves[i]; }
  
  Move* begin() { return &moves[0]; }
  Move* end() { return &moves[size]; }
  const Move* begin() const { return &moves[0]; }
  const Move* end() const { return &moves[size]; }
};

} // namespace contrast
