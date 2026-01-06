#include "contrast/game_state.hpp"
#include <sstream>

namespace contrast {

GameState::GameState() { reset(); }

void GameState::reset() {
  board_.reset();
  to_move_ = Player::Black;
  inv_black_ = TileInventory();
  inv_white_ = TileInventory();
  history_.clear();
  uint64_t h = compute_hash();
  history_[h] = 1;
}

void GameState::apply_move(const Move& m) {
  if (!board_.in_bounds(m.sx, m.sy)) return;
  if (!board_.in_bounds(m.dx, m.dy)) return;
  Player p = to_move_;
  // move piece
  board_.at(m.dx,m.dy).occupant = board_.at(m.sx,m.sy).occupant;
  board_.at(m.sx,m.sy).occupant = Player::None;

  // tile placement
  if (m.place_tile) {
    if (board_.in_bounds(m.tx, m.ty) && board_.at(m.tx,m.ty).tile == TileType::None && board_.at(m.tx,m.ty).occupant == Player::None) {
      board_.at(m.tx,m.ty).tile = m.tile;
      TileInventory& inv = inventory(p);
      if (m.tile == TileType::Black && inv.black > 0) inv.black -= 1;
      if (m.tile == TileType::Gray && inv.gray > 0) inv.gray -= 1;
    }
  }

  // update to_move and history
  to_move_ = (to_move_ == Player::Black) ? Player::White : Player::Black;
  uint64_t h = compute_hash();
  history_[h] += 1;
}

uint64_t GameState::compute_hash() const {
  // simple serialization-based hash for now
  std::size_t seed = 1469598103934665603ULL;
  auto mix = [&](uint64_t v){ seed ^= v; seed *= 1099511628211ULL; };
  
  // Unrolled loop for 5x5 board (25 cells)
  const Cell* cells = &board_.at(0, 0);
  for (int i = 0; i < 25; ++i) {
    mix(static_cast<uint64_t>(cells[i].occupant));
    mix(static_cast<uint64_t>(cells[i].tile));
  }
  
  mix(static_cast<uint64_t>(to_move_));
  return seed;
}

} // namespace contrast
