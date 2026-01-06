#pragma once
#include <cstdint>

namespace contrast {

enum class Player : uint8_t { None = 0, Black = 1, White = 2 };

enum class Piece : uint8_t { Empty = 0, Pawn = 1 };

enum class TileType : uint8_t { None = 0, Black = 1, Gray = 2 };

using Score = int;

constexpr int BOARD_W = 5;
constexpr int BOARD_H = 5;

} // namespace contrast
