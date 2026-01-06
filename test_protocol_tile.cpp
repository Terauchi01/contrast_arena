#include <iostream>
#include "ntuple_big.hpp"
#include "contrast/game_state.hpp"
#include "contrast/rules.hpp"
#include "protocol.hpp"

std::string xy_to_coord(int x, int y) {
    const char cols[] = {'a', 'b', 'c', 'd', 'e'};
    return std::string(1, cols[x]) + std::to_string(y + 1);
}

char tile_to_char(contrast::TileType tile) {
    if (tile == contrast::TileType::Black) return 'b';
    if (tile == contrast::TileType::Gray) return 'g';
    return '-';
}

protocol::Move convert_core_move(const contrast::Move& move) {
    protocol::Move out;
    out.origin = xy_to_coord(move.sx, move.sy);
    out.target = xy_to_coord(move.dx, move.dy);
    if (move.place_tile) {
        out.tile.skip = false;
        out.tile.coord = xy_to_coord(move.tx, move.ty);
        out.tile.color = tile_to_char(move.tile);
    } else {
        out.tile = protocol::TilePlacement::none();
    }
    return out;
}

int main() {
    std::cout << "=== Testing tile placement protocol ===" << std::endl;
    
    // Test with tile
    contrast::Move move_with_tile;
    move_with_tile.sx = 2; move_with_tile.sy = 0;
    move_with_tile.dx = 2; move_with_tile.dy = 1;
    move_with_tile.place_tile = true;
    move_with_tile.tx = 2; move_with_tile.ty = 2;
    move_with_tile.tile = contrast::TileType::Gray;
    
    protocol::Move proto_with = convert_core_move(move_with_tile);
    std::string text_with = protocol::format_move(proto_with);
    std::cout << "Move WITH tile: " << text_with << std::endl;
    std::cout << "  skip=" << proto_with.tile.skip << std::endl;
    
    // Test without tile
    contrast::Move move_without_tile;
    move_without_tile.sx = 1; move_without_tile.sy = 4;
    move_without_tile.dx = 1; move_without_tile.dy = 3;
    move_without_tile.place_tile = false;
    
    protocol::Move proto_without = convert_core_move(move_without_tile);
    std::string text_without = protocol::format_move(proto_without);
    std::cout << "\nMove WITHOUT tile: " << text_without << std::endl;
    std::cout << "  skip=" << proto_without.tile.skip << std::endl;
    
    // Now test with actual policy
    std::cout << "\n=== Testing with NTuple policy ===" << std::endl;
    contrast_ai::NTuplePolicy policy;
    if (!policy.load("ai/bin/ntuple_weights_vs_rulebased_swap.bin.100000")) {
        std::cerr << "Failed to load weights" << std::endl;
        return 1;
    }
    
    contrast::GameState state;
    state.reset();
    
    // Play first move
    contrast::Move m1 = policy.pick(state);
    protocol::Move p1 = convert_core_move(m1);
    std::string t1 = protocol::format_move(p1);
    std::cout << "Turn 1: " << t1 << " (place_tile=" << m1.place_tile << ")" << std::endl;
    state.apply_move(m1);
    
    // Play second move
    contrast::Move m2 = policy.pick(state);
    protocol::Move p2 = convert_core_move(m2);
    std::string t2 = protocol::format_move(p2);
    std::cout << "Turn 2: " << t2 << " (place_tile=" << m2.place_tile << ")" << std::endl;
    state.apply_move(m2);
    
    // Play third move - check if it can be without tile
    contrast::Move m3 = policy.pick(state);
    protocol::Move p3 = convert_core_move(m3);
    std::string t3 = protocol::format_move(p3);
    std::cout << "Turn 3: " << t3 << " (place_tile=" << m3.place_tile << ")" << std::endl;
    
    // Check how many moves are available without tiles
    contrast::MoveList moves;
    contrast::Rules::legal_moves(state, moves);
    int without_tile = 0;
    for (size_t i = 0; i < moves.size; ++i) {
        if (!moves[i].place_tile) without_tile++;
    }
    std::cout << "  (Available moves without tile: " << without_tile << "/" << moves.size << ")" << std::endl;
    
    std::cout << "\n=== Test completed ===" << std::endl;
    return 0;
}
