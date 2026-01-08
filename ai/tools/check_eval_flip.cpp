#include <iostream>
#include "contrast/game_state.hpp"
#include "ntuple_big.hpp"

using namespace contrast;
using namespace contrast_ai;

int main() {
  GameState s;
  s.reset();

  // Set up a sample position: place a Black piece at (0,0) and White at (4,4)
  s.board().at(0,0).occupant = Player::Black;
  s.board().at(4,4).occupant = Player::White;
  s.inventory(Player::Black).black = 2;
  s.inventory(Player::White).black = 3;

  NTupleNetwork net;

  // Evaluate with X (Black) to move
  s.to_move_ = Player::Black;
  float v_black = net.evaluate(s);

  // Same board, flip to White to move
  s.to_move_ = Player::White;
  float v_white = net.evaluate(s);

  std::cout << "Eval (Black to move): " << v_black << std::endl;
  std::cout << "Eval (White to move): " << v_white << std::endl;
  std::cout << "Sum: " << (v_black + v_white) << std::endl;
  std::cout << "Negated White: " << (-v_white) << std::endl;

  if (std::abs(v_black + v_white) < 1e-3) {
    std::cout << "Result: SIGN-FLIP OK (v_black ≈ -v_white)" << std::endl;
  } else {
    std::cout << "Result: SIGN-FLIP MISMATCH" << std::endl;
  }

  return 0;
}
