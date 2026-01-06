#pragma once
#include "contrast/types.hpp"
#include "contrast/move.hpp"
#include "contrast/move_list.hpp"

namespace contrast {

class GameState; // forward

namespace Rules {
  void legal_moves(const GameState& s, MoveList& out);
  bool is_win(const GameState& s, Player p);
  bool is_loss(const GameState& s, Player p);
}

} // namespace contrast
