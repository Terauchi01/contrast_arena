#pragma once
#include "contrast/types.hpp"
#include <optional>

namespace contrast {

struct Move {
  // source
  int sx = -1;
  int sy = -1;
  // destination
  int dx = -1;
  int dy = -1;
  // optional tile placement after move
  bool place_tile = false;
  int tx = -1;
  int ty = -1;
  TileType tile = TileType::None;
};

} // namespace contrast
