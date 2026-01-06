#include "rule_based_policy2.hpp"
#include "contrast/rules.hpp"
#include "contrast/board.hpp"
#include "contrast/move_list.hpp"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <limits>
#include <vector>

namespace {

using contrast::GameState;
using contrast::Move;
using contrast::MoveList;
using contrast::Player;

int goal_row(Player p) { return (p == Player::Black) ? 4 : 0; }
int home_row(Player p) { return (p == Player::Black) ? 0 : 4; }

int distance_to_nearest_empty_goal(const GameState& s, int x, int y, Player player) {
  int target = goal_row(player);
  int best = 1000;
  for (int gx = 0; gx < 5; ++gx) {
    const auto& cell = s.board().at(gx, target);
    if (cell.occupant == contrast::Player::None) {
      int dist = std::abs(x - gx) + std::abs(y - target);
      best = std::min(best, dist);
    }
  }
  if (best == 1000) {
    best = std::abs(y - target);
  }
  return best;
}

int min_distance_to_empty_goal(const GameState& s, Player player) {
  int best = 1000;
  for (int x = 0; x < 5; ++x) {
    for (int y = 0; y < 5; ++y) {
      if (s.board().at(x, y).occupant == player) {
        best = std::min(best, distance_to_nearest_empty_goal(s, x, y, player));
      }
    }
  }
  return best;
}

int row_progress(Player p, const Move& m) {
  int delta = m.dy - m.sy;
  return (p == Player::Black) ? delta : -delta;
}

int chebyshev_distance(const Move& m) {
  return std::max(std::abs(m.dx - m.sx), std::abs(m.dy - m.sy));
}

struct ColumnInfo {
  int x = 0;
  bool has_friend = false;
  int friend_row = -1;
  int friend_proj = -1;
  bool has_enemy_front = false; // closest enemy column from our側
  int enemy_front_row = -1;
  int enemy_front_proj = -1;
  bool has_enemy_ahead = false; // enemy ahead of current front piece
  int enemy_ahead_row = -1;
  int gap = -1; // empty cells between friend front and first enemy ahead
};

int project_row(Player me, int y, int height) {
  return (me == Player::Black) ? y : (height - 1 - y);
}

std::vector<ColumnInfo> collect_column_info(const GameState& s, Player me, Player opponent) {
  std::vector<ColumnInfo> cols;
  int width = s.board().width();
  int height = s.board().height();
  int dir = (me == Player::Black) ? 1 : -1;

  cols.reserve(width);
  for (int x = 0; x < width; ++x) {
    ColumnInfo info;
    info.x = x;

    if (dir == 1) {
      for (int y = 0; y < height; ++y) {
        const auto occ = s.board().at(x, y).occupant;
        if (occ == me) {
          info.has_friend = true;
          info.friend_row = y;
        }
      }
    } else {
      for (int y = height - 1; y >= 0; --y) {
        const auto occ = s.board().at(x, y).occupant;
        if (occ == me) {
          info.has_friend = true;
          info.friend_row = y;
        }
      }
    }
    if (info.has_friend) {
      info.friend_proj = project_row(me, info.friend_row, height);
    }

    if (dir == 1) {
      for (int y = 0; y < height; ++y) {
        const auto occ = s.board().at(x, y).occupant;
        if (occ == opponent) {
          info.has_enemy_front = true;
          info.enemy_front_row = y;
          info.enemy_front_proj = project_row(me, y, height);
          break;
        }
      }
    } else {
      for (int y = height - 1; y >= 0; --y) {
        const auto occ = s.board().at(x, y).occupant;
        if (occ == opponent) {
          info.has_enemy_front = true;
          info.enemy_front_row = y;
          info.enemy_front_proj = project_row(me, y, height);
          break;
        }
      }
    }

    if (info.has_friend) {
      int y = info.friend_row + dir;
      while (y >= 0 && y < height) {
        const auto occ = s.board().at(x, y).occupant;
        if (occ == opponent) {
          info.has_enemy_ahead = true;
          info.enemy_ahead_row = y;
          break;
        }
        if (occ != contrast::Player::None) {
          break;
        }
        y += dir;
      }
      if (info.has_enemy_ahead) {
        info.gap = (dir == 1) ? (info.enemy_ahead_row - info.friend_row - 1)
                              : (info.friend_row - info.enemy_ahead_row - 1);
        if (info.gap < 0) info.gap = 0;
      }
    }

    cols.push_back(info);
  }

  return cols;
}

} // namespace

