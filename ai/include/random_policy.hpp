#pragma once
#include "contrast/game_state.hpp"
#include "contrast/move.hpp"
#include <vector>
#include <random>

namespace contrast_ai {

class RandomPolicy {
public:
  RandomPolicy();
  contrast::Move pick(const contrast::GameState& s);
  
private:
  std::mt19937 rng_;
};

} // namespace contrast_ai
