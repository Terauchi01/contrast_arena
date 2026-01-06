#include "contrast_ai/ntuple.hpp"
#include "contrast/rules.hpp"
#include "contrast/board.hpp"
#include "contrast/move_list.hpp"
#include "contrast/symmetry.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <chrono>

namespace contrast_ai {

// ============================================================================
// NTuple implementation
// ============================================================================

#ifdef SEPARATE_ENCODING
/**
 * 駒の状態のみを0-2の整数にエンコード（完全分離版）
 * 
 * タイルの色は別テーブルで管理することでメモリを大幅削減
 * 
 * エンコード方式（現在のプレイヤー視点）：
 *   0 = Empty（空）
 *   1 = My（自分の駒）
 *   2 = Opp（相手の駒）
 * 
 * メモリ削減効果（10セル）：
 *   9^10 = 3,486,784,401 → 3^10 = 59,049（約59,000分の1）
 * 
 * @param c エンコードするセル
 * @param current_player 現在のプレイヤー（視点の基準）
 * @return 0-2の整数（3通りの状態）
 */
int NTuple::encode_cell_piece(const contrast::Cell& c, contrast::Player current_player) {
  using namespace contrast;
  
  if (c.occupant == Player::None) {
    return 0; // Empty
  } else if (c.occupant == current_player) {
    return 1; // My piece
  } else {
    return 2; // Opponent's piece
  }
}

/**
 * タイルの色のみを0-2の整数にエンコード（完全分離版）
 * 
 * 駒とタイルを分離することで、パターンテーブルのサイズを削減
 * 
 * エンコード方式：
 *   0 = None（タイルなし）
 *   1 = Black（黒タイル）
 *   2 = Gray（灰タイル）
 * 
 * @param c エンコードするセル
 * @return 0-2の整数（3通りの状態）
 */
int NTuple::encode_cell_tile(const contrast::Cell& c) {
  return static_cast<int>(c.tile);
}

#else
/**
 * 駒とタイルを結合して0-8の整数にエンコード（部分結合版）
 * 
 * 駒とタイルの相互作用を直接学習できるが、メモリ使用量は多い
 * 
 * エンコード方式（現在のプレイヤー視点）：
 *   value = piece * 3 + tile
 *   piece: 0=Empty, 1=My, 2=Opp
 *   tile: 0=None, 1=Black, 2=Gray
 * 
 * @param c エンコードするセル
 * @param current_player 現在のプレイヤー（視点の基準）
 * @return 0-8の整数（9通りの状態）
 */
int NTuple::encode_cell(const contrast::Cell& c, contrast::Player current_player) {
  using namespace contrast;
  
  int piece;
  if (c.occupant == Player::None) {
    piece = 0;
  } else if (c.occupant == current_player) {
    piece = 1;
  } else {
    piece = 2;
  }
  
  int tile = static_cast<int>(c.tile);
  return piece * 3 + tile;
}
#endif

/**
 * パターンの盤面状態を一意のインデックスに変換
 * 
 * インデックス計算式:
 *   idx = Σ(cell_value × base^position)
 *   base = 3 (完全分離版) or 9 (部分結合版)
 * 
 * メモリ使用量の見積もり：
 *   完全分離版（3値）:
 *     - 10セル: 3^10 = 59,049 → 約0.23MB
 *     - 9セル: 3^9 = 19,683 → 約77KB
 *   部分結合版（9値）:
 *     - 10セル: 9^10 = 3,486,784,401 → 約13.0GB
 *     - 9セル: 9^9 = 387,420,489 → 約1.4GB
 * 
 * @param board 評価する盤面
 * @param offset_x パターンのX方向オフセット
 * @param offset_y パターンのY方向オフセット
 * @param current_player 現在のプレイヤー（視点の基準）
 * @return パターン状態を表す一意のインデックス（0 ～ num_states()-1）
 */
long long NTuple::to_index(const contrast::Board& board, int offset_x, int offset_y,
                           contrast::Player current_player) const {
  long long idx = 0;
#ifdef SEPARATE_ENCODING
  constexpr int base = 3; // 3 possible states per cell (Empty/My/Opp or None/Black/Gray)
#else
  constexpr int base = 9; // 9 possible states per cell (piece × tile)
#endif
  
  // Encode board pattern
  for (size_t i = 0; i < num_cells; ++i) {
    int cell_idx = cell_indices[i];
    int dx = cell_idx % 5;
    int dy = cell_idx / 5;
    int x = offset_x + dx;
    int y = offset_y + dy;
    
    // Out of bounds = treat as empty
    if (x < 0 || x >= board.width() || y < 0 || y >= board.height()) {
      idx = idx * base + 0;
    } else {
#ifdef SEPARATE_ENCODING
      idx = idx * base + encode_cell_piece(board.at(x, y), current_player);
#else
      idx = idx * base + encode_cell(board.at(x, y), current_player);
#endif
    }
  }
  
  return idx;
}

/**
 * このパターンが取りうる状態の総数を計算
 * 
 * 重みテーブルのサイズを決定するために使用
 * 
 * メモリ使用量の見積もり：
 *   完全分離版（3値）:
 *     - 10セル: 3^10 = 59,049 → 約0.23MB
 *     - 9セル: 3^9 = 19,683 → 約77KB
 *   部分結合版（9値）:
 *     - 10セル: 9^10 = 3,486,784,401 → 約13.0GB
 *     - 9セル: 9^9 = 387,420,489 → 約1.4GB
 * 
 * @return 状態の総数
 */
long long NTuple::num_states() const {
  long long result = 1;
#ifdef SEPARATE_ENCODING
  constexpr long long base = 3LL;
#else
  constexpr long long base = 9LL;
#endif
  for (size_t i = 0; i < num_cells; ++i) {
    result *= base;
  }
  return result;
}

// ============================================================================
// NTupleNetwork implementation
// ============================================================================

/**
 * 手持ちタイルからインデックスを計算
 * 
 * 手持ちの状態をエンコード（合計8状態）
 * 
 * エンコード方式：
 *   black_bits = min(black_remain, 3)  // 0-3+の4状態
 *   gray_bits = min(gray_remain, 1)    // 0-1の2状態
 *   index = black_bits * 2 + gray_bits  // 0-7の8状態
 * 
 * @param black_remain 黒タイルの手持ち枚数
 * @param gray_remain 灰タイルの手持ち枚数
 * @return 0-7のインデックス
 */
int NTupleNetwork::hand_index(int black_remain, int gray_remain) {
  int b = std::min(black_remain, 3);
  int g = std::min(gray_remain, 1);
  return b * 2 + g;
}

/**
 * NTupleNetworkのコンストラクタ
 * 
 * ネットワークの初期化手順：
 *   1. パターンの定義（init_tuples）
 *   2. 各パターンの重みテーブルを0で初期化
 *   3. 手持ちテーブルを初期化
 * 
 * 重みテーブル構造：
 *   weights_[i][j] = i番目のパターンのj番目の状態に対する評価値
 *   hand_weights_[k] = 手持ち状態kの評価値
 */
NTupleNetwork::NTupleNetwork() {
  init_tuples();
  
  // Initialize pattern weights to a small positive value
  weights_.resize(tuples_.size());
  const float initial_weight = 0.5f / (tuples_.size() + 1);  // +1 for hand table
  for (size_t i = 0; i < tuples_.size(); ++i) {
    weights_[i].resize(tuples_[i].num_states(), initial_weight);
  }
  
  // Initialize hand weights (8 states: 4x2 for black_remain x gray_remain)
  hand_weights_.resize(8, initial_weight);
  
#ifdef SEPARATE_ENCODING
  // Initialize tile pattern weights (for separate encoding)
  tile_weights_.resize(tile_tuples_.size());
  for (size_t i = 0; i < tile_tuples_.size(); ++i) {
    tile_weights_[i].resize(tile_tuples_[i].num_states(), initial_weight);
  }
#endif
}

// Copy constructor for fast in-memory copying
NTupleNetwork::NTupleNetwork(const NTupleNetwork& other) 
  : tuples_(other.tuples_), weights_(other.weights_), hand_weights_(other.hand_weights_)
#ifdef SEPARATE_ENCODING
  , tile_tuples_(other.tile_tuples_), tile_weights_(other.tile_weights_)
#endif
{
}

/**
 * 使用するパターンを定義
 * 
 * 8つの基本パターン形状を定義し、それぞれを盤面上の異なる位置に平行移動させて配置
 * これにより位置不変性を高め、盤面全体を効率的にカバーする
 * 
 * 基本パターン：
 *   1. 3x3正方形 - コンパクトな局所領域
 *   2. L字型 - コーナー周辺の特徴
 *   3. T字型 - 縦方向の展開
 *   4-5. 斜め - 斜め方向の連結
 *   6-7. 十字型 - 中央エリアの制御
 *   8. 縦長 - 縦方向の支配
 * 
 * 平行移動戦略：
 *   - 各パターンを盤面上でスライドさせて配置
 *   - はみ出さない範囲で最大限カバー
 *   - 重複を許容（異なる視点から同じ領域を評価）
 * 
 * メモリ使用量：
 *   - 各パターン: 9^9 = 387,420,489 状態 → 約1.44GB
 *   - 合計パターン数に応じて線形増加
 */
void NTupleNetwork::init_tuples() {
  // 盤面パターン（駒の配置 or 駒×タイル）
  std::vector<std::vector<int>> base_patterns = {
    /*
     0, 1, 2, 3, 4,
     5, 6, 7, 8, 9,
    10,11,12,13,14,
    15,16,17,18,19,
    20,21,22,23,24,
    */

    // 横長パターン（5x2形状）
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9},         // 横長 行0-1
    {5, 6, 7, 8, 9, 10, 11, 12, 13, 14},     // 横長 行1-2
    {10, 11, 12, 13, 14, 15, 16, 17, 18, 19}, // 横長 行2-3
    {15, 16, 17, 18, 19, 20, 21, 22, 23, 24}, // 横長 行3-4

    // 縦長パターン（5x2形状）
    {0, 5, 10, 15, 20, 1, 6, 11, 16, 21},    // 縦長 列0-1
    {1, 6, 11, 16, 21, 2, 7, 12, 17, 22},    // 縦長 列1-2
    {2, 7, 12, 17, 22, 3, 8, 13, 18, 23},    // 縦長 列2-3
    
    // 3x3正方形パターン
    { 0,  1,  2,  5,  6,  7, 10, 11, 12},    // 3x3 左上
    { 1,  2,  3,  6,  7,  8, 11, 12, 13},    // 3x3 中上
    { 5,  6,  7, 10, 11, 12, 15, 16, 17},    // 3x3 左中
    { 6,  7,  8, 11, 12, 13, 16, 17, 18},    // 3x3 中央
    {10, 11, 12, 15, 16, 17, 20, 21, 22},    // 3x3 左下
    {11, 12, 13, 16, 17, 18, 21, 22, 23},    // 3x3 中下
    
    // T字型・斜めパターン
    {0, 1, 2, 3, 4, 5, 10, 15, 20},          // T字型
    {0, 1, 2, 3, 4, 6, 11, 16, 21},          // T字型
    {0, 1, 2, 3, 4, 7, 12, 17, 22},          // 斜め
  };
  
