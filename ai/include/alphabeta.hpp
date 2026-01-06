#pragma once
#include "contrast/game_state.hpp"
#include "contrast/move.hpp"
#include "ntuple_big.hpp"
#include <memory>
#include <vector>
#include <unordered_map>
#include <chrono>
#include <limits>

namespace contrast_ai {

/**
 * 置換表エントリ
 * 探索済みの局面を記憶して再利用
 */
struct TranspositionEntry {
  uint64_t hash;
  float value;
  int depth;
  enum class Flag { EXACT, LOWER_BOUND, UPPER_BOUND } flag;
  contrast::Move best_move;
};

/**
 * アルファベータ探索
 * N-tupleネットワークを評価関数として使用
 */
class AlphaBeta {
public:
  AlphaBeta();
  explicit AlphaBeta(const std::string& ntuple_weights_file);
  
  // 探索を実行して最良手を返す（MCTSと同じインターフェース）
  // max_depthが負の場合はtime_msを使用
  contrast::Move search(const contrast::GameState& s, int max_depth = 5, int time_ms = -1);
  
  // N-tupleネットワークを設定
  void set_network(const NTupleNetwork& network);
  void load_network(const std::string& weights_file);
  
  // パラメータ設定
  void set_use_transposition_table(bool use) { use_tt_ = use; }
  void set_use_move_ordering(bool use) { use_move_ordering_ = use; }
  void set_verbose(bool v) { verbose_ = v; }
  
  // 統計情報
  struct Stats {
    int nodes_searched;
    int tt_hits;
    int tt_cutoffs;
    int beta_cutoffs;
    int64_t time_ms;
    int max_depth_reached;
    
    void reset() {
      nodes_searched = 0;
      tt_hits = 0;
      tt_cutoffs = 0;
      beta_cutoffs = 0;
      time_ms = 0;
      max_depth_reached = 0;
    }
  };
  
  const Stats& get_stats() const { return stats_; }
  
private:
  // アルファベータ探索の本体
  float alphabeta(const contrast::GameState& state, int depth, float alpha, float beta, 
                  bool maximizing, contrast::Move& best_move);
  
  // 反復深化（時間または深さ指定）
  contrast::Move iterative_deepening(const contrast::GameState& state, int max_depth);
  contrast::Move iterative_deepening_time(const contrast::GameState& state, 
                                           std::chrono::steady_clock::time_point deadline);
  
  // ヘルパー関数
  std::vector<contrast::Move> get_legal_moves(const contrast::GameState& state) const;
  bool is_terminal(const contrast::GameState& state, float& terminal_value) const;
  float evaluate_state(const contrast::GameState& state) const;
  uint64_t compute_hash(const contrast::GameState& state) const;
  
  // 移動順序付け（Move Ordering）
  void order_moves(std::vector<contrast::Move>& moves, const contrast::GameState& state);
  
  // 置換表（Transposition Table）
  void store_tt(uint64_t hash, float value, int depth, 
                TranspositionEntry::Flag flag, const contrast::Move& best_move);
  bool probe_tt(uint64_t hash, int depth, float alpha, float beta, 
                float& value, contrast::Move& best_move);
  
  NTupleNetwork network_;
  std::unordered_map<uint64_t, TranspositionEntry> tt_;
  bool use_tt_;
  bool use_move_ordering_;
  bool verbose_;
  Stats stats_;
};

} // namespace contrast_ai
