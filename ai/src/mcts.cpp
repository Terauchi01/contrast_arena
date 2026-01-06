#include "mcts.hpp"
#include "contrast/rules.hpp"
#include "contrast/move_list.hpp"
#include <algorithm>
#include <iostream>
#include <chrono>

using namespace contrast_ai;
using namespace contrast;

MCTS::MCTS() 
  : exploration_constant_(1.414f), verbose_(false),
    rng_(std::chrono::steady_clock::now().time_since_epoch().count()) {
}

MCTS::MCTS(const std::string& ntuple_weights_file)
  : exploration_constant_(1.414f), verbose_(false),
    rng_(std::chrono::steady_clock::now().time_since_epoch().count()) {
  load_network(ntuple_weights_file);
}

void MCTS::set_network(const NTupleNetwork& network) {
  network_ = network;
}

void MCTS::load_network(const std::string& weights_file) {
  network_.load(weights_file);
  std::cerr << "[MCTS] Network loaded successfully" << std::endl;
}

std::vector<Move> MCTS::get_legal_moves(const GameState& state) const {
  MoveList moves;
  Rules::legal_moves(state, moves);
  
  std::vector<Move> result;
  result.reserve(moves.size);
  for (size_t i = 0; i < moves.size; ++i) {
    result.push_back(moves[i]);
  }
  
  return result;
}

bool MCTS::is_terminal(const GameState& state) const {
  MoveList moves;
  Rules::legal_moves(state, moves);
  
  if (moves.empty()) return true;
  if (Rules::is_win(state, Player::Black)) return true;
  if (Rules::is_win(state, Player::White)) return true;
  
  return false;
}

float MCTS::evaluate_terminal(const GameState& state) const {
  MoveList moves;
  Rules::legal_moves(state, moves);
  
  if (moves.empty()) {
    // 手番側の負け
    return -1.0f;
  }
  
  if (Rules::is_win(state, Player::Black)) {
    return (state.current_player() == Player::Black) ? 1.0f : -1.0f;
  }
  
  if (Rules::is_win(state, Player::White)) {
    return (state.current_player() == Player::White) ? 1.0f : -1.0f;
  }
  
  return 0.0f;
}

float MCTS::evaluate_state(const GameState& state) const {
  return network_.evaluate(state);
}

MCTSNode* MCTS::select(MCTSNode* node) {
  while (!node->is_terminal && node->is_expanded) {
    if (node->children.empty()) {
      return node;
    }
    
    // UCB1で最良の子ノードを選択
    MCTSNode* best_child = nullptr;
    float best_ucb = -std::numeric_limits<float>::infinity();
    
    for (auto& child : node->children) {
      float ucb = child->ucb1(exploration_constant_);
      if (ucb > best_ucb) {
        best_ucb = ucb;
        best_child = child.get();
      }
    }
    
    node = best_child;
  }
  
  return node;
}

void MCTS::expand(MCTSNode* node) {
  if (node->is_terminal || node->is_expanded) {
    return;
  }
  
  auto moves = get_legal_moves(node->state);
  
  if (moves.empty()) {
    node->is_terminal = true;
    node->is_expanded = true;
    return;
  }
  
  for (const auto& move : moves) {
    GameState next_state = node->state;
    next_state.apply_move(move);
    
    auto child = std::make_unique<MCTSNode>(next_state, move, node);
    child->is_terminal = is_terminal(next_state);
    
    node->children.push_back(std::move(child));
  }
  
  node->is_expanded = true;
}

float MCTS::simulate(MCTSNode* node) {
  // 終端ノードの場合
  if (node->is_terminal) {
    return evaluate_terminal(node->state);
  }
  
  // NTuple評価関数を使ってシミュレーション
  float eval = evaluate_state(node->state);
  
  // 評価値を[-1, 1]の範囲に正規化（より緩やかに）
  // NTupleの評価値は通常[-3, 3]程度なので、それを考慮
  return std::tanh(eval / 3.0f);
}

void MCTS::backpropagate(MCTSNode* node, float value) {
  while (node != nullptr) {
    node->visits++;
    node->total_value += value;
    
    // 次のノードでは相手の視点になるので値を反転
    value = -value;
    node = node->parent;
  }
}

Move MCTS::search(const GameState& s, int iterations) {
  if (verbose_) {
    std::cerr << "[MCTS] Starting search with " << iterations << " iterations..." << std::endl;
  }
  
  // ルートノード作成
  auto root = std::make_unique<MCTSNode>(s, Move{0, 0}, nullptr);
  
  // MCTS反復
  for (int i = 0; i < iterations; ++i) {
    // 1. Selection
    MCTSNode* node = select(root.get());
    
    // 2. Expansion
    if (node->visits > 0 && !node->is_terminal) {
      expand(node);
      if (!node->children.empty()) {
        node = node->children[0].get();
      }
    }
    
    // 3. Simulation
    float value = simulate(node);
    
    // 4. Backpropagation
    backpropagate(node, value);
    
    if (verbose_ && (i + 1) % 100 == 0) {
      std::cerr << "[MCTS] Iteration " << (i + 1) << "/" << iterations << std::endl;
    }
  }
  
  // 最も訪問回数の多い手を選択
  if (root->children.empty()) {
    expand(root.get());
    if (root->children.empty()) {
      // 合法手がない場合
      return Move{0, 0};
    }
  }
  
  MCTSNode* best_child = nullptr;
  int best_visits = -1;
  
  for (auto& child : root->children) {
    if (child->visits > best_visits) {
      best_visits = child->visits;
      best_child = child.get();
    }
  }
  
  if (verbose_) {
    std::cerr << "[MCTS] Best move: visits=" << best_visits 
              << ", avg_value=" << (best_child ? best_child->average_value() : 0.0f) 
              << std::endl;
  }
  
  return best_child ? best_child->move : Move{0, 0};
}