  // 基本パターンを追加（駒配置用）
  long long total_piece_states = 0;
  for (const auto& base : base_patterns) {
    NTuple pattern;
    pattern.num_cells = 0;
    for (int cell : base) {
      pattern.cell_indices[pattern.num_cells++] = cell;
    }
    tuples_.push_back(pattern);
    if (total_piece_states == 0) {
      total_piece_states = pattern.num_states();
    }
  }
  
#ifdef SEPARATE_ENCODING
  // 完全分離版：タイル配置用の同じパターンを追加
  for (const auto& base : base_patterns) {
    NTuple pattern;
    pattern.num_cells = 0;
    for (int cell : base) {
      pattern.cell_indices[pattern.num_cells++] = cell;
    }
    tile_tuples_.push_back(pattern);
  }
  
  long long total_tile_states = tile_tuples_[0].num_states();
  double piece_memory = tuples_.size() * total_piece_states * sizeof(float) / (1024.0*1024.0);
  double tile_memory = tile_tuples_.size() * total_tile_states * sizeof(float) / (1024.0*1024.0);
  double hand_memory = 8 * sizeof(float) / 1024.0;  // 8 states in KB
  
  std::cout << "========================================\n";
  std::cout << "N-tuple Network Configuration\n";
  std::cout << "========================================\n";
  std::cout << "Encoding: SEPARATE (Piece + Tile + Hand)\n";
  std::cout << "----------------------------------------\n";
  std::cout << "Piece patterns: " << tuples_.size() << "\n";
  std::cout << "  Alphabet: 3 (Empty/My/Opp)\n";
  std::cout << "  States/pattern: " << total_piece_states << "\n";
  std::cout << "  Memory: " << piece_memory << " MB\n";
  std::cout << "Tile patterns: " << tile_tuples_.size() << "\n";
  std::cout << "  Alphabet: 3 (None/Black/Gray)\n";
  std::cout << "  States/pattern: " << total_tile_states << "\n";
  std::cout << "  Memory: " << tile_memory << " MB\n";
  std::cout << "Hand table: 8 states (" << hand_memory << " KB)\n";
  std::cout << "----------------------------------------\n";
  std::cout << "Total memory: " << (piece_memory + tile_memory) << " MB\n";
  std::cout << "========================================\n";
  
#else
  // 部分結合版：駒×タイルを結合
  double piece_memory = tuples_.size() * total_piece_states * sizeof(float) / (1024.0*1024.0*1024.0);
  double hand_memory = 8 * sizeof(float) / 1024.0;  // 8 states in KB
  
