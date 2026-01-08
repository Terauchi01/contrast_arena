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

inline bool operator==(const Move& a, const Move& b) {
  return a.sx == b.sx && a.sy == b.sy && a.dx == b.dx && a.dy == b.dy
      && a.place_tile == b.place_tile && a.tx == b.tx && a.ty == b.ty && a.tile == b.tile;
}

inline bool operator!=(const Move& a, const Move& b) {
  return !(a == b);
}

} // namespace contrast
