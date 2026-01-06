#pragma once
#include "contrast/game_state.hpp"
#include "contrast/move.hpp"
#include <vector>
#include <random>

namespace contrast_ai {

// Greedy policy: always try to move forward (toward opponent's back row)
// If multiple forward moves exist, pick one randomly
class GreedyPolicy {
public:
  GreedyPolicy();
  contrast::Move pick(const contrast::GameState& s);
  
private:
  std::mt19937 rng_;
};

} // namespace contrast_ai
