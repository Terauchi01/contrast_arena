#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace protocol {

inline constexpr std::array<char, 5> FILES{'a', 'b', 'c', 'd', 'e'};
inline constexpr std::array<char, 5> RANKS{'1', '2', '3', '4', '5'};
inline constexpr std::array<char, 2> TILE_COLORS{'b', 'g'};

struct ProtocolError : public std::runtime_error {
    using std::runtime_error::runtime_error;
};

inline std::string to_lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return text;
}

inline bool is_valid_coord(std::string_view coord) {
    if (coord.size() != 2) {
        return false;
    }
    return std::find(FILES.begin(), FILES.end(), coord[0]) != FILES.end() &&
           std::find(RANKS.begin(), RANKS.end(), coord[1]) != RANKS.end();
}

inline std::string normalize_coord(std::string coord) {
    coord = to_lower(coord);
    if (!is_valid_coord(coord)) {
        throw ProtocolError("Invalid board coordinate: " + coord);
    }
    return coord;
}

inline std::pair<int, int> coord_to_indices(const std::string& coord) {
    const auto file = std::find(FILES.begin(), FILES.end(), coord[0]);
    const auto rank = std::find(RANKS.begin(), RANKS.end(), coord[1]);
    if (file == FILES.end() || rank == RANKS.end()) {
        throw ProtocolError("Coordinate out of bounds: " + coord);
    }
    return {static_cast<int>(std::distance(FILES.begin(), file)),
            static_cast<int>(std::distance(RANKS.begin(), rank))};
}

inline int chebyshev_distance(const std::string& a, const std::string& b) {
    auto [ax, ay] = coord_to_indices(a);
    auto [bx, by] = coord_to_indices(b);
    const int dx = std::abs(ax - bx);
    const int dy = std::abs(ay - by);
    return std::max(dx, dy);
}

struct TilePlacement {
    bool skip{true};
    std::string coord{};
    char color{'b'};

    static TilePlacement none() { return TilePlacement{}; }
};

struct Move {
    std::string origin;
    std::string target;
    TilePlacement tile;
};

struct StateSnapshot {
    std::map<std::string, char> pieces;
    std::map<std::string, char> tiles;
    char turn{'X'};
    std::string status{"ongoing"};
    std::string last_move{};
    std::map<char, int> stock_black;
    std::map<char, int> stock_gray;
};

inline TilePlacement parse_tile(const std::string& text) {
    const std::string trimmed = to_lower(text);
    if (trimmed == "-1") {
        return TilePlacement::none();
    }
    if (trimmed.size() != 3) {
        throw ProtocolError("Tile descriptor must look like b3g or -1");
    }
    TilePlacement tile;
    tile.coord = normalize_coord(trimmed.substr(0, 2));
    tile.color = trimmed[2];
    if (std::find(TILE_COLORS.begin(), TILE_COLORS.end(), tile.color) == TILE_COLORS.end()) {
        throw ProtocolError("Unsupported tile color: " + std::string(1, tile.color));
    }
    tile.skip = false;
    return tile;
}

inline Move parse_move(const std::string& text) {
    auto trimmed = text;
    trimmed.erase(trimmed.begin(), std::find_if(trimmed.begin(), trimmed.end(), [](int ch) {
        return !std::isspace(ch);
    }));
    trimmed.erase(std::find_if(trimmed.rbegin(), trimmed.rend(), [](int ch) {
                      return !std::isspace(ch);
                  }).base(),
                  trimmed.end());
    const auto space_pos = trimmed.find(' ');
    if (space_pos == std::string::npos) {
        throw ProtocolError("Move must include space separating to/tile segments");
    }
    const std::string displacement = trimmed.substr(0, space_pos);
    const std::string tile_part = trimmed.substr(space_pos + 1);
    const auto comma_pos = displacement.find(',');
    if (comma_pos == std::string::npos || displacement.find(',', comma_pos + 1) != std::string::npos) {
        throw ProtocolError("Move must include exactly one comma between origin and target");
    }
    Move move;
    move.origin = normalize_coord(displacement.substr(0, comma_pos));
    move.target = normalize_coord(displacement.substr(comma_pos + 1));
    move.tile = parse_tile(tile_part);
    return move;
}

inline std::string format_move(const Move& move) {
    std::ostringstream oss;
    oss << move.origin << ',' << move.target << ' ';
    if (move.tile.skip) {
        oss << "-1";
    } else {
        oss << move.tile.coord << move.tile.color;
    }
    return oss.str();
}

