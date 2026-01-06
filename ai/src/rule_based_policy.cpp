#include "rule_based_policy.hpp"
#include "contrast/rules.hpp"
#include "contrast/board.hpp"
#include "contrast/move_list.hpp"
#include <chrono>
#include <algorithm>
#include <limits>

namespace contrast_ai {

RuleBasedPolicy::RuleBasedPolicy() 
  : rng_(std::chrono::steady_clock::now().time_since_epoch().count()) {
}

bool RuleBasedPolicy::is_winning_move(const contrast::GameState& s, const contrast::Move& m) {
  contrast::GameState next = s;
  next.apply_move(m);
  return contrast::Rules::is_win(next, s.current_player());
}

bool RuleBasedPolicy::can_opponent_win(const contrast::GameState& s) {
  return false; // Not used in new strategy
}

void RuleBasedPolicy::find_blocking_moves(const contrast::GameState& s, contrast::MoveList& out) {
  // Not used in new strategy
}

int RuleBasedPolicy::forward_score(const contrast::Move& m, contrast::Player player) {
  int row_delta = m.dy - m.sy;
  int forward_value = (player == contrast::Player::Black) ? row_delta : -row_delta;
  
  // This function now needs GameState to calculate distance to empty goal
  // For now, keep simple heuristic - will be replaced by better scoring in pick()
  int goal_row = (player == contrast::Player::Black) ? 4 : 0;
  int dist_before = std::abs(m.sy - goal_row);
  int dist_after = std::abs(m.dy - goal_row);
  
  // Prioritize moving pieces that are already close to goal
  int closeness_bonus = (5 - dist_before) * 10;
  
  // Progress bonus: reward moves that get closer to goal
  int progress = dist_before - dist_after;
  
  return closeness_bonus + forward_value * 5 + progress;
}

int RuleBasedPolicy::central_score(const contrast::Move& m) {
  int dx = std::abs(m.dx - 2);
  int dy = std::abs(m.dy - 2);
  return -(dx + dy);
}

// Helper: Find closest empty goal position for a player
int distance_to_nearest_empty_goal(const contrast::GameState& s, int x, int y, contrast::Player player) {
  int goal_row = (player == contrast::Player::Black) ? 4 : 0;
  
  // Find all empty positions in goal row
  int min_dist = 1000;
  for (int gx = 0; gx < 5; ++gx) {
    if (s.board().at(gx, goal_row).occupant == contrast::Player::None) {
      // Calculate Manhattan distance to this empty goal position
      int dist = std::abs(x - gx) + std::abs(y - goal_row);
      if (dist < min_dist) {
        min_dist = dist;
      }
    }
  }
  
  // If all goal positions are occupied, just use distance to goal row
  if (min_dist == 1000) {
    min_dist = std::abs(y - goal_row);
  }
  
  return min_dist;
}

// Helper: Get minimum distance to nearest empty goal position for a player
int min_distance_to_empty_goal(const contrast::GameState& s, contrast::Player player) {
  int min_dist = 1000;
  
  for (int x = 0; x < 5; ++x) {
    for (int y = 0; y < 5; ++y) {
      if (s.board().at(x, y).occupant == player) {
        int dist = distance_to_nearest_empty_goal(s, x, y, player);
        if (dist < min_dist) {
          min_dist = dist;
        }
      }
    }
  }
  
  return min_dist;
}

// Better scoring function that considers empty goal positions
int score_move_to_empty_goal(const contrast::GameState& s, const contrast::Move& m, contrast::Player player) {
  int goal_row = (player == contrast::Player::Black) ? 4 : 0;
  
  // Distance to nearest empty goal before and after move
  int dist_before = distance_to_nearest_empty_goal(s, m.sx, m.sy, player);
  int dist_after = distance_to_nearest_empty_goal(s, m.dx, m.dy, player);
  
  // How much closer to an empty goal position
  int progress = dist_before - dist_after;
  
  // STRONG bonus for moving pieces that are FAR from goal (bring up rear pieces)
  // This encourages multi-piece attacks instead of single-piece rushes
  int rear_bonus = dist_before * 30;  // Farther pieces get higher priority
  
  // Row progress (still important for general forward movement)
  int row_delta = m.dy - m.sy;
  int row_progress = (player == contrast::Player::Black) ? row_delta : -row_delta;
  
  // Penalty for lateral movement (not moving toward goal)
  int lateral_penalty = 0;
  if (row_progress <= 0) {
    lateral_penalty = -50;  // Discourage moves that don't advance
  }
  
  return rear_bonus + progress * 25 + row_progress * 40 + lateral_penalty;
}

// Helper: Check if player has a piece near goal (goal-1 or goal+1)
bool has_piece_near_goal(const contrast::GameState& s, contrast::Player player, int& out_x, int& out_y) {
  int goal_row = (player == contrast::Player::Black) ? 4 : 0;
  int near_row_1 = (player == contrast::Player::Black) ? 3 : 1;
  int near_row_2 = (player == contrast::Player::Black) ? 2 : 2; // Alternative near position
  
  for (int x = 0; x < 5; ++x) {
    for (int y = 0; y < 5; ++y) {
      if (s.board().at(x, y).occupant == player) {
        if (y == near_row_1 || y == near_row_2) {
          out_x = x;
          out_y = y;
          return true;
        }
      }
    }
  }
  return false;
}

// Helper: Find blocking moves against opponent
void find_block_moves(const contrast::GameState& s, contrast::Player opponent, 
                      contrast::MoveList& all_moves, contrast::MoveList& block_moves) {
  int opp_goal = (opponent == contrast::Player::Black) ? 4 : 0;
  
  // Find opponent pieces close to goal
  for (int x = 0; x < 5; ++x) {
    for (int y = 0; y < 5; ++y) {
      if (s.board().at(x, y).occupant == opponent) {
        int dist = std::abs(y - opp_goal);
        if (dist <= 2) {
          // Try to move near this opponent piece to block
          for (size_t i = 0; i < all_moves.size; ++i) {
            int dx = std::abs(all_moves[i].dx - x);
            int dy = std::abs(all_moves[i].dy - y);
            if (dx <= 1 && dy <= 1) {
              block_moves.push_back(all_moves[i]);
            }
          }
        }
      }
    }
  }
}

// Helper: Select best blocking move (prioritize blocking closest threats)
contrast::Move select_best_block_move(const contrast::GameState& s, 
                                       contrast::Player opponent,
                                       contrast::MoveList& block_moves) {
  if (block_moves.empty()) {
    return contrast::Move();
  }
  
  int opp_goal = (opponent == contrast::Player::Black) ? 4 : 0;
  
  // Score each blocking move based on:
  // 1. How close the blocked opponent piece is to goal (higher priority)
  // 2. How directly it blocks the path
  const contrast::Move* best_move = &block_moves[0];
  int best_score = -1000;
  
  for (size_t i = 0; i < block_moves.size; ++i) {
    const auto& m = block_moves[i];
    
    // Find the closest opponent piece to this move's destination
    int min_opp_dist = 100;
    int closest_opp_x = -1, closest_opp_y = -1;
    
    for (int x = 0; x < 5; ++x) {
      for (int y = 0; y < 5; ++y) {
        if (s.board().at(x, y).occupant == opponent) {
          int dx = std::abs(m.dx - x);
          int dy = std::abs(m.dy - y);
          if (dx <= 1 && dy <= 1) {
            int opp_goal_dist = std::abs(y - opp_goal);
            if (opp_goal_dist < min_opp_dist) {
              min_opp_dist = opp_goal_dist;
              closest_opp_x = x;
              closest_opp_y = y;
            }
          }
        }
      }
    }
    
    if (closest_opp_x != -1) {
      // Score: higher for blocking pieces closer to goal
      int threat_score = (5 - min_opp_dist) * 100;
      
      // Bonus for blocking directly in path to goal
      bool blocks_path = false;
      if (opponent == contrast::Player::Black) {
        // Black moves toward y=4, so block if we're between piece and goal
        if (m.dy > closest_opp_y && m.dy <= 4) {
          blocks_path = true;
        }
      } else {
        // White moves toward y=0, so block if we're between piece and goal
        if (m.dy < closest_opp_y && m.dy >= 0) {
          blocks_path = true;
        }
      }
      
      int path_bonus = blocks_path ? 50 : 0;
      int score = threat_score + path_bonus;
      
      if (score > best_score) {
        best_score = score;
        best_move = &block_moves[i];
      }
    }
  }
  
  return *best_move;
}

contrast::Move RuleBasedPolicy::pick(const contrast::GameState& s) {
  contrast::MoveList moves;
  contrast::Rules::legal_moves(s, moves);
  
  if (moves.empty()) {
    return contrast::Move();
  }
  
  contrast::Player me = s.current_player();
  contrast::Player opp = (me == contrast::Player::Black) ? contrast::Player::White : contrast::Player::Black;
  
  // Priority 1: Check if I can win immediately
  for (size_t i = 0; i < moves.size; ++i) {
    if (is_winning_move(s, moves[i])) {
      return moves[i];
    }
  }
  
  // Priority 2: Block opponent's immediate winning move (distance = 1 only)
  int opp_min_dist = min_distance_to_empty_goal(s, opp);
  if (opp_min_dist == 1) {
    // Opponent can win next turn, must block
    contrast::MoveList block_moves;
    find_block_moves(s, opp, moves, block_moves);
    
    if (!block_moves.empty()) {
      return select_best_block_move(s, opp, block_moves);
    }
  }
  
  // Priority 3: Move rear pieces forward for multi-piece attack
  // Score all moves and pick the best one
  const contrast::Move* best = &moves[0];
  int best_score = score_move_to_empty_goal(s, moves[0], me);
  
  for (size_t i = 1; i < moves.size; ++i) {
    int score = score_move_to_empty_goal(s, moves[i], me);
    if (score > best_score) {
      best_score = score;
      best = &moves[i];
    }
  }
  
  return *best;
}

} // namespace contrast_ai
