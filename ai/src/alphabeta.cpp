#include "alphabeta.hpp"
#include "contrast/rules.hpp"
#include "contrast/move_list.hpp"
#include <algorithm>
#include <iostream>
#include <functional>

using namespace contrast_ai;
using namespace contrast;

// ============================================================================
// AlphaBeta implementation
// ============================================================================

AlphaBeta::AlphaBeta() 
  : use_tt_(true), use_move_ordering_(true), verbose_(false) {
  stats_.reset();
}

AlphaBeta::AlphaBeta(const std::string& ntuple_weights_file)
  : use_tt_(true), use_move_ordering_(true), verbose_(false) {
  load_network(ntuple_weights_file);
  stats_.reset();
}

void AlphaBeta::set_network(const NTupleNetwork& network) {
  network_ = network;
}

void AlphaBeta::load_network(const std::string& weights_file) {
  network_.load(weights_file);
}

std::vector<Move> AlphaBeta::get_legal_moves(const GameState& state) const {
  MoveList moves;
  Rules::legal_moves(state, moves);
  
  std::vector<Move> result;
  result.reserve(moves.size);
  for (size_t i = 0; i < moves.size; ++i) {
    result.push_back(moves[i]);
  }
  
  return result;
}

bool AlphaBeta::is_terminal(const GameState& state, float& terminal_value) const {
  MoveList moves;
  Rules::legal_moves(state, moves);
  
  if (moves.empty()) {
    // 手番側の負け
    terminal_value = -10000.0f;
    return true;
  }
  
  if (Rules::is_win(state, Player::Black)) {
    terminal_value = (state.current_player() == Player::Black) ? 10000.0f : -10000.0f;
    return true;
  }
  
  if (Rules::is_win(state, Player::White)) {
    terminal_value = (state.current_player() == Player::White) ? 10000.0f : -10000.0f;
    return true;
  }
  
  return false;
}

float AlphaBeta::evaluate_state(const GameState& state) const {
  return network_.evaluate(state);
}

uint64_t AlphaBeta::compute_hash(const GameState& state) const {
  // 簡易ハッシュ（Zobrist hashの方が良いが、簡易版）
  uint64_t hash = 0;
  const auto& board = state.board();
  
  for (int y = 0; y < board.height(); ++y) {
    for (int x = 0; x < board.width(); ++x) {
      const auto& cell = board.at(x, y);
      hash ^= (static_cast<uint64_t>(cell.occupant) << (y * 5 + x));
      hash ^= (static_cast<uint64_t>(cell.tile) << (25 + y * 5 + x));
    }
  }
  
  hash ^= (static_cast<uint64_t>(state.current_player()) << 50);
  
  return hash;
}

void AlphaBeta::order_moves(std::vector<Move>& moves, const GameState& state) {
  if (!use_move_ordering_ || moves.size() <= 1) {
    return;
  }
  
  // 各手の評価値を計算して降順ソート
  std::vector<std::pair<float, Move>> scored_moves;
  scored_moves.reserve(moves.size());
  
  for (const auto& move : moves) {
    GameState next_state = state;
    next_state.apply_move(move);
    float score = -evaluate_state(next_state);  // Negamax形式
    scored_moves.emplace_back(score, move);
  }
  
  std::sort(scored_moves.begin(), scored_moves.end(),
            [](const auto& a, const auto& b) { return a.first > b.first; });
  
  for (size_t i = 0; i < moves.size(); ++i) {
    moves[i] = scored_moves[i].second;
  }
}

void AlphaBeta::store_tt(uint64_t hash, float value, int depth, 
                         TranspositionEntry::Flag flag, const Move& best_move) {
  if (!use_tt_) return;
  
  tt_[hash] = TranspositionEntry{hash, value, depth, flag, best_move};
}

