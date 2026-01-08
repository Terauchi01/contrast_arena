#include <iostream>
#include <chrono>
#include "alphabeta.hpp"
#include "contrast/game_state.hpp"

int main() {
  using namespace contrast_ai;
  using namespace contrast;
  AlphaBeta ab;
  ab.set_use_transposition_table(true);
  ab.set_use_move_ordering(true);
  ab.set_verbose(false);

  GameState s;
  // optional: reset to initial position
  s.reset();

  auto start = std::chrono::steady_clock::now();
  auto deadline = start + std::chrono::milliseconds(100);
  // use public search API with time_ms (pass max_depth negative so time is used)
  auto mv = ab.search(s, -1, 100);
  auto end = std::chrono::steady_clock::now();

  auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
  const auto& stats = ab.get_stats();

  std::cout << "Elapsed ms: " << elapsed_ms << std::endl;
  std::cout << "Nodes searched: " << stats.nodes_searched << std::endl;
  std::cout << "Max depth reached: " << stats.max_depth_reached << std::endl;
  std::cout << "TT hits: " << stats.tt_hits << ", TT cutoffs: " << stats.tt_cutoffs << ", Beta cutoffs: " << stats.beta_cutoffs << std::endl;

  std::cout << "Returned move: ";
  if (mv.sx >= 0) {
    std::cout << "(" << mv.sx << "," << mv.sy << ") -> (" << mv.dx << "," << mv.dy << ")";
    if (mv.place_tile) {
      std::cout << ", place tile (" << mv.tx << "," << mv.ty << ") type=" << static_cast<int>(mv.tile);
    }
  } else {
    std::cout << "(pass or none)";
  }
  std::cout << std::endl;
  return 0;
}
