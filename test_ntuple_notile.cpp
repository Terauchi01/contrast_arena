#include <iostream>
#include "ntuple_big.hpp"
#include "contrast/game_state.hpp"
#include "contrast/rules.hpp"

void print_move(const contrast::Move& m) {
    char cols[] = {'a', 'b', 'c', 'd', 'e'};
    std::cout << "Move: " << cols[m.sx] << (m.sy + 1) 
              << " -> " << cols[m.dx] << (m.dy + 1);
    if (m.place_tile) {
        std::cout << " + tile at " << cols[m.tx] << (m.ty + 1);
        if (m.tile == contrast::TileType::Black) {
            std::cout << " (Black)";
        } else if (m.tile == contrast::TileType::Gray) {
            std::cout << " (Gray)";
        }
    } else {
        std::cout << " (no tile)";
    }
    std::cout << std::endl;
}

int main() {
    std::cout << "=== Testing NTuple with and without tile placement ===" << std::endl;
    
    // Create NTuplePolicy and load weights
    contrast_ai::NTuplePolicy policy;
    const std::string weights_path = "ai/bin/ntuple_weights_vs_rulebased_swap.bin.100000";
    if (!policy.load(weights_path)) {
        std::cerr << "Failed to load weights" << std::endl;
        return 1;
    }
    std::cout << "Weights loaded successfully\n" << std::endl;
    
    // Test Turn 1 (with tile)
    std::cout << "=== Turn 1 (Player X) ===" << std::endl;
    contrast::GameState state;
    state.reset();
    
    contrast::MoveList moves;
    contrast::Rules::legal_moves(state, moves);
    std::cout << "Legal moves: " << moves.size << std::endl;
    
    // Count moves with and without tiles
    int with_tile = 0, without_tile = 0;
    for (size_t i = 0; i < moves.size; ++i) {
        if (moves[i].place_tile) with_tile++;
        else without_tile++;
    }
    std::cout << "  With tile: " << with_tile << std::endl;
    std::cout << "  Without tile: " << without_tile << std::endl;
    
    contrast::Move chosen = policy.pick(state);
    std::cout << "Chosen: ";
    print_move(chosen);
    
    // Apply the move
    state.apply_move(chosen);
    std::cout << std::endl;
    
    // Test Turn 2 (without tile available)
    std::cout << "=== Turn 2 (Player O) ===" << std::endl;
    contrast::Rules::legal_moves(state, moves);
    std::cout << "Legal moves: " << moves.size << std::endl;
    
    with_tile = 0, without_tile = 0;
    for (size_t i = 0; i < moves.size; ++i) {
        if (moves[i].place_tile) with_tile++;
        else without_tile++;
    }
    std::cout << "  With tile: " << with_tile << std::endl;
    std::cout << "  Without tile: " << without_tile << std::endl;
    
    // Show a few example moves
    std::cout << "\nFirst 5 legal moves:" << std::endl;
    for (size_t i = 0; i < std::min(static_cast<size_t>(5), static_cast<size_t>(moves.size)); ++i) {
        std::cout << "  " << i << ": ";
        print_move(moves[i]);
    }
    
    chosen = policy.pick(state);
    std::cout << "\nChosen: ";
    print_move(chosen);
    
    // Apply the move
    state.apply_move(chosen);
    std::cout << std::endl;
    
    // Test Turn 3
    std::cout << "=== Turn 3 (Player X) ===" << std::endl;
    contrast::Rules::legal_moves(state, moves);
    std::cout << "Legal moves: " << moves.size << std::endl;
    
    with_tile = 0, without_tile = 0;
    for (size_t i = 0; i < moves.size; ++i) {
        if (moves[i].place_tile) with_tile++;
        else without_tile++;
    }
    std::cout << "  With tile: " << with_tile << std::endl;
    std::cout << "  Without tile: " << without_tile << std::endl;
    
    chosen = policy.pick(state);
    std::cout << "Chosen: ";
    print_move(chosen);
    
    std::cout << "\n=== Test completed ===" << std::endl;
    return 0;
}