namespace contrast_ai {

RuleBasedPolicy2::RuleBasedPolicy2()
  : rng_(std::chrono::steady_clock::now().time_since_epoch().count()) {}

bool RuleBasedPolicy2::checkImmediateWin(const GameState& s, const Move& m) const {
  GameState next = s;
  next.apply_move(m);
  return contrast::Rules::is_win(next, s.current_player());
}

bool RuleBasedPolicy2::blockImmediateThreat(const GameState& s,
                                            Player opponent,
                                            const MoveList& moves,
                                            Move& out_move) const {
  if (min_distance_to_empty_goal(s, opponent) > 1) {
    return false;
  }
  for (size_t i = 0; i < moves.size; ++i) {
    GameState next = s;
    next.apply_move(moves[i]);
    if (min_distance_to_empty_goal(next, opponent) > 1) {
      out_move = moves[i];
      return true;
    }
  }
  return false;
}

bool RuleBasedPolicy2::paritySkirmishControl(const GameState& s,
                                             Player me,
                                             Player opponent,
                                             const MoveList& moves,
                                             Move& out_move) const {
  auto columns = collect_column_info(s, me, opponent);
  int total_gap = 0;
  int counted = 0;
  int widest_col = -1;
  int widest_gap = -1;
  for (const auto& col : columns) {
    if (col.gap >= 0) {
      total_gap += col.gap;
      ++counted;
      if (col.gap > widest_gap) {
        widest_gap = col.gap;
        widest_col = col.x;
      }
    }
  }

  if (!counted) {
    return false;
  }

  bool wants_forward = (total_gap % 2 == 1);
  int dir = (me == Player::Black) ? 1 : -1;

  if (wants_forward) {
    const Move* best = nullptr;
    int best_score = 0;
    for (size_t i = 0; i < moves.size; ++i) {
      const auto& m = moves[i];
      if (m.place_tile) continue;
      if (m.dx != m.sx) continue; // keep lane straight
      if (row_progress(me, m) <= 0) continue;

      const auto& col = columns[m.sx];
      if (!col.has_friend || col.friend_row != m.sy) continue; // only push current frontline

      int score = row_progress(me, m) * 120;
      if (col.gap >= 0) score += (col.gap + 1) * 25;
      if (col.has_enemy_ahead) {
        int remaining = (dir == 1) ? (col.enemy_ahead_row - m.dy - 1)
                                   : (m.dy - col.enemy_ahead_row - 1);
        if (remaining < 0) remaining = 0;
        score += std::max(0, 60 - remaining * 15);
      }

      if (score > best_score) {
        best_score = score;
        best = &m;
      }
    }

    if (best) {
      out_move = *best;
      return true;
    }
    return false;
  }

  // Even corridor: insert a tile directly in front of the nearest enemy
  const Move* best = nullptr;
  int best_score = 0;
  int height = s.board().height();
  for (size_t i = 0; i < moves.size; ++i) {
    const auto& m = moves[i];
    if (!m.place_tile) continue;
    if (m.tx < 0 || m.tx >= static_cast<int>(columns.size())) continue;
    const auto& col = columns[m.tx];
    if (!col.has_friend || !col.has_enemy_front) continue;

    int desired_row = col.enemy_front_row - dir; // cell immediately before enemy
    if (desired_row < 0 || desired_row >= height) continue;
    if (m.ty != desired_row) continue;

    int score = 140;
    if (col.gap >= 0) score += col.gap * 12;
    if (m.tx == widest_col) score += 30;
    if (m.tile == contrast::TileType::Gray) score += 30;
    if (m.tile == contrast::TileType::Black) score += 20;

    if (score > best_score) {
      best_score = score;
      best = &m;
    }
  }

  if (best) {
    out_move = *best;
    return true;
  }
  return false;
}

bool RuleBasedPolicy2::interdictRowFormation(const GameState& s,
                                             Player me,
                                             Player opponent,
                                             const MoveList& moves,
                                             Move& out_move) const {
  auto columns = collect_column_info(s, me, opponent);
  int width = s.board().width();
  int dir = (me == Player::Black) ? 1 : -1;

  std::vector<int> targets;
  for (int i = 0; i < width; ++i) {
    const auto& col = columns[i];
    if (!col.has_enemy_front) continue;
    bool irregular = false;
    if (i > 0 && columns[i - 1].has_enemy_front) {
      if (std::abs(columns[i - 1].enemy_front_proj - col.enemy_front_proj) >= 2) {
        irregular = true;
      }
    }
    if (i + 1 < width && columns[i + 1].has_enemy_front) {
      if (std::abs(columns[i + 1].enemy_front_proj - col.enemy_front_proj) >= 2) {
        irregular = true;
      }
    }
    if (irregular) {
      targets.push_back(i);
    }
  }

  if (targets.empty()) {
    int fallback_col = -1;
    int closest = std::numeric_limits<int>::max();
    for (const auto& col : columns) {
      if (!col.has_enemy_front) continue;
      if (col.enemy_front_proj < closest) {
        closest = col.enemy_front_proj;
        fallback_col = col.x;
      }
    }
    if (fallback_col != -1) {
      targets.push_back(fallback_col);
    }
  }

  if (targets.empty()) {
    return false;
  }

  const Move* best = nullptr;
  int best_score = 0;
  for (size_t i = 0; i < moves.size; ++i) {
    const auto& m = moves[i];
    if (!m.place_tile) continue;

    int score = 0;
    for (int idx : targets) {
      if (std::abs(m.tx - idx) > 1) continue;
      const auto& target_col = columns[idx];
      if (!target_col.has_enemy_front) continue;
      int row_diff = std::abs(m.ty - target_col.enemy_front_row);
      score = std::max(score, 80 - row_diff * 15);
      bool ahead = (dir == 1) ? (m.ty >= target_col.enemy_front_row)
                              : (m.ty <= target_col.enemy_front_row);
      if (ahead) score += 20;
    }

    if (score == 0) continue;

    if (m.tile == contrast::TileType::Gray) score += 25;
    if (m.tile == contrast::TileType::Black) score += 15;

    if (score > best_score) {
      best_score = score;
      best = &m;
    }
  }

  if (best) {
    out_move = *best;
    return true;
  }
  return false;
}

bool RuleBasedPolicy2::prioritizeLeadPiece(const GameState& s,
                                           Player me,
                                           const MoveList& moves,
                                           Move& out_move) const {
  Player opponent = (me == Player::Black) ? Player::White : Player::Black;
  auto columns = collect_column_info(s, me, opponent);
  int width = s.board().width();

  const Move* best = nullptr;
  int best_score = 0;
  for (size_t i = 0; i < moves.size; ++i) {
    const auto& m = moves[i];
    if (m.place_tile) continue;
    if (row_progress(me, m) <= 0) continue;
    if (m.dx != m.sx) continue;
    if (m.sx != 0 && m.sx != width - 1) continue; // raise edges first

    const auto& col = columns[m.sx];
    int score = row_progress(me, m) * 110;
    if (col.has_friend && col.friend_row == m.sy) score += 30;
    score += project_row(me, m.dy, s.board().height()) * 5;

    if (score > best_score) {
      best_score = score;
      best = &m;
    }
  }

  if (best) {
    out_move = *best;
    return true;
  }
  return false;
}

bool RuleBasedPolicy2::outflankStraightRunner(const GameState& s,
                                              Player me,
                                              Player opponent,
                                              const MoveList& moves,
                                              Move& out_move) const {
  auto columns = collect_column_info(s, me, opponent);
  int dir = (me == Player::Black) ? 1 : -1;

  int closest_enemy = std::numeric_limits<int>::max();
  for (const auto& col : columns) {
    if (col.has_enemy_front) {
      closest_enemy = std::min(closest_enemy, col.enemy_front_proj);
    }
  }
  if (closest_enemy == std::numeric_limits<int>::max()) {
    return false;
  }

  const Move* best = nullptr;
  int best_score = 0;
  for (size_t i = 0; i < moves.size; ++i) {
    const auto& m = moves[i];
    if (m.place_tile) continue;
    if (row_progress(me, m) <= 0) continue;

    const auto& col = columns[m.sx];
    if (!col.has_enemy_front) continue;
    if (col.enemy_front_proj > closest_enemy + 1) continue;

    int desired_row = col.enemy_front_row - dir;
    int after_gap = (dir == 1) ? (col.enemy_front_row - m.dy - 1)
                               : (m.dy - col.enemy_front_row - 1);
    if (after_gap < 0) after_gap = 0;

    int score = 100 - after_gap * 35;
    if (col.has_friend && col.friend_row == m.sy) score += 30;
    int dist_to_desired = std::abs(m.dy - desired_row);
    score += std::max(0, 40 - dist_to_desired * 15);

    if (score > best_score) {
      best_score = score;
      best = &m;
    }
  }

  if (best) {
    out_move = *best;
    return true;
  }
  return false;
}

contrast::Move RuleBasedPolicy2::fallbackByScore(const GameState& s,
                                                 Player me,
                                                 const MoveList& moves) const {
  Player opponent = (me == Player::Black) ? Player::White : Player::Black;
  const Move* best = &moves[0];
  int best_score = std::numeric_limits<int>::min();

  for (size_t i = 0; i < moves.size; ++i) {
    const auto& m = moves[i];
    int score = row_progress(me, m) * 80;
    score -= distance_to_nearest_empty_goal(s, m.dx, m.dy, me) * 15;

    if (!m.place_tile) {
      if (s.board().at(m.dx, m.dy).occupant == opponent) {
        score += 50;
      }
    } else {
      if (m.tile == contrast::TileType::Gray) score += 30;
      else if (m.tile == contrast::TileType::Black) score += 15;
      score += (std::abs(m.tx - m.sx) <= 1) ? 10 : 0;
    }

    if (score > best_score) {
      best_score = score;
      best = &m;
    }
  }

  return *best;
}

contrast::Move RuleBasedPolicy2::pick(const GameState& s) {
  MoveList moves;
  contrast::Rules::legal_moves(s, moves);

  if (moves.empty()) {
    return contrast::Move();
  }

  Player me = s.current_player();
  Player opp = (me == Player::Black) ? Player::White : Player::Black;

  for (size_t i = 0; i < moves.size; ++i) {
    if (checkImmediateWin(s, moves[i])) {
      return moves[i];
    }
  }

  Move candidate;
  if (blockImmediateThreat(s, opp, moves, candidate)) {
    return candidate;
  }
  if (paritySkirmishControl(s, me, opp, moves, candidate)) {
    return candidate;
  }
  if (interdictRowFormation(s, me, opp, moves, candidate)) {
    return candidate;
  }
  if (prioritizeLeadPiece(s, me, moves, candidate)) {
    return candidate;
  }
  if (outflankStraightRunner(s, me, opp, moves, candidate)) {
    return candidate;
  }

  return fallbackByScore(s, me, moves);
}

} // namespace contrast_ai
