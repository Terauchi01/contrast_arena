#include "random_policy.hpp"
#include "contrast/rules.hpp"
#include "contrast/move_list.hpp"
#include <chrono>

namespace contrast_ai {

RandomPolicy::RandomPolicy() 
  : rng_(std::chrono::steady_clock::now().time_since_epoch().count()) {
}

contrast::Move RandomPolicy::pick(const contrast::GameState& s) {
  contrast::MoveList moves;
  contrast::Rules::legal_moves(s, moves);
  if (moves.empty()) {
    return contrast::Move(); // No legal move
  }
  
  std::uniform_int_distribution<size_t> dist(0, moves.size - 1);
  return moves[dist(rng_)];
}

} // namespace contrast_ai