  std::cout << "========================================\n";
  std::cout << "N-tuple Network Configuration\n";
  std::cout << "========================================\n";
  std::cout << "Encoding: COMBINED (Piece×Tile + Hand)\n";
  std::cout << "----------------------------------------\n";
  std::cout << "Piece×Tile patterns: " << tuples_.size() << "\n";
  std::cout << "  Alphabet: 9 (3 pieces × 3 tiles)\n";
  std::cout << "  States/pattern: " << total_piece_states << "\n";
  std::cout << "  Memory: " << piece_memory << " GB\n";
  std::cout << "Hand table: 8 states (" << hand_memory << " KB)\n";
  std::cout << "----------------------------------------\n";
  std::cout << "Total memory: " << piece_memory << " GB\n";
  std::cout << "========================================\n";
#endif
}

/**
 * 盤面状態から特徴インデックスを抽出（内部用）
 * 
 * 各パターンについて、現在の盤面状態をインデックスに変換
 * これらのインデックスが重みテーブルへのアクセスキーとなる
 * 
 * @param board 評価する盤面
 * @param current_player 現在のプレイヤー
 * @return 各パターンのインデックスのリスト
 */
std::vector<int> NTupleNetwork::extract_features(const contrast::Board& board, 
                                                  contrast::Player current_player) const {
  std::vector<int> features;
  features.reserve(tuples_.size());
  
  // Each tuple evaluated at its base position
  for (const auto& tuple : tuples_) {
    int idx = tuple.to_index(board, 0, 0, current_player);
    features.push_back(idx);
  }
  
  return features;
}