bool AlphaBeta::probe_tt(uint64_t hash, int depth, float alpha, float beta, 
                         float& value, Move& best_move) {
  if (!use_tt_) return false;
  
  auto it = tt_.find(hash);
  if (it == tt_.end()) {
    return false;
  }
  
  stats_.tt_hits++;
  
  const auto& entry = it->second;
  
  if (entry.depth >= depth) {
    if (entry.flag == TranspositionEntry::Flag::EXACT) {
      value = entry.value;
      best_move = entry.best_move;
      stats_.tt_cutoffs++;
      return true;
    } else if (entry.flag == TranspositionEntry::Flag::LOWER_BOUND && entry.value >= beta) {
      value = entry.value;
      best_move = entry.best_move;
      stats_.tt_cutoffs++;
      return true;
    } else if (entry.flag == TranspositionEntry::Flag::UPPER_BOUND && entry.value <= alpha) {
      value = entry.value;
      best_move = entry.best_move;
      stats_.tt_cutoffs++;
      return true;
    }
  }
  
  // エントリはあるが使えない場合でも、best_moveは参考になる
  if (entry.depth > 0) {
    best_move = entry.best_move;
  }
  
  return false;
}

/**
 * アルファベータ探索の本体（Negamax形式）
 */
float AlphaBeta::alphabeta(const GameState& state, int depth, float alpha, float beta, 
                           bool maximizing, Move& best_move) {
  stats_.nodes_searched++;
  
  // 終局チェック
  float terminal_value;
  if (is_terminal(state, terminal_value)) {
    return terminal_value;
  }
  
  // 深さ制限
  if (depth <= 0) {
    return evaluate_state(state);
  }
  
  // 置換表の確認
  uint64_t hash = compute_hash(state);
  float tt_value;
  Move tt_move;
  if (probe_tt(hash, depth, alpha, beta, tt_value, tt_move)) {
    best_move = tt_move;
    return tt_value;
  }
  
  // 合法手の生成と順序付け
  std::vector<Move> moves = get_legal_moves(state);
  if (moves.empty()) {
    return -10000.0f;  // 負け
  }
  
  order_moves(moves, state);
  
  float best_value = -std::numeric_limits<float>::infinity();
  Move local_best_move = moves[0];
  
  for (const auto& move : moves) {
    GameState next_state = state;
    next_state.apply_move(move);
    
    Move child_best_move;
    float value = -alphabeta(next_state, depth - 1, -beta, -alpha, !maximizing, child_best_move);
    
    if (value > best_value) {
      best_value = value;
      local_best_move = move;
    }
    
    alpha = std::max(alpha, value);
    
    // ベータカット
    if (alpha >= beta) {
      stats_.beta_cutoffs++;
      break;
    }
  }
  
  best_move = local_best_move;
  
  // 置換表に保存
  TranspositionEntry::Flag flag = TranspositionEntry::Flag::EXACT;
  if (best_value <= alpha) {
    flag = TranspositionEntry::Flag::UPPER_BOUND;
  } else if (best_value >= beta) {
    flag = TranspositionEntry::Flag::LOWER_BOUND;
  }
  
  store_tt(hash, best_value, depth, flag, best_move);
  
  return best_value;
}

/**
 * 反復深化（Iterative Deepening）
 * 深さ1から徐々に深くして探索
 */
Move AlphaBeta::iterative_deepening(const GameState& state, int max_depth) {
  Move best_move;
  
  std::cerr << "[AlphaBeta] Iterative deepening up to depth " << max_depth << std::endl;
  
  for (int d = 1; d <= max_depth; ++d) {
    stats_.max_depth_reached = d;
    
    std::cerr << "[AlphaBeta] Searching depth " << d << "/" << max_depth << "..." << std::flush;
    
    Move current_best;
    float value = alphabeta(state, d, -std::numeric_limits<float>::infinity(), 
                           std::numeric_limits<float>::infinity(), true, current_best);
    
    std::cerr << " done! (nodes: " << stats_.nodes_searched << ", value: " << value << ")" << std::endl;
    
    if (verbose_) {
      std::cout << "[AlphaBeta] Depth " << d << " | Value: " << value 
                << " | Nodes: " << stats_.nodes_searched
                << " | TT hits: " << stats_.tt_hits
                << " | Beta cuts: " << stats_.beta_cutoffs << "\n";
    }
    
    best_move = current_best;
  }
  
  return best_move;
}

