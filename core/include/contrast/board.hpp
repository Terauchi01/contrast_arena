#pragma once
#include "contrast/types.hpp"
#include <array>
#include <optional>

namespace contrast {

struct Cell {
  Player occupant = Player::None;
  TileType tile = TileType::None;
};

class Board {
public:
  Board();
  int width() const { return BOARD_W; }
  int height() const { return BOARD_H; }
  void reset();

  bool in_bounds(int x, int y) const {
    return x >= 0 && x < width() && y >= 0 && y < height();
  }

  Cell& at(int x, int y) { return cells_[y * width() + x]; }
  const Cell& at(int x, int y) const { return cells_[y * width() + x]; }

private:
  std::array<Cell, BOARD_W * BOARD_H> cells_;
};

} // namespace contrast
