#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include "contrast/game_state.hpp"
#include "ntuple_big.hpp"
#include <sstream>

using namespace contrast;
using namespace contrast_ai;

// parse 5x5 tokens where each token is like "X.", ".b", "Ob", etc.
GameState parse_board(const std::vector<std::string>& rows) {
  GameState s;
  s.reset();
  for (int y = 0; y < 5; ++y) {
    int row = y;
    std::istringstream iss(rows[y]);
    for (int x = 0; x < 5; ++x) {
      std::string tok;
      if (!(iss >> tok)) continue;
      // default
      s.board().at(x,row).occupant = Player::None;
      s.board().at(x,row).tile = TileType::None;
      if (tok.size() >= 1) {
        char c0 = tok[0];
        if (c0 == 'X') s.board().at(x,row).occupant = Player::Black;
        else if (c0 == 'O') s.board().at(x,row).occupant = Player::White;
      }
      if (tok.size() >= 2) {
        char c1 = tok[1];
        if (c1 == 'b') s.board().at(x,row).tile = TileType::Black;
        else if (c1 == 'g') s.board().at(x,row).tile = TileType::Gray;
      }
    }
  }
  return s;
}

// transformations
GameState transform_sym(const GameState& s, int sym) {
  // sym: 0 identity,1 rot90,2 rot180,3 rot270,4 mirrorX,5 mirrorY,6 diag,7 anti-diag
  GameState out;
  out.reset();
  for (int y=0;y<5;++y) for (int x=0;x<5;++x) {
    int nx=x, ny=y;
    switch(sym) {
      case 0: nx=x; ny=y; break;
      case 1: nx=4-y; ny=x; break;
      case 2: nx=4-x; ny=4-y; break;
      case 3: nx=y; ny=4-x; break;
      case 4: nx=4-x; ny=y; break; // mirror vertical
      case 5: nx=x; ny=4-y; break; // mirror horizontal
      case 6: nx=y; ny=x; break; // diag
      case 7: nx=4-y; ny=4-x; break; // anti-diag
    }
    out.board().at(nx,ny) = s.board().at(x,y);
  }
  // inventories and to_move not set here
  return out;
}

int main(){
  NTupleNetwork net;
  const char* weights = "ai/bin/ntuple_weights_vs_rulebased_swap.bin.100000";
  net.load(weights);

  // Board A
  std::vector<std::string> A = {
    ".b .. X. X. ..",
    ".. .b O. .. X.",
    ".b X. X. .b .g",
    ".g O. .b O. Ob",
    ".. .. .. .. O."
  };
  // Board B
  std::vector<std::string> B = {
    ".. .. .. .. X.",
    ".g X. .b X. Xb",
    ".b O. O. .b .g",
    ".. .b X. .. O.",
    ".b .. O. O. .."
  };

  GameState sA = parse_board(A);
  GameState sB = parse_board(B);

  sA.to_move_ = Player::Black;
  sB.to_move_ = Player::Black;

  float vA = net.evaluate(sA);
  float vB = net.evaluate(sB);

  std::cout << "Eval A (Black to move): " << vA << "\n";
  std::cout << "Eval B (Black to move): " << vB << "\n";

  // Now create swapped version of A: swap occupants, swap tiles (b<->g), swap inventories, and try symmetries
  GameState A_swapped = sA;
  // swap occupants and tiles
  for (int y=0;y<5;++y) for (int x=0;x<5;++x) {
    auto& c = A_swapped.board().at(x,y);
    if (c.occupant == Player::Black) c.occupant = Player::White;
    else if (c.occupant == Player::White) c.occupant = Player::Black;
    if (c.tile == TileType::Black) c.tile = TileType::Gray;
    else if (c.tile == TileType::Gray) c.tile = TileType::Black;
  }
  // swap inventories
  auto ib = A_swapped.inventory(Player::Black);
  auto iw = A_swapped.inventory(Player::White);
  A_swapped.inventory(Player::Black) = iw;
  A_swapped.inventory(Player::White) = ib;
  // flip to_move
  A_swapped.to_move_ = (sA.current_player() == Player::Black) ? Player::White : Player::Black;

  // Evaluate swapped A without symmetry
  float vA_swapped = net.evaluate(A_swapped);
  std::cout << "Eval A_swapped (to_move flipped): " << vA_swapped << "\n";

  // Try applying symmetries to A_swapped and compare absolute occupancy to B
  bool match_found = false;
  for (int sym=0;sym<8;++sym){
    GameState t = transform_sym(A_swapped, sym);
    // inventories and to_move need to be set
    t.inventory(Player::Black) = A_swapped.inventory(Player::Black);
    t.inventory(Player::White) = A_swapped.inventory(Player::White);
    t.to_move_ = A_swapped.to_move_;

    // compare absolute boards (occupant and tile)
    bool same = true;
    for (int y=0;y<5 && same;++y) for (int x=0;x<5;++x){
      auto ca = t.board().at(x,y);
      auto cb = sB.board().at(x,y);
      if (ca.occupant != cb.occupant || ca.tile != cb.tile) { same = false; break; }
    }
    if (same) {
      match_found = true;
      float vT = net.evaluate(t);
      std::cout << "Match found with sym="<<sym<<" eval(t)="<<vT<<" eval(B)="<<vB<<" diff="<<(vT - vB)<<"\n";
    }
  }

  if (!match_found) std::cout << "No exact swapped+symmetry match of A to B found.\n";

  return 0;
}
