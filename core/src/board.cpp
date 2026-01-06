#include "contrast/board.hpp"
using namespace contrast;

Board::Board() { reset(); }

void Board::reset() {
  // clear all 25 cells (5x5 board)
  for (int i = 0; i < 25; ++i) {
    cells_[i].occupant = Player::None;
    cells_[i].tile = TileType::None;
  }

  // initial pieces: top row Black (y=0, x=0..4), bottom row White (y=4, x=0..4)
  cells_[0].occupant = Player::Black;  // (0,0)
  cells_[1].occupant = Player::Black;  // (1,0)
  cells_[2].occupant = Player::Black;  // (2,0)
  cells_[3].occupant = Player::Black;  // (3,0)
  cells_[4].occupant = Player::Black;  // (4,0)
  
  cells_[20].occupant = Player::White; // (0,4)
  cells_[21].occupant = Player::White; // (1,4)
  cells_[22].occupant = Player::White; // (2,4)
  cells_[23].occupant = Player::White; // (3,4)
  cells_[24].occupant = Player::White; // (4,4)
}
