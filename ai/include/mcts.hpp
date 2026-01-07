#pragma once
#include "contrast/game_state.hpp"
#include "contrast/move.hpp"
#include "ntuple_big.hpp"
#include <memory>
#include <vector>
#include <unordered_map>
#include <cmath>
#include <random>

namespace contrast_ai {

/**
 * MCTSノード
 */
struct MCTSNode {
  contrast::GameState state;
  contrast::Move move;  // このノードに至った手
  MCTSNode* parent;
  std::vector<std::unique_ptr<MCTSNode>> children;
  
  int visits;
  float total_value;
  bool is_terminal;
  bool is_expanded;
  
  MCTSNode(const contrast::GameState& s, const contrast::Move& m, MCTSNode* p)
    : state(s), move(m), parent(p), visits(0), total_value(0.0f), 
      is_terminal(false), is_expanded(false) {}
  
  float ucb1(float exploration_constant = 1.414f) const {
    if (visits == 0) return std::numeric_limits<float>::infinity();
    // total_value is stored from this node's viewpoint; when selecting
    // among children we need exploitation from the parent's viewpoint,
    // therefore negate the child's average here.
    float exploitation = - (total_value / visits);
    float exploration = exploration_constant * std::sqrt(std::log(parent->visits) / visits);
    return exploitation + exploration;
  }
  
  float average_value() const {
    return visits > 0 ? total_value / visits : 0.0f;
  }
};

/**
 * MCTS with NTuple evaluation
 */
class MCTS {
public:
  MCTS();
  explicit MCTS(const std::string& ntuple_weights_file);
  
  // 探索を実行して最良手を返す
  // If time_ms > 0, search will stop after time_ms milliseconds (overrides/limits iterations).
  // If time_ms == 0 and environment variable CONTRAST_MOVE_TIME is set (seconds), it will be used.
  contrast::Move search(const contrast::GameState& s, int iterations=1000, int time_ms=0);
  
  // N-tupleネットワークを設定
  void set_network(const NTupleNetwork& network);
  void load_network(const std::string& weights_file);
  
  // パラメータ設定
  void set_exploration_constant(float c) { exploration_constant_ = c; }
  void set_verbose(bool v) { verbose_ = v; }
  
private:
  // MCTS操作
  MCTSNode* select(MCTSNode* node);
  void expand(MCTSNode* node);
  float simulate(MCTSNode* node);
  void backpropagate(MCTSNode* node, float value);
  
  // ヘルパー関数
  std::vector<contrast::Move> get_legal_moves(const contrast::GameState& state) const;
  bool is_terminal(const contrast::GameState& state) const;
  float evaluate_terminal(const contrast::GameState& state) const;
  float evaluate_state(const contrast::GameState& state) const;
  
  NTupleNetwork network_;
  float exploration_constant_;
  bool verbose_;
  std::mt19937 rng_;
};

} // namespace contrast_ai