inline std::string render_board(const std::map<std::string, char>& pieces,
                                const std::map<std::string, char>& tiles) {
    std::ostringstream oss;
    for (auto rank_it = RANKS.rbegin(); rank_it != RANKS.rend(); ++rank_it) {
        oss << *rank_it << '|';
        for (char file : FILES) {
            const std::string coord{file, *rank_it};
            auto piece_it = pieces.find(coord);
            if (piece_it != pieces.end()) {
                oss << ' ' << piece_it->second << ' ';
                continue;
            }
            auto tile_it = tiles.find(coord);
            if (tile_it != tiles.end()) {
                const char color = static_cast<char>(std::tolower(tile_it->second));
                if (color == 'b') {
                    oss << " []";
                } else if (color == 'g') {
                    oss << " ()";
                } else {
                    oss << " [" << static_cast<char>(std::toupper(color)) << ']';
                }
                continue;
            }
            oss << "  .";
        }
        oss << " |\n";
    }
    oss << "   ";
    for (char file : FILES) {
        oss << ' ' << file << ' ';
    }
    return oss.str();
}

inline std::vector<std::string> split(const std::string& text, char delim) {
    std::vector<std::string> parts;
    std::stringstream ss(text);
    std::string item;
    while (std::getline(ss, item, delim)) {
        if (!item.empty()) {
            parts.push_back(item);
        }
    }
    return parts;
}

inline std::string join_entries(const std::map<std::string, char>& data) {
    std::ostringstream oss;
    bool first = true;
    for (const auto& [coord, value] : data) {
        if (!first) {
            oss << ',';
        }
        first = false;
        oss << coord << ':' << value;
    }
    return oss.str();
}

inline std::string join_counts(const std::map<char, int>& data) {
    std::ostringstream oss;
    bool first = true;
    for (const auto& [player, value] : data) {
        if (!first) {
            oss << ',';
        }
        first = false;
        oss << player << ':' << value;
    }
    return oss.str();
}

inline std::map<std::string, char> parse_entries(const std::string& text) {
    std::map<std::string, char> data;
    if (text.empty()) {
        return data;
    }
    const auto items = split(text, ',');
    for (const auto& item : items) {
        const auto colon = item.find(':');
        if (colon == std::string::npos || colon == item.size() - 1) {
            throw ProtocolError("Malformed entry in STATE payload: " + item);
        }
        const std::string coord = normalize_coord(item.substr(0, colon));
        const char value = item[colon + 1];
        data.emplace(coord, value);
    }
    return data;
}

inline std::map<char, int> parse_counts(const std::string& text) {
    std::map<char, int> data;
    if (text.empty()) {
        return data;
    }
    const auto items = split(text, ',');
    for (const auto& item : items) {
        const auto colon = item.find(':');
        if (colon == std::string::npos || colon == item.size() - 1) {
            throw ProtocolError("Malformed inventory entry: " + item);
        }
        const char player = item[0];
        const int value = std::stoi(item.substr(colon + 1));
        data[player] = value;
    }
    return data;
}

inline std::string build_state_message(const StateSnapshot& snapshot) {
    std::ostringstream oss;
    oss << "STATE\n";
    oss << "turn=" << snapshot.turn << "\n";
    oss << "status=" << snapshot.status << "\n";
    oss << "last=" << snapshot.last_move << "\n";
    oss << "pieces=" << join_entries(snapshot.pieces) << "\n";
    oss << "tiles=" << join_entries(snapshot.tiles) << "\n";
    oss << "stock_b=" << join_counts(snapshot.stock_black) << "\n";
    oss << "stock_g=" << join_counts(snapshot.stock_gray) << "\n";
    oss << "END\n";
    return oss.str();
}

inline StateSnapshot parse_state_block(const std::vector<std::string>& lines) {
    StateSnapshot snapshot;
    for (const auto& line : lines) {
        const auto eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        const std::string key = line.substr(0, eq);
        const std::string value = line.substr(eq + 1);
        if (key == "turn" && !value.empty()) {
            snapshot.turn = value.front();
        } else if (key == "status") {
            snapshot.status = value;
        } else if (key == "last") {
            snapshot.last_move = value;
        } else if (key == "pieces") {
            snapshot.pieces = parse_entries(value);
        } else if (key == "tiles") {
            snapshot.tiles = parse_entries(value);
        } else if (key == "stock_b") {
            snapshot.stock_black = parse_counts(value);
        } else if (key == "stock_g") {
            snapshot.stock_gray = parse_counts(value);
        }
    }
    return snapshot;
}

}  // namespace protocol
