#include "contrast/rules.hpp"
#include "contrast/game_state.hpp"

namespace contrast {

// Direction vectors as fixed arrays
static constexpr int ORTHO[4][2] = {{1,0},{-1,0},{0,1},{0,-1}};
static constexpr int DIAG[4][2] = {{1,1},{1,-1},{-1,1},{-1,-1}};
static constexpr int ALL_8[8][2] = {{1,0},{-1,0},{0,1},{0,-1},{1,1},{1,-1},{-1,1},{-1,-1}};

void Rules::legal_moves(const GameState& s, MoveList& out) {
  out.clear();
  
  const Board& b = s.board();
  Player p = s.current_player();
  
  // Temporary storage for base moves (without tile placement)
  MoveList base_moves;

  for (int y = 0; y < b.height(); ++y) {
    for (int x = 0; x < b.width(); ++x) {
      if (b.at(x,y).occupant != p) continue;

      // Choose directions depending on tile under the piece
      const int (*dirs)[2] = nullptr;
      int num_dirs = 0;
      
      if (b.at(x,y).tile == TileType::None) {
        dirs = ORTHO;
        num_dirs = 4;
      } else if (b.at(x,y).tile == TileType::Black) {
        dirs = DIAG;
        num_dirs = 4;
      } else { // gray
        dirs = ALL_8;
        num_dirs = 8;
      }

      for (int i = 0; i < num_dirs; ++i) {
        int dx = dirs[i][0];
        int dy = dirs[i][1];
        int tx = x + dx;
        int ty = y + dy;
        
        if (!b.in_bounds(tx,ty)) continue;
        // blocked by opponent
        if (b.at(tx,ty).occupant != Player::None && b.at(tx,ty).occupant != p) continue;

        // if empty adjacent, simple move
        if (b.at(tx,ty).occupant == Player::None) {
          Move m; 
          m.sx = x; m.sy = y; m.dx = tx; m.dy = ty; m.place_tile = false;
          base_moves.push_back(m);
        } else {
          // jump over own pieces until first empty; opponent blocks
          int jx = tx;
          int jy = ty;
          while (b.in_bounds(jx, jy) && b.at(jx,jy).occupant == p) {
            jx += dx; jy += dy;
          }
          if (b.in_bounds(jx,jy) && b.at(jx,jy).occupant == Player::None) {
            Move m; 
            m.sx = x; m.sy = y; m.dx = jx; m.dy = jy; m.place_tile = false;
            base_moves.push_back(m);
          }
        }
      }
    }
  }

  // For each base move, add optional tile placement variants
  const TileInventory& inv = s.inventory(p);
  
  for (size_t i = 0; i < base_moves.size; ++i) {
    const Move& base = base_moves[i];
    
    // No tile placement
    out.push_back(base);
    
    // With black tile placement
    if (inv.black > 0) {
      for (int y = 0; y < b.height(); ++y) {
        for (int x = 0; x < b.width(); ++x) {
          if (b.at(x,y).occupant == Player::None && b.at(x,y).tile == TileType::None) {
            Move m = base; 
            m.place_tile = true; 
            m.tx = x; 
            m.ty = y; 
            m.tile = TileType::Black;
            out.push_back(m);
          }
        }
      }
    }
    
    // With gray tile placement
    if (inv.gray > 0) {
      for (int y = 0; y < b.height(); ++y) {
        for (int x = 0; x < b.width(); ++x) {
          if (b.at(x,y).occupant == Player::None && b.at(x,y).tile == TileType::None) {
            Move m = base;
            m.place_tile = true;
            m.tx = x;
            m.ty = y;
            m.tile = TileType::Gray;
            out.push_back(m);
          }
        }
      }
    }
  }
}

bool Rules::is_win(const GameState& s, Player p) {
  const Board& b = s.board();
  // player wins if any of their pieces on opponent's back row
  int target_row = (p == Player::Black) ? (b.height()-1) : 0;
  for (int x = 0; x < b.width(); ++x) if (b.at(x,target_row).occupant == p) return true;
  return false;
}

bool Rules::is_loss(const GameState& s, Player p) {
  (void)p;
  // loss if no legal moves
  MoveList moves;
  legal_moves(s, moves);
  return moves.empty();
}

} // namespace contrast
