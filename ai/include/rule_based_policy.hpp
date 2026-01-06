#pragma once
#include "contrast/game_state.hpp"
#include "contrast/move.hpp"
#include "contrast/move_list.hpp"
#include <vector>
#include <random>

namespace contrast_ai {

// Rule-based policy with strategic heuristics
// Priority order:
// 1. Winning move
// 2. Block opponent's winning move
// 3. Move toward goal (forward progress)
// 4. Central control
// 5. Random from remaining moves
class RuleBasedPolicy {
public:
  RuleBasedPolicy();
  contrast::Move pick(const contrast::GameState& s);
  
private:
  std::mt19937 rng_;
  
  // Check if a move leads to a win
  bool is_winning_move(const contrast::GameState& s, const contrast::Move& m);
  
  // Check if opponent can win on next turn
  bool can_opponent_win(const contrast::GameState& s);
  
  // Find moves that block opponent's winning moves
  void find_blocking_moves(const contrast::GameState& s, contrast::MoveList& out);
  
  // Score a move based on forward progress
  int forward_score(const contrast::Move& m, contrast::Player player);
  
  // Score a move based on central control
  int central_score(const contrast::Move& m);
};

} // namespace contrast_ai