/**
 * 盤面を評価して価値を返す（メイン評価関数）
 * 
 * N-tupleネットワークの推論プロセス：
 * 
 * 1. 対称性の正規化
 *    - 同じ局面でも向きが違うと別物として学習してしまう問題を解決
 *    - 8種類の対称性（回転・反転）から代表形を選択
 * 
 * 2. パターンマッチング
 *    - 駒配置パターンの評価
 *    - (完全分離版) タイル配置パターンの評価
 *    - 手持ちタイルの評価
 * 
 * 3. 価値の合算
 *    - 全パターンの重みを足し合わせる
 * 
 * 4. 手番の考慮
 *    - 重みは常に黒番視点で学習
 *    - 白番の場合は符号を反転
 * 
 * @param state 評価する局面
 * @return 評価値（正=現在のプレイヤーに有利、負=不利）
 */
float NTupleNetwork::evaluate(const contrast::GameState& state) const {
  // 正規化された（対称性を考慮した）ボードを使用
  const auto& b = state.board();
  auto canonical_sym = contrast::SymmetryOps::get_canonical_symmetry(b);
  auto canonical_board = contrast::SymmetryOps::transform_board(b, canonical_sym);
  
  contrast::Player current_player = state.current_player();
  float value = 0.0f;
  
  // 駒配置パターンの評価
  for (size_t i = 0; i < tuples_.size(); ++i) {
    const auto& tuple = tuples_[i];
    const auto& weights = weights_[i];
    
    int idx = tuple.to_index(canonical_board, 0, 0, current_player);
    value += weights[idx];
  }
  
#ifdef SEPARATE_ENCODING
  // タイル配置パターンの評価（完全分離版のみ）
  for (size_t i = 0; i < tile_tuples_.size(); ++i) {
    const auto& tuple = tile_tuples_[i];
    const auto& weights = tile_weights_[i];
    
    // タイルはプレイヤー依存しないので、任意のプレイヤーを渡す
    long long idx = 0;
    constexpr int base = 3;
    for (size_t j = 0; j < tuple.num_cells; ++j) {
      int cell_idx = tuple.cell_indices[j];
      int dx = cell_idx % 5;
      int dy = cell_idx / 5;
      int x = dx;
      int y = dy;
      
      if (x < 0 || x >= canonical_board.width() || y < 0 || y >= canonical_board.height()) {
        idx = idx * base + 0;
      } else {
        idx = idx * base + NTuple::encode_cell_tile(canonical_board.at(x, y));
      }
    }
    value += weights[idx];
  }
#endif
  
  // 手持ちタイルの評価
  const auto& inv = state.inventory(current_player);
  int h_idx = hand_index(inv.black, inv.gray);
  value += hand_weights_[h_idx];
  
  // Flip sign if evaluating from White's perspective
  if (current_player == contrast::Player::White) {
    value = -value;
  }
  
  return value;
}