/**
 * 反復深化（時間制限付き）
 */
Move AlphaBeta::iterative_deepening_time(const GameState& state, 
                                         std::chrono::steady_clock::time_point deadline) {
  Move best_move;
  int depth = 1;
  
  while (std::chrono::steady_clock::now() < deadline) {
    stats_.max_depth_reached = depth;
    
    Move current_best;
    float value = alphabeta(state, depth, -std::numeric_limits<float>::infinity(), 
                           std::numeric_limits<float>::infinity(), true, current_best);
    
    if (verbose_) {
      auto now = std::chrono::steady_clock::now();
      auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - (deadline - std::chrono::milliseconds(stats_.time_ms))).count();
      
      std::cout << "[AlphaBeta] Depth " << depth << " | Value: " << value 
                << " | Nodes: " << stats_.nodes_searched
                << " | TT hits: " << stats_.tt_hits
                << " | Beta cuts: " << stats_.beta_cutoffs
                << " | Time: " << elapsed << "ms\n";
    }
    
    best_move = current_best;
    
    // 時間チェック
    if (std::chrono::steady_clock::now() >= deadline) {
      break;
    }
    
    depth++;
  }
  
  return best_move;
}

/**
 * メイン探索関数（深さまたは時間指定）
 */
Move AlphaBeta::search(const GameState& s, int max_depth, int time_ms) {
  stats_.reset();
  
  // Determine effective time limit (ms). If time_ms <= 0, allow env var CONTRAST_MOVE_TIME (seconds).
  int effective_time_ms = time_ms;
  if (effective_time_ms <= 0) {
    const char* env = std::getenv("CONTRAST_MOVE_TIME");
    if (env) {
      double secs = std::atof(env);
      if (secs > 0.0) effective_time_ms = static_cast<int>(secs * 1000.0);
    }
  }

  std::cerr << "[AlphaBeta] Starting search (depth=" << max_depth << ", time_ms=" << effective_time_ms << ")..." << std::endl;

  if (effective_time_ms > 0) {
    // 時間指定の場合は反復深化
    auto start_time = std::chrono::steady_clock::now();
    auto deadline = start_time + std::chrono::milliseconds(effective_time_ms);
    Move best_move = iterative_deepening_time(s, deadline);
    
    auto end_time = std::chrono::steady_clock::now();
    stats_.time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
    
    std::cerr << "[AlphaBeta] Search complete | Depth: " << stats_.max_depth_reached
              << " | Nodes: " << stats_.nodes_searched
              << " | Time: " << stats_.time_ms << "ms" << std::endl;
    
    if (verbose_) {
      std::cout << "[AlphaBeta] Search complete | Depth: " << stats_.max_depth_reached
                << " | Nodes: " << stats_.nodes_searched
                << " | Time: " << stats_.time_ms << "ms"
                << " | NPS: " << (stats_.nodes_searched * 1000 / std::max<int64_t>(stats_.time_ms, 1)) << "\n";
    }
    return best_move;
  } else {
    // 深さ指定の場合は通常の探索
    auto start_time = std::chrono::steady_clock::now();
    Move best_move = iterative_deepening(s, max_depth);
    
    auto end_time = std::chrono::steady_clock::now();
    stats_.time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
    stats_.max_depth_reached = max_depth;
    
    if (verbose_) {
      std::cout << "[AlphaBeta] Search complete | Depth: " << max_depth
                << " | Nodes: " << stats_.nodes_searched
                << " | Time: " << stats_.time_ms << "ms"
                << " | NPS: " << (stats_.nodes_searched * 1000 / std::max<int64_t>(stats_.time_ms, 1)) << "\n";
    }
    return best_move;
  }
}
