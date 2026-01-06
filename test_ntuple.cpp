#include <iostream>
#include "ntuple_big.hpp"
#include "contrast/game_state.hpp"

int main() {
    std::cout << "=== NTuple Test ===" << std::endl;
    
    // Create NTuplePolicy
    std::cout << "Creating NTuplePolicy..." << std::endl;
    contrast_ai::NTuplePolicy policy;
    
    // Try to load weights
    std::cout << "Loading weights..." << std::endl;
    const std::string weights_path = "ai/bin/ntuple_weights_vs_rulebased_swap.bin.100000";
    bool loaded = policy.load(weights_path);
    
    if (loaded) {
        std::cout << "[SUCCESS] Weights loaded from " << weights_path << std::endl;
    } else {
        std::cout << "[FAILED] Could not load weights from " << weights_path << std::endl;
        return 1;
    }
    
    // Create a game state
    std::cout << "\nCreating initial game state..." << std::endl;
    contrast::GameState state;
    state.reset();
    
    // Try to pick a move
    std::cout << "Trying to pick a move..." << std::endl;
    try {
        contrast::Move move = policy.pick(state);
        std::cout << "[SUCCESS] Picked move from (" << move.sx << "," << move.sy << ")";
        std::cout << " to (" << move.dx << "," << move.dy << ")";
        if (move.place_tile) {
            std::cout << " + tile at (" << move.tx << "," << move.ty << ")";
        }
        std::cout << std::endl;
    } catch (const std::exception& e) {
        std::cout << "[ERROR] Exception during pick: " << e.what() << std::endl;
        return 1;
    }
    
    std::cout << "\n=== All tests passed ===" << std::endl;
    return 0;
}