/**
 * TD学習で重みを更新（学習のコア）
 * 
 * Temporal Difference (TD) Learning の実装
 * 強化学習の一種で、時系列データから価値を学習する手法
 * 
 * 学習プロセス：
 * 
 * 1. 現在の評価値を計算
 *    - 正規化された盤面から、現在のネットワークが予測する価値を取得
 * 
 * 2. 誤差の計算
 *    - error = target - current_value
 *    - target: 教師信号（報酬 + 次の状態の価値、または最終的な勝敗）
 *    - current_value: 現在のネットワークの予測
 * 
 * 3. 手番の考慮
 *    - 評価は手番に応じて符号反転される
 *    - 更新時も一貫性を保つため、誤差を黒番視点に戻す
 * 
 * 4. 重みの更新
 *    - weights[idx] += learning_rate * error
 *    - 誤差が大きいほど大きく更新
 *    - 学習率で更新幅を制御
 * 
 * @param state 更新対象の局面
 * @param target 教師信号（目標となる評価値）
 * @param learning_rate 学習率（更新の大きさを制御、通常0.001-0.01）
 */
void NTupleNetwork::td_update(const contrast::GameState& state, float target, float learning_rate) {
  // 正規化された（対称性を考慮した）ボードを使用
  const auto& b = state.board();
  auto canonical_sym = contrast::SymmetryOps::get_canonical_symmetry(b);
  auto canonical_board = contrast::SymmetryOps::transform_board(b, canonical_sym);
  
  contrast::Player current_player = state.current_player();
  
  // Get current value in raw form (before perspective flip)
  float raw_value = 0.0f;
  
  // 駒配置パターン
  for (size_t i = 0; i < tuples_.size(); ++i) {
    const auto& tuple = tuples_[i];
    const auto& weights = weights_[i];
    
    long long idx = tuple.to_index(canonical_board, 0, 0, current_player);
    raw_value += weights[idx];
  }
  
#ifdef SEPARATE_ENCODING
  // タイル配置パターン（完全分離版のみ）
  for (size_t i = 0; i < tile_tuples_.size(); ++i) {
    const auto& tuple = tile_tuples_[i];
    const auto& weights = tile_weights_[i];
    
    long long idx = 0;
    constexpr int base = 3;
    for (size_t j = 0; j < tuple.num_cells; ++j) {
      int cell_idx = tuple.cell_indices[j];
      int dx = cell_idx % 5;
      int dy = cell_idx / 5;
      int x = dx;
      int y = dy;
      
      if (x < 0 || x >= canonical_board.width() || y < 0 || y >= canonical_board.height()) {
        idx = idx * base + 0;
      } else {
        idx = idx * base + NTuple::encode_cell_tile(canonical_board.at(x, y));
      }
    }
    raw_value += weights[idx];
  }
#endif
  
  // 手持ちタイル
  const auto& inv = state.inventory(current_player);
  int h_idx = hand_index(inv.black, inv.gray);
  raw_value += hand_weights_[h_idx];
  
  // Apply perspective for current player
  float current_value = raw_value;
  if (current_player == contrast::Player::White) {
    current_value = -current_value;
  }
  
  float error = target - current_value;
  
  // Convert error back to raw (Black's perspective) for weight update
  if (current_player == contrast::Player::White) {
    error = -error;
  }
  
  // Normalize learning rate by number of components
  int num_components = tuples_.size() + 1;  // +1 for hand table
#ifdef SEPARATE_ENCODING
  num_components += tile_tuples_.size();
#endif
  float normalized_lr = learning_rate / num_components;
  
  // Update piece patterns
  for (size_t i = 0; i < tuples_.size(); ++i) {
    const auto& tuple = tuples_[i];
    auto& weights = weights_[i];
    
    long long idx = tuple.to_index(canonical_board, 0, 0, current_player);
    weights[idx] += normalized_lr * error;
  }
  
#ifdef SEPARATE_ENCODING
  // Update tile patterns
  for (size_t i = 0; i < tile_tuples_.size(); ++i) {
    const auto& tuple = tile_tuples_[i];
    auto& weights = tile_weights_[i];
    
    long long idx = 0;
    constexpr int base = 3;
    for (size_t j = 0; j < tuple.num_cells; ++j) {
      int cell_idx = tuple.cell_indices[j];
      int dx = cell_idx % 5;
      int dy = cell_idx / 5;
      int x = dx;
      int y = dy;
      
      if (x < 0 || x >= canonical_board.width() || y < 0 || y >= canonical_board.height()) {
        idx = idx * base + 0;
      } else {
        idx = idx * base + NTuple::encode_cell_tile(canonical_board.at(x, y));
      }
    }
    weights[idx] += normalized_lr * error;
  }
#endif
  
  // Update hand table
  hand_weights_[h_idx] += normalized_lr * error;
}

