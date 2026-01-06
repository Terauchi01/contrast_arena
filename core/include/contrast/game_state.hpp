#pragma once
#include "contrast/board.hpp"
#include "contrast/types.hpp"
#include "contrast/move.hpp"
#include <vector>
#include <unordered_map>
#include <cstdint>

namespace contrast {

struct TileInventory {
  int black = 3;
  int gray = 1;
};

class GameState {
public:
  GameState();
  void reset();
  Player current_player() const { return to_move_; }

  const Board& board() const { return board_; }
  Board& board() { return board_; }

  TileInventory& inventory(Player p) { return (p == Player::Black) ? inv_black_ : inv_white_; }
  const TileInventory& inventory(Player p) const { return (p == Player::Black) ? inv_black_ : inv_white_; }

  void apply_move(const Move& m);
  uint64_t compute_hash() const;

  Player to_move_ = Player::Black;

  // history of hashes for repetition detection
  std::unordered_map<uint64_t,int> history_;

private:
  Board board_;
  TileInventory inv_black_;
  TileInventory inv_white_;
};

} // namespace contrast
