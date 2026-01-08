#include <iostream>
#include <random>
#include <string>
#include <vector>
#include <chrono>

#include "contrast/game_state.hpp"
#include "contrast/move_list.hpp"
#include "ntuple_big.hpp"
#include "contrast/rules.hpp"

using namespace contrast;
using namespace contrast_ai;

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "Usage: check_eval_random <weights-file> [N=100] [max_moves=20]" << std::endl;
    return 1;
  }
  std::string weights = argv[1];
  int N = 100;
  int max_moves = 20;
  if (argc >= 3) N = std::stoi(argv[2]);
  if (argc >= 4) max_moves = std::stoi(argv[3]);

  NTupleNetwork net;
  if (!weights.empty()) {
    try {
      net.load(weights);
    } catch (...) {
      std::cerr << "Warning: failed to load weights: " << weights << std::endl;
    }
  }

  std::mt19937_64 rng((unsigned)std::chrono::high_resolution_clock::now().time_since_epoch().count());
  std::uniform_int_distribution<int> move_dist(1, max_moves);

  int ok = 0;
  int bad = 0;
  double sum_abs_err = 0.0;
  double max_abs_err = 0.0;

  for (int i = 0; i < N; ++i) {
    GameState s;
    s.reset();

    // Play a random length of random legal moves to get varied non-terminal positions
    int L = move_dist(rng);
    for (int mv = 0; mv < L; ++mv) {
      MoveList moves;
      Rules::legal_moves(s, moves);
      if (moves.size == 0) break; // terminal
      std::uniform_int_distribution<int> pick(0, (int)moves.size - 1);
      Move m = moves[pick(rng)];
      s.apply_move(m);
      // if terminal, stop
      float term_val;
      if (Rules::is_win(s, Player::Black) || Rules::is_win(s, Player::White)) break;
    }

    // Ensure not terminal; if terminal, skip and continue
    MoveList final_moves; Rules::legal_moves(s, final_moves);
    if (final_moves.size == 0) { --i; continue; }

    // Evaluate with Black to move
    s.to_move_ = Player::Black;
    float v_black = net.evaluate(s);
    // Evaluate with White to move
    s.to_move_ = Player::White;
    float v_white = net.evaluate(s);

    // Create swapped state: swap piece ownership, swap tile colors (Black<->Gray), swap inventories, and flip to_move
    GameState swapped = s;
    // swap board occupants and tiles
    for (int y = 0; y < swapped.board().height(); ++y) {
      for (int x = 0; x < swapped.board().width(); ++x) {
        auto& c = swapped.board().at(x, y);
        if (c.occupant == Player::Black) c.occupant = Player::White;
        else if (c.occupant == Player::White) c.occupant = Player::Black;

        if (c.tile == TileType::Black) c.tile = TileType::Gray;
        else if (c.tile == TileType::Gray) c.tile = TileType::Black;
      }
    }
    // swap inventories
    auto inv_b = swapped.inventory(Player::Black);
    auto inv_w = swapped.inventory(Player::White);
    swapped.inventory(Player::Black) = inv_w;
    swapped.inventory(Player::White) = inv_b;

    // flip to_move
    swapped.to_move_ = (s.current_player() == Player::Black) ? Player::White : Player::Black;

    float v_swapped = net.evaluate(swapped);
    double err_swapped = std::abs(v_black + v_swapped);

    std::cout << " Eval(orig Black): " << v_black << " Eval(swapped, flipped to_move): " << v_swapped << " err_swapped=" << err_swapped << "\n";

    // Print board for both perspectives for small N (helps verify only to_move differs)
    auto print_absolute = [&](const GameState& st) {
      const auto& b = st.board();
      std::cout << " (absolute) to_move=" << (st.current_player() == Player::Black ? 'X' : 'O') << "\n";
      for (int y = 0; y < b.height(); ++y) {
        for (int x = 0; x < b.width(); ++x) {
          const auto& c = b.at(x, y);
          char occ = (c.occupant == Player::Black) ? 'X' : (c.occupant == Player::White) ? 'O' : '.';
          char tile = (c.tile == TileType::None) ? '.' : (c.tile == TileType::Black) ? 'b' : 'g';
          std::cout << occ << tile << " ";
        }
        std::cout << "\n";
      }
    };

    auto print_perspective = [&](const GameState& st) {
      const auto& b = st.board();
      std::cout << " (perspective) to_move=" << (st.current_player() == Player::Black ? 'X' : 'O') << "\n";
      for (int y = 0; y < b.height(); ++y) {
        for (int x = 0; x < b.width(); ++x) {
          const auto& c = b.at(x, y);
          char occ;
          if (c.occupant == Player::None) occ = '.';
          else if (c.occupant == st.current_player()) occ = 'M';
          else occ = 'E'; // enemy
          char tile = (c.tile == TileType::None) ? '.' : (c.tile == TileType::Black) ? 'b' : 'g';
          std::cout << occ << tile << " ";
        }
        std::cout << "\n";
      }
    };

    std::cout << "--- Position i=" << i << " ---\n";
    GameState tmp = s;
    tmp.to_move_ = Player::Black;
    std::cout << "Black to move:"; print_absolute(tmp); std::cout << " "; print_perspective(tmp);
    tmp.to_move_ = Player::White;
    std::cout << "White to move:"; print_absolute(tmp); std::cout << " "; print_perspective(tmp);

    double err = std::abs(v_black + v_white);
    sum_abs_err += err;
    if (err > max_abs_err) max_abs_err = err;

    // small tolerance: consider OK if |v_black + v_white| < 1e-2
    if (err < 1e-2) ++ok; else ++bad;

    if (err >= 1e-2) {
      std::cout << "[BAD] i=" << i << " v_black=" << v_black << " v_white=" << v_white << " err=" << err << std::endl;
    }
  }

  std::cout << "Tested: " << (ok + bad) << " positions\n";
  std::cout << "OK (|v+v'|<1e-2): " << ok << ", Bad: " << bad << "\n";
  std::cout << "Mean abs err: " << (sum_abs_err / std::max(1, ok+bad)) << ", Max abs err: " << max_abs_err << "\n";

  return 0;
}