/**
 * 学習済み重みをファイルに保存
 * 
 * バイナリ形式で保存（テキストより高速・省容量）
 * 
 * ファイル構造：
 *   1. パターン数（size_t）
 *   2. 各パターンについて：
 *      - 重みテーブルのサイズ（size_t）
 *      - 重みデータ（float配列）
 *   3. 手持ちテーブル（float配列）
 *   4. (完全分離版) タイルパターン
 * 
 * @param filename 保存先ファイル名
 */
void NTupleNetwork::save(const std::string& filename) const {
  std::ofstream ofs(filename, std::ios::binary);
  if (!ofs) return;
  
  // Save number of piece tuples
  size_t num_tuples = tuples_.size();
  ofs.write(reinterpret_cast<const char*>(&num_tuples), sizeof(num_tuples));
  
  // Save each piece tuple's weights
  for (const auto& w : weights_) {
    size_t size = w.size();
    ofs.write(reinterpret_cast<const char*>(&size), sizeof(size));
    ofs.write(reinterpret_cast<const char*>(w.data()), size * sizeof(float));
  }
  
  // Save hand weights
  size_t hand_size = hand_weights_.size();
  ofs.write(reinterpret_cast<const char*>(&hand_size), sizeof(hand_size));
  ofs.write(reinterpret_cast<const char*>(hand_weights_.data()), hand_size * sizeof(float));
  
#ifdef SEPARATE_ENCODING
  // Save number of tile tuples
  size_t num_tile_tuples = tile_tuples_.size();
  ofs.write(reinterpret_cast<const char*>(&num_tile_tuples), sizeof(num_tile_tuples));
  
  // Save each tile tuple's weights
  for (const auto& w : tile_weights_) {
    size_t size = w.size();
    ofs.write(reinterpret_cast<const char*>(&size), sizeof(size));
    ofs.write(reinterpret_cast<const char*>(w.data()), size * sizeof(float));
  }
#endif
}

