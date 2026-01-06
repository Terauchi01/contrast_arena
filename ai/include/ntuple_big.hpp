#pragma once
#include "contrast/game_state.hpp"
#include "contrast/move.hpp"
#include <array>
#include <vector>
#include <random>
#include <string>

// ============================================================================
// N-tupleエンコード方式の選択
// ============================================================================
// 
// 2種類のエンコード方式から選択してください：
//
// 【方式1: 完全分離版 (SEPARATE_ENCODING)】
//   - 駒(3値) + タイル(3値) + 手持ち を完全分離
//   - メリット: メモリ使用量が劇的に少ない（~4MB）
//   - デメリット: 駒とタイルの相互作用を直接学習できない
//   - 推奨: 初期学習やメモリ制約がある場合
//
// 【方式2: 部分結合版 (SEPARATE_ENCODINGをコメントアウト)】
//   - 駒×タイル(9値) + 手持ち を結合
//   - メリット: 駒とタイルの相互作用を直接学習できる
//   - デメリット: メモリ使用量が多い（~220GB）
//   - 推奨: 十分なメモリがあり、より高精度な評価が必要な場合
//
// 切り替え方法:
//   完全分離版を使う場合: 下の行をそのまま有効にする
//   部分結合版を使う場合: 下の行をコメントアウト（// を追加）
// ============================================================================

#define SEPARATE_ENCODING  // ← この行をコメントアウトで切り替え

namespace contrast_ai {

/**
 * N-tupleパターン定義
 * 
 * N-tupleネットワークの基本単位となる局所パターン
 * 盤面の一部分（例：3x3の領域）の状態を表現
 * 
 * 主な役割：
 *   - どのセルを見るか（cell_indices）を定義
 *   - 盤面状態を一意のインデックスに変換
 *   - 状態数の計算（メモリ見積もりに使用）
 */
struct NTuple {
  static constexpr size_t MAX_CELLS = 25;  // 5x5盤面の最大セル数
  std::array<int, MAX_CELLS> cell_indices; // パターン内のセル位置（線形化インデックス y*5+x）
  size_t num_cells = 0;                    // パターンに含まれるセルの実際の数
  
  // 盤面をこのパターンのインデックスに変換
  // offset: パターンを盤面上で移動させる場合のオフセット
  long long to_index(const contrast::Board& board, int offset_x, int offset_y,
                     contrast::Player current_player) const;
  
  // このパターンが取りうる状態の総数を計算
  long long num_states() const;
  
  // セルの状態をエンコード
#ifdef SEPARATE_ENCODING
  // 完全分離版: 駒のみ (0=Empty, 1=My, 2=Opp)
  static int encode_cell_piece(const contrast::Cell& c, contrast::Player current_player);
  // タイルのみ (0=None, 1=Black, 2=Gray)
  static int encode_cell_tile(const contrast::Cell& c);
#else
  // 部分結合版: 駒×タイル (0-8の9値)
  static int encode_cell(const contrast::Cell& c, contrast::Player current_player);
#endif
};

/**
 * N-tupleネットワーク - 盤面評価と学習の中核
 * 
 * N-tuple Networkは強化学習で盤面を評価するための関数近似器
 * 複数のパターン（N-tuple）を組み合わせて盤面全体の価値を推定
 * 
 * アーキテクチャ：
 *   - 入力：盤面状態（5x5, 各セル9通り）
 *   - 中間：複数のパターン（現在は3x3など）
 *   - 出力：評価値（スカラー、正=有利、負=不利）
 * 
 * 学習方法：
 *   - TD(0)学習（Temporal Difference Learning）
 *   - セルフプレイまたは対戦相手との対局から学習
 *   - 勝敗の結果で最終調整
 * 
 * メモリ使用量：
 *   - 3x3パターン×1個: 9^9 = 約387百万状態 → 約1.4GB (float)
 *   - 2x2パターン×1個: 9^4 = 約6,561状態 → 約26KB (float)
 */
class NTupleNetwork {
public:
  NTupleNetwork();
  
  // Copy constructor for fast in-memory copying
  NTupleNetwork(const NTupleNetwork& other);
  
  // 盤面を評価（現在のプレイヤー視点）
  // 正の値=現在のプレイヤーに有利、負の値=不利
  float evaluate(const contrast::GameState& state) const;
  
  // TD学習で重みを更新
  // target = 報酬 + gamma * 次の状態の価値（または最終勝敗）
  // learning_rate = 更新の大きさ（通常0.001-0.01）
  void td_update(const contrast::GameState& state, float target, float learning_rate);
  
  // 重みの保存/読み込み（学習結果の永続化）
  void save(const std::string& filename) const;
  void load(const std::string& filename);
  
  // パターン数を取得
  size_t num_tuples() const { return tuples_.size(); }
  
  // 重みの総数を取得（デバッグ用）
  size_t num_weights() const;
  
  // パターン情報を取得（デバッグ・可視化用）
  const std::vector<NTuple>& get_tuples() const { return tuples_; }
  
private:
  std::vector<NTuple> tuples_;              // 駒配置用パターンのリスト
  std::vector<std::vector<float>> weights_; // 各パターンの重みテーブル
  std::vector<float> hand_weights_;         // 手持ちタイル評価テーブル
  
#ifdef SEPARATE_ENCODING
  std::vector<NTuple> tile_tuples_;              // タイル配置用パターン（完全分離版のみ）
  std::vector<std::vector<float>> tile_weights_; // タイルパターンの重みテーブル
#endif
  
  // パターンの初期化
  void init_tuples();
  
  // 手持ちからインデックスを計算 (0-7)
  static int hand_index(int black_remain, int gray_remain);
  
  // 盤面から特徴インデックスを抽出（内部用）
  std::vector<int> extract_features(const contrast::Board& board, 
                                    contrast::Player current_player) const;
};

/**
 * N-tupleネットワークを使用するポリシー
 * 
 * 学習済みN-tupleネットワークを使って実際にゲームをプレイ
 * 
 * 使用例：
 *   // 学習済みモデルを読み込んで対局
 *   NTuplePolicy policy("weights_10k.bin");
 *   Move move = policy.pick(current_state);
 * 
 * 強化の方向性：
 *   1. ミニマックス探索の追加
 *      - 現在は1手先のみ評価
 *      - 2-3手先まで読めばより強く
 *   2. アルファベータ枝刈り
 *      - 探索の効率化
 *   3. 評価関数の改善
 *      - 勝利判定の追加
 *      - 詰みチェック
 */
class NTuplePolicy {
public:
  // デフォルトコンストラクタ（未学習の状態）
  NTuplePolicy();
  
  // 学習済み重みを読み込んで初期化
  explicit NTuplePolicy(const std::string& weights_file);
  
  // 最良の手を選択
  // 全ての合法手を評価し、評価値が最大のものを返す
  contrast::Move pick(const contrast::GameState& s);
  
  // 重みの保存/読み込み
  bool save(const std::string& filename) const;
  bool load(const std::string& filename);
  
  // 学習用にネットワークへのアクセスを提供
  NTupleNetwork& network() { return network_; }
  const NTupleNetwork& network() const { return network_; }
  
private:
  NTupleNetwork network_; // 評価に使用するネットワーク
  std::mt19937 rng_;      // 同点の手からランダム選択用
};

} // namespace contrast_ai
