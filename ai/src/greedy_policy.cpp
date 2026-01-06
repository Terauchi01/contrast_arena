#include "contrast_ai/greedy_policy.hpp"
#include "contrast/rules.hpp"
#include "contrast/board.hpp"
#include "contrast/move_list.hpp"
#include <chrono>
#include <algorithm>

namespace contrast_ai {

GreedyPolicy::GreedyPolicy() 
  : rng_(std::chrono::steady_clock::now().time_since_epoch().count()) {
}

contrast::Move GreedyPolicy::pick(const contrast::GameState& s) {
  contrast::MoveList moves;
  contrast::Rules::legal_moves(s, moves);
  if (moves.empty()) {
    return contrast::Move(); // No legal move
  }
  
  // Determine forward direction based on current player
  // Black: wants to move down (increase row: toward row 4)
  // White: wants to move up (decrease row: toward row 0)
  int forward_direction = (s.current_player() == contrast::Player::Black) ? 1 : -1;
  
  // Separate moves into base moves (no tile placement)
  contrast::MoveList base_moves;
  for (size_t i = 0; i < moves.size; ++i) {
    if (!moves[i].place_tile) {
      base_moves.push_back(moves[i]);
    }
  }
  
  // If no base moves, fall back to all moves
  if (base_moves.empty()) {
    base_moves = moves;
  }
  
  // Find moves that go forward
  contrast::MoveList forward_moves;
  for (size_t i = 0; i < base_moves.size; ++i) {
    const auto& m = base_moves[i];
    int row_delta = m.dy - m.sy;
    if ((forward_direction > 0 && row_delta > 0) || 
        (forward_direction < 0 && row_delta < 0)) {
      forward_moves.push_back(m);
    }
  }
  
  // If forward moves exist, pick one randomly
  if (!forward_moves.empty()) {
    std::uniform_int_distribution<size_t> dist(0, forward_moves.size - 1);
    return forward_moves[dist(rng_)];
  }
  
  // Otherwise, pick any base move randomly
  std::uniform_int_distribution<size_t> dist(0, base_moves.size - 1);
  return base_moves[dist(rng_)];
}

} // namespace contrast_ai