/**
 * 保存された重みをファイルから読み込み
 * 
 * 学習済みモデルを再利用するために必須
 * 継続学習や評価に使用
 * 
 * 安全性チェック：
 *   - パターン数が一致するか確認
 *   - 不一致の場合は読み込まない（構造が異なる）
 * 
 * @param filename 読み込むファイル名
 */
void NTupleNetwork::load(const std::string& filename) {
  std::ifstream ifs(filename, std::ios::binary);
  if (!ifs) return;
  
  size_t num_tuples;
  ifs.read(reinterpret_cast<char*>(&num_tuples), sizeof(num_tuples));
  
  if (num_tuples != tuples_.size()) return; // Mismatch
  
  // Load piece patterns
  for (auto& w : weights_) {
    size_t size;
    ifs.read(reinterpret_cast<char*>(&size), sizeof(size));
    w.resize(size);
    ifs.read(reinterpret_cast<char*>(w.data()), size * sizeof(float));
  }
  
  // Load hand weights
  size_t hand_size;
  ifs.read(reinterpret_cast<char*>(&hand_size), sizeof(hand_size));
  if (hand_size != hand_weights_.size()) return; // Mismatch
  ifs.read(reinterpret_cast<char*>(hand_weights_.data()), hand_size * sizeof(float));
  
#ifdef SEPARATE_ENCODING
  // Load tile patterns
  size_t num_tile_tuples;
  ifs.read(reinterpret_cast<char*>(&num_tile_tuples), sizeof(num_tile_tuples));
  
  if (num_tile_tuples != tile_tuples_.size()) return; // Mismatch
  
  for (auto& w : tile_weights_) {
    size_t size;
    ifs.read(reinterpret_cast<char*>(&size), sizeof(size));
    w.resize(size);
    ifs.read(reinterpret_cast<char*>(w.data()), size * sizeof(float));
  }
#endif
}

/**
 * 重みの総数を取得
 * 
 * メモリ使用量の確認やデバッグに使用
 * 
 * @return 全パターンの重みの合計数
 */
size_t NTupleNetwork::num_weights() const {
  size_t total = 0;
  
  // Piece pattern weights
  for (const auto& w : weights_) {
    total += w.size();
  }
  
  // Hand weights
  total += hand_weights_.size();
  
#ifdef SEPARATE_ENCODING
  // Tile pattern weights
  for (const auto& w : tile_weights_) {
    total += w.size();
  }
#endif
  
  return total;
}

// ============================================================================
// NTuplePolicy implementation
// ============================================================================

NTuplePolicy::NTuplePolicy() 
  : rng_(std::chrono::steady_clock::now().time_since_epoch().count()) {
}

NTuplePolicy::NTuplePolicy(const std::string& weights_file)
  : rng_(std::chrono::steady_clock::now().time_since_epoch().count()) {
  network_.load(weights_file);
}

contrast::Move NTuplePolicy::pick(const contrast::GameState& s) {
  contrast::MoveList moves;
  contrast::Rules::legal_moves(s, moves);
  if (moves.empty()) {
    return contrast::Move(); // No legal move
  }
  
  // Evaluate all moves
  float best_value = -1e9f;
  contrast::MoveList best_moves;
  
  for (size_t i = 0; i < moves.size; ++i) {
    const auto& m = moves[i];
    // Apply move to get next state
    contrast::GameState next = s;
    next.apply_move(m);
    
    // Evaluate from current player's perspective
    // (network already handles perspective flip)
    float value = -network_.evaluate(next); // Negamax: opponent's value = -our value
    
    if (value > best_value) {
      best_value = value;
      best_moves.clear();
      best_moves.push_back(m);
    } else if (std::abs(value - best_value) < 1e-6f) {
      best_moves.push_back(m);
    }
  }
  
  // Pick randomly among best moves
  if (best_moves.empty()) return moves[0];
  
  std::uniform_int_distribution<size_t> dist(0, best_moves.size - 1);
  return best_moves[dist(rng_)];
}

bool NTuplePolicy::save(const std::string& filename) const {
  network_.save(filename);
  return true;
}

bool NTuplePolicy::load(const std::string& filename) {
  try {
    network_.load(filename);
    return true;
  } catch (const std::exception& e) {
    std::cerr << "Failed to load N-tuple weights: " << e.what() << "\n";
    return false;
  }
}

} // namespace contrast_ai
