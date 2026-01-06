#pragma once
#include "contrast/board.hpp"
#include "contrast/types.hpp"
#include <array>

namespace contrast {

// 2つの対称変換: 恒等変換と左右反転
// 注意: 黒が上から下へ、白が下から上へ進むため回転対称性はない
enum class Symmetry : uint8_t {
  Identity = 0,    // 元の状態
  FlipH = 1        // 水平反転（左右反転）
};

constexpr int NUM_SYMMETRIES = 2;

namespace SymmetryOps {

// 座標変換関数 (5x5ボード用)
inline void transform_coords(int& x, int& y, Symmetry sym) {
  constexpr int W = BOARD_W;
  
  if (sym == Symmetry::FlipH) {
    // 水平反転: (x,y) -> (W-1-x, y)
    x = W - 1 - x;
  }
  // Identity の場合は何もしない
}

// ボード全体を変換
inline Board transform_board(const Board& original, Symmetry sym) {
  if (sym == Symmetry::Identity) {
    return original;
  }
  
  Board result;
  constexpr int W = BOARD_W;
  
  // 水平反転
  for (int y = 0; y < BOARD_H; ++y) {
    for (int x = 0; x < BOARD_W; ++x) {
      result.at(x, y) = original.at(W - 1 - x, y);
    }
  }
  
  return result;
}

// 正規化: 2つの対称性（恒等変換と左右反転）のうち辞書順最小のものを選ぶ
inline Symmetry get_canonical_symmetry(const Board& board) {
  // ボードのハッシュ値を計算
  auto board_hash = [](const Board& b) -> uint64_t {
    uint64_t hash = 0;
    for (int y = 0; y < BOARD_H; ++y) {
      for (int x = 0; x < BOARD_W; ++x) {
        const auto& cell = b.at(x, y);
        hash = hash * 9 + static_cast<int>(cell.occupant) * 3 + static_cast<int>(cell.tile);
      }
    }
    return hash;
  };
  
  uint64_t original_hash = board_hash(board);
  Board flipped = transform_board(board, Symmetry::FlipH);
  uint64_t flipped_hash = board_hash(flipped);
  
  // 辞書順で小さい方を選ぶ
  return (flipped_hash < original_hash) ? Symmetry::FlipH : Symmetry::Identity;
}

} // namespace SymmetryOps
} // namespace contrast
