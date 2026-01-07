#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <csignal>
#include <cstring>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "../common/protocol.hpp"
#include "contrast/game_state.hpp"
#include "contrast/move_list.hpp"
#include "contrast/rules.hpp"
#include "contrast/types.hpp"

namespace {

constexpr int kDefaultServerPort = 8765;
constexpr int kBacklog = 8;

int parse_port_string(const std::string& value, int fallback) {
    try {
        const int port = std::stoi(value);
        if (port < 1 || port > 65535) {
            return fallback;
        }
        return port;
    } catch (...) {
        return fallback;
    }
}

int resolve_server_port(int argc, char** argv) {
    // Priority: CLI flag --port / --port=... > env CONTRAST_SERVER_PORT > default
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc) {
            return parse_port_string(argv[i + 1], kDefaultServerPort);
        }
        const std::string prefix = "--port=";
        if (arg.rfind(prefix, 0) == 0) {
            return parse_port_string(arg.substr(prefix.size()), kDefaultServerPort);
        }
    }
    if (const char* env = std::getenv("CONTRAST_SERVER_PORT")) {
        return parse_port_string(env, kDefaultServerPort);
    }
    return kDefaultServerPort;
}

struct GameStats {
    int total_games{0};
    int x_wins{0};
    int o_wins{0};
    int draws{0};
    std::string x_player_name;
    std::string o_player_name;
};

struct ClientSession {
    int socket{-1};
    std::string role{"spectator"};
    std::string name{"anon"};
    bool active{true};
    bool ready{false};
    bool multi_game{false}; // 連戦モード
};

std::mutex g_clients_mutex;
std::vector<std::shared_ptr<ClientSession>> g_clients;
std::mutex g_game_mutex;
contrast::GameState g_state;
std::string g_last_move;
std::string g_status = "ongoing";
GameStats g_stats;
std::ofstream g_log_file;
uint64_t g_game_id = 1;
// Track last accepted move_id per role to ignore duplicates/old moves
std::map<std::string, uint64_t> g_last_received_move_id{{"X", 0}, {"O", 0}};

bool should_log_board() {
    const char* flag = std::getenv("CONTRAST_SERVER_LOG_BOARD");
    return flag && std::string(flag) == "1";
}

char player_to_symbol(contrast::Player player) {
    switch (player) {
        case contrast::Player::Black:
            return 'X';
        case contrast::Player::White:
            return 'O';
        default:
            return '?';
    }
}

contrast::Player role_to_player(const std::string& role) {
    if (role == "X") {
        return contrast::Player::Black;
    }
    if (role == "O") {
        return contrast::Player::White;
    }
    return contrast::Player::None;
}

std::pair<int, int> coord_to_xy(const std::string& coord) {
    const int x = coord[0] - 'a';
    const int rank_index = coord[1] - '1';
    const int y = contrast::BOARD_H - 1 - rank_index;
    return {x, y};
}

std::string xy_to_coord(int x, int y) {
    const char file = static_cast<char>('a' + x);
    const char rank = static_cast<char>('1' + (contrast::BOARD_H - 1 - y));
    return std::string{file, rank};
}

contrast::TileType tile_from_char(char color) {
    if (color == 'b') {
        return contrast::TileType::Black;
    }
    if (color == 'g') {
        return contrast::TileType::Gray;
    }
    return contrast::TileType::None;
}

char tile_to_char(contrast::TileType tile) {
    switch (tile) {
        case contrast::TileType::Black:
            return 'b';
        case contrast::TileType::Gray:
            return 'g';
        default:
            return '-';
    }
}

contrast::Move convert_move(const protocol::Move& move) {
    auto [sx, sy] = coord_to_xy(move.origin);
    auto [dx, dy] = coord_to_xy(move.target);
    contrast::Move core_move;
    core_move.sx = sx;
    core_move.sy = sy;
    core_move.dx = dx;
    core_move.dy = dy;
    if (!move.tile.skip) {
        auto [tx, ty] = coord_to_xy(move.tile.coord);
        core_move.place_tile = true;
        core_move.tx = tx;
        core_move.ty = ty;
        core_move.tile = tile_from_char(move.tile.color);
    }
    return core_move;
}

std::string format_core_move(const contrast::Move& m) {
    std::string origin = xy_to_coord(m.sx, m.sy);
    std::string target = xy_to_coord(m.dx, m.dy);
    std::string tile_str = "-1";
    if (m.place_tile) {
        tile_str = xy_to_coord(m.tx, m.ty) + tile_to_char(m.tile);
    }
    return origin + "," + target + " " + tile_str;
}

bool moves_equal(const contrast::Move& a, const contrast::Move& b) {
    if (a.sx != b.sx || a.sy != b.sy || a.dx != b.dx || a.dy != b.dy) {
        return false;
    }
    if (a.place_tile != b.place_tile) {
        return false;
    }
    if (!a.place_tile) {
        return true;
    }
    return a.tx == b.tx && a.ty == b.ty && a.tile == b.tile;
}

protocol::StateSnapshot build_snapshot() {
    protocol::StateSnapshot snapshot;
    const contrast::Board& board = g_state.board();
    for (int y = 0; y < board.height(); ++y) {
        for (int x = 0; x < board.width(); ++x) {
            const auto& cell = board.at(x, y);
            const std::string coord = xy_to_coord(x, y);
            if (cell.occupant != contrast::Player::None) {
                snapshot.pieces[coord] = player_to_symbol(cell.occupant);
            }
            if (cell.tile != contrast::TileType::None) {
                snapshot.tiles[coord] = tile_to_char(cell.tile);
            }
        }
    }
    snapshot.turn = player_to_symbol(g_state.current_player());
    snapshot.status = g_status;
    snapshot.last_move = g_last_move;
    const auto& inv_black = g_state.inventory(contrast::Player::Black);
    const auto& inv_white = g_state.inventory(contrast::Player::White);
    snapshot.stock_black = std::map<char,int>{{'X', inv_black.black}, {'O', inv_white.black}};
    snapshot.stock_gray = std::map<char,int>{{'X', inv_black.gray}, {'O', inv_white.gray}};
    snapshot.game_id = g_game_id;
    return snapshot;
}

void update_status(contrast::Player last_player) {
    if (contrast::Rules::is_win(g_state, last_player)) {
        g_status = std::string(1, player_to_symbol(last_player)) + "_win";
        return;
    }
    const contrast::Player opponent =
        (last_player == contrast::Player::Black) ? contrast::Player::White : contrast::Player::Black;
    if (contrast::Rules::is_loss(g_state, opponent)) {
        g_status = std::string(1, player_to_symbol(last_player)) + "_win";
        return;
    }
    if (contrast::Rules::is_draw(g_state)) {
        g_status = "draw";
        return;
    }
    g_status = "ongoing";
}

void reset_game() {
    g_state = contrast::GameState();
    g_last_move.clear();
    g_status = "ongoing";
    // Advance logical game id so clients sending old game_id are rejected
    ++g_game_id;
    // Reset last received move ids so new game starts fresh
    g_last_received_move_id["X"] = 0;
    g_last_received_move_id["O"] = 0;
    std::lock_guard<std::mutex> lock(g_clients_mutex);
    for (auto& client : g_clients) {
        client->ready = false;
    }
}

void log_snapshot_details(const protocol::StateSnapshot& snapshot, const std::string& prefix) {
    std::ostringstream oss;
    oss << prefix << " turn=" << snapshot.turn << " status=" << snapshot.status << " last_move=" << snapshot.last_move;
    oss << " pieces:";
    for (const auto& p : snapshot.pieces) {
        oss << ' ' << p.first << ':' << p.second;
    }
    oss << " tiles:";
    for (const auto& t : snapshot.tiles) {
        oss << ' ' << t.first << ':' << t.second;
    }
    oss << " stock_b:";
    for (const auto& sb : snapshot.stock_black) {
        oss << ' ' << sb.first << ':' << sb.second;
    }
    oss << " stock_g:";
    for (const auto& sg : snapshot.stock_gray) {
        oss << ' ' << sg.first << ':' << sg.second;
    }
    std::string s = oss.str();
    std::cerr << s << std::endl;
    if (g_log_file.is_open()) {
        g_log_file << s << std::endl;
        g_log_file.flush();
    }
}

void record_game_result(const std::string& winner) {
    std::lock_guard<std::mutex> lock(g_clients_mutex);
    g_stats.total_games++;
    if (winner == "X") {
        g_stats.x_wins++;
    } else if (winner == "O") {
        g_stats.o_wins++;
    } else {
        g_stats.draws++;
    }
    
    // Update player names
    for (const auto& client : g_clients) {
        if (client->role == "X") {
            g_stats.x_player_name = client->name;
        } else if (client->role == "O") {
            g_stats.o_player_name = client->name;
        }
    }
    
    // Log to file
    if (g_log_file.is_open()) {
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        g_log_file << "Game " << g_stats.total_games << " | Winner: " << winner 
                   << " | X(" << g_stats.x_player_name << ") vs O(" << g_stats.o_player_name << ")"
                   << " | Time: " << std::ctime(&time);
        g_log_file.flush();
    }
    
    std::cout << "\n=== Game " << g_stats.total_games << " finished ==="
              << "\nWinner: " << winner
              << "\nScore: X=" << g_stats.x_wins << " O=" << g_stats.o_wins << " Draw=" << g_stats.draws
              << " (" << g_stats.x_player_name << " vs " << g_stats.o_player_name << ")"
              << "\n" << std::endl;
}

bool all_players_ready() {
    std::lock_guard<std::mutex> lock(g_clients_mutex);
    bool has_x = false, has_o = false;
    bool x_ready = false, o_ready = false;
    
    for (const auto& client : g_clients) {
        if (!client->active) continue;
        if (client->role == "X") {
            has_x = true;
            x_ready = client->ready;
        } else if (client->role == "O") {
            has_o = true;
            o_ready = client->ready;
        }
    }
    
    return has_x && has_o && x_ready && o_ready;
}

void send_all(int socket, const std::string& payload) {
    const char* data = payload.data();
    ssize_t remaining = static_cast<ssize_t>(payload.size());
    while (remaining > 0) {
        const ssize_t sent = ::send(socket, data, remaining, 0);
        if (sent <= 0) {
            throw std::runtime_error("send failed: " + std::string(std::strerror(errno)));
        }
        remaining -= sent;
        data += sent;
    }
}

std::optional<std::string> recv_line(int socket, std::string& buffer) {
    for (;;) {
        const auto newline_pos = buffer.find('\n');
        if (newline_pos != std::string::npos) {
            std::string line = buffer.substr(0, newline_pos);
            buffer.erase(0, newline_pos + 1);
            return line;
        }
        char chunk[512];
        const ssize_t received = ::recv(socket, chunk, sizeof(chunk), 0);
        if (received == 0) {
            if (buffer.empty()) {
                return std::nullopt;
            }
            std::string line = buffer;
            buffer.clear();
            return line;
        }
        if (received < 0) {
            if (errno == EINTR) {
                continue;
            }
            return std::nullopt;
        }
        buffer.append(chunk, received);
    }
}

void send_state_to(int socket, const protocol::StateSnapshot& snapshot) {
    const std::string message = protocol::build_state_message(snapshot);
    send_all(socket, message);
}

void broadcast_state(const protocol::StateSnapshot& snapshot) {
    const std::string message = protocol::build_state_message(snapshot);
    std::lock_guard<std::mutex> lock(g_clients_mutex);
    for (auto it = g_clients.begin(); it != g_clients.end();) {
        auto session = *it;
        if (!session->active) {
            it = g_clients.erase(it);
            continue;
        }
        try {
            send_all(session->socket, message);
            ++it;
        } catch (...) {
            session->active = false;
            ::close(session->socket);
            it = g_clients.erase(it);
        }
    }
}

void send_info(int socket, const std::string& text) {
    send_all(socket, "INFO " + text + "\n");
}

void send_error(int socket, const std::string& text) {
    send_all(socket, "ERROR " + text + "\n");
}

std::string assign_role_locked() {
    bool has_O = false;
    bool has_X = false;
    for (const auto& client : g_clients) {
        if (!client->active) {
            continue;
        }
        if (client->role == "O") {
            has_O = true;
        } else if (client->role == "X") {
            has_X = true;
        }
    }
    if (!has_X) {
        return "X";
    }
    if (!has_O) {
        return "O";
    }
    return "spectator";
}

bool role_in_use_locked(const std::string& role, const std::shared_ptr<ClientSession>& requester) {
    if (role != "X" && role != "O") {
        return false;
    }
    for (const auto& client : g_clients) {
        if (!client->active || client == requester) {
            continue;
        }
        if (client->role == role) {
            return true;
        }
    }
    return false;
}

bool both_players_multi_game() {
    std::lock_guard<std::mutex> lock(g_clients_mutex);
    bool x_multi = false, o_multi = false;
    for (const auto& client : g_clients) {
        if (!client->active) continue;
        if (client->role == "X") x_multi = client->multi_game;
        if (client->role == "O") o_multi = client->multi_game;
    }
    return x_multi && o_multi;
}

void remove_client(const std::shared_ptr<ClientSession>& session) {
    std::lock_guard<std::mutex> lock(g_clients_mutex);
    for (auto it = g_clients.begin(); it != g_clients.end(); ++it) {
        if (*it == session) {
            g_clients.erase(it);
            break;
        }
    }
}

void handle_move(const std::shared_ptr<ClientSession>& session, const std::string& payload) {
    if (session->role != "O" && session->role != "X") {
        send_error(session->socket, "Spectators cannot submit moves");
        return;
    }
    const contrast::Player player = role_to_player(session->role);
    if (player == contrast::Player::None) {
        send_error(session->socket, "Unknown player role");
        return;
    }
    protocol::Move move;
    try {
        move = protocol::parse_move(payload);
    } catch (const std::exception& ex) {
        send_error(session->socket, ex.what());
        return;
    }
    // Convert to core move and log both protocol and core representations for debugging
    contrast::Move desired = convert_move(move);
    {
        std::ostringstream oss;
        oss << "[RECV_MOVE] from " << session->role << "(" << session->name << "): proto=\""
            << protocol::format_move(move) << "\" core=\"" << format_core_move(desired) << "\"";
        std::string s = oss.str();
        std::cerr << s << std::endl;
        if (g_log_file.is_open()) {
            g_log_file << s << std::endl;
            g_log_file.flush();
        }
    }
    // If client provided a game_id, verify it matches current server game id.
    // If mismatched, resend authoritative STATE so client can resync.
    {
        protocol::StateSnapshot cur_snapshot;
        {
            std::lock_guard<std::mutex> lock(g_game_mutex);
            if (move.game_id != 0 && move.game_id != g_game_id) {
                cur_snapshot = build_snapshot();
            }
        }
        if (cur_snapshot.game_id != 0) {
            log_snapshot_details(cur_snapshot, "[STALE_GAME_ID_REJECT]");
            send_error(session->socket, "Stale or mismatched game_id; resyncing state");
            try {
                broadcast_state(cur_snapshot);
            } catch (...) {
                std::cerr << "Failed to broadcast STATE for stale game_id to clients" << std::endl;
            }
            return;
        }
    }
    protocol::StateSnapshot snapshot;
    bool game_ended = false;
    {
        std::lock_guard<std::mutex> lock(g_game_mutex);
        if (player != g_state.current_player()) {
            send_error(session->socket, std::string("It is ") + player_to_symbol(g_state.current_player()) + "'s turn");
            return;
        }
        // If client provided a move_id, ignore duplicates or older move_ids for this role
            if (move.move_id != 0) {
                const uint64_t last = g_last_received_move_id[session->role];
                if (move.move_id <= last) {
                    // Duplicate or stale move_id: broadcast current authoritative state
                    protocol::StateSnapshot cur_snapshot = build_snapshot();
                    log_snapshot_details(cur_snapshot, "[DUPLICATE_OR_OLD_MOVE]");
                    send_error(session->socket, "Duplicate or old move_id; resyncing state");
                    try {
                        broadcast_state(cur_snapshot);
                    } catch (...) {
                        std::cerr << "Failed to broadcast STATE for duplicate/old move" << std::endl;
                    }
                    return;
                }
            }
        contrast::Move desired = convert_move(move);
        contrast::MoveList legal;
        contrast::Rules::legal_moves(g_state, legal);
        auto it = std::find_if(legal.begin(), legal.end(),
                               [&](const contrast::Move& candidate) { return moves_equal(candidate, desired); });
        if (it == legal.end()) {
            // Determine a human-readable reason why the move is illegal (best-effort)
            std::string reason;
            const contrast::Board& board = g_state.board();
            // Bounds checks
            if (!board.in_bounds(desired.sx, desired.sy) || !board.in_bounds(desired.dx, desired.dy)) {
                reason = "Origin or target coordinate out of bounds";
            } else if (board.at(desired.sx, desired.sy).occupant != player) {
                const auto occ = board.at(desired.sx, desired.sy).occupant;
                reason = std::string("Origin does not contain player's piece (has ") + (occ == contrast::Player::None ? "none" : std::string(1, player_to_symbol(occ))) + ")";
            } else if (board.at(desired.dx, desired.dy).occupant != contrast::Player::None) {
                const auto occ = board.at(desired.dx, desired.dy).occupant;
                reason = std::string("Destination occupied by ") + std::string(1, player_to_symbol(occ));
            } else if (desired.place_tile) {
                if (!board.in_bounds(desired.tx, desired.ty)) {
                    reason = "Tile placement coordinate out of bounds";
                } else if (board.at(desired.tx, desired.ty).tile != contrast::TileType::None) {
                    reason = std::string("Tile target ") + xy_to_coord(desired.tx, desired.ty) + " already has a tile";
                } else {
                    const auto inv = g_state.inventory(player);
                    if (desired.tile == contrast::TileType::Black && inv.black <= 0) {
                        reason = "No black tiles available in inventory";
                    } else if (desired.tile == contrast::TileType::Gray && inv.gray <= 0) {
                        reason = "No gray tiles available in inventory";
                    }
                }
            }

            if (reason.empty()) {
                reason = "Move not present in generated legal moves";
            }

            // Log detailed info about the illegal move, the reason, and available legal moves
            std::ostringstream oss;
            oss << "Illegal move received from " << session->role << "(" << session->name << "): "
                << protocol::format_move(move) << ". Reason: " << reason << ". Legal moves:";
            for (const auto& lm : legal) {
                oss << ' ' << format_core_move(lm);
            }
            std::string info = oss.str();
            std::cerr << info << std::endl;
            if (g_log_file.is_open()) {
                g_log_file << info << std::endl;
                g_log_file.flush();
            }

            // Also log the current snapshot for debugging and resend it to the sender
            protocol::StateSnapshot cur_snapshot = build_snapshot();
            log_snapshot_details(cur_snapshot, "[ILLEGAL_MOVE_SNAPSHOT]");
            // Notify client of illegal move and include the reason so client logs are actionable
            send_error(session->socket, std::string("Illegal move: ") + reason + "; resyncing state");
            try {
                broadcast_state(cur_snapshot);
            } catch (...) {
                std::cerr << "Failed to broadcast STATE after illegal move" << std::endl;
            }
            return;
        }
        g_state.apply_move(*it);
        g_last_move = protocol::format_move(move);
        // Mark this move_id as accepted for this role so duplicates are ignored later
        if (move.move_id != 0) {
            g_last_received_move_id[session->role] = move.move_id;
        }
        update_status(player);
        snapshot = build_snapshot();
        // Record result if game ended
        if (g_status != "ongoing") {
            std::string winner;
            if (g_status == "X_win") {
                winner = "X";
            } else if (g_status == "O_win") {
                winner = "O";
            } else if (g_status == "draw") {
                winner = "Draw";
            }
            if (!winner.empty()) {
                record_game_result(winner);
            }
            game_ended = true;
        }
    }
    if (should_log_board()) {
        std::cout << "\n" << protocol::render_board(snapshot.pieces, snapshot.tiles) << "\n";
    }
    broadcast_state(snapshot);

    // 連戦モードなら自動で次ゲームを開始
    if (game_ended && both_players_multi_game()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(300)); // クライアント側の受信猶予
        {
            std::lock_guard<std::mutex> lock(g_game_mutex);
            reset_game();
            protocol::StateSnapshot next_snapshot = build_snapshot();
            log_snapshot_details(next_snapshot, "[AUTO_RESET_BROADCAST]");
            broadcast_state(next_snapshot);
        }
    }
}

void handle_role(const std::shared_ptr<ClientSession>& session, const std::string& payload) {
    std::istringstream iss(payload);
    std::string role_token;
    std::string name_token;
    std::string model_token;
    std::string multi_token;
    if (!(iss >> role_token)) {
        send_error(session->socket, "ROLE requires a target role");
        return;
    }
    if (!(iss >> name_token)) {
        name_token = "-";
    }
    if (iss >> model_token) {
        // モデル名は現在サーバ側で利用しないが、クライアントとの整合性のため読み捨てておく
        (void)model_token;
        // さらにmulti_game指定があれば受け取る
        if (iss >> multi_token) {
            if (multi_token == "multi" || multi_token == "連戦" || multi_token == "multi_game") {
                session->multi_game = true;
            }
        }
    }
    std::string normalized = role_token;
    for (char& ch : normalized) {
        ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    }
    std::string requested_role;
    if (normalized == "-") {
        requested_role.clear();
    } else if (normalized == "X" || normalized == "O") {
        requested_role = normalized;
    } else if (normalized == "SPECTATOR" || normalized == "SPEC") {
        requested_role = "spectator";
    } else {
        send_error(session->socket, "Unknown role: " + role_token);
        return;
    }
    if (requested_role.empty()) {
        requested_role = session->role;
    }
    {
        std::lock_guard<std::mutex> lock(g_clients_mutex);
        if (role_in_use_locked(requested_role, session)) {
            send_error(session->socket, requested_role + " already taken");
            return;
        }
        session->role = requested_role;
        if (name_token != "-") {
            session->name = name_token;
        }
        // multi_gameはROLEコマンドで指定がなければfalseにリセット
        if (multi_token.empty()) {
            session->multi_game = false;
        }
    }
    // 両者がmulti_gameならtrue (ヘルパー関数はファイルレベルで定義されています)
    send_info(session->socket, "You are " + session->role + " (" + session->name + ")");
    // Send current game state after role assignment
    {
        std::lock_guard<std::mutex> lock(g_game_mutex);
        send_state_to(session->socket, build_snapshot());
    }
}

void handle_ready(const std::shared_ptr<ClientSession>& session) {
    if (session->role != "X" && session->role != "O") {
        send_error(session->socket, "Spectators cannot ready up");
        return;
    }

    // READY multi などでmulti_game指定を受け付ける
    if (!session->name.empty()) {
        // READYコマンドのpayloadをbufferから取得するにはclient_thread側の処理を修正する必要があるが、
        // ここでは簡易的にmulti_game指定はROLE推奨とする
    }
    {
        std::lock_guard<std::mutex> lock(g_clients_mutex);
        session->ready = true;
    }

    send_info(session->socket, "Ready acknowledged");

    // Check if all players are ready
    if (all_players_ready()) {
        std::cout << "Both players ready, starting new game..." << std::endl;
        {
            std::lock_guard<std::mutex> lock(g_game_mutex);
            reset_game();
        }

        // Broadcast new game state
        protocol::StateSnapshot snapshot;
        {
            std::lock_guard<std::mutex> lock(g_game_mutex);
            snapshot = build_snapshot();
        }
        log_snapshot_details(snapshot, "[NEW_GAME_BROADCAST]");
        broadcast_state(snapshot);
    }
}

void client_thread(std::shared_ptr<ClientSession> session) {
    std::string buffer;
    try {
        // Don't send initial info or state - wait for ROLE command
        // Client will send ROLE immediately after connection
        while (true) {
            auto line = recv_line(session->socket, buffer);
            if (!line) {
                break;
            }
            if (line->empty()) {
                continue;
            }
            if (line->rfind("MOVE ", 0) == 0) {
                handle_move(session, line->substr(5));
            } else if (line->rfind("ROLE ", 0) == 0) {
                handle_role(session, line->substr(5));
            } else if (*line == "READY") {
                handle_ready(session);
            } else if (*line == "GET_STATE") {
                std::lock_guard<std::mutex> lock(g_game_mutex);
                send_state_to(session->socket, build_snapshot());
            } else if (*line == "GET_STATS") {
                std::string stats_msg = "STATS games=" + std::to_string(g_stats.total_games) +
                                       " x_wins=" + std::to_string(g_stats.x_wins) +
                                       " o_wins=" + std::to_string(g_stats.o_wins) +
                                       " draws=" + std::to_string(g_stats.draws) + "\n";
                send_all(session->socket, stats_msg);
            } else {
                send_error(session->socket, "Unknown command: " + *line);
            }
        }
    } catch (const std::exception& ex) {
        std::cerr << "Client thread error: " << ex.what() << '\n';
    }
    session->active = false;
    ::close(session->socket);
    remove_client(session);
    std::cout << "Client disconnected (" << session->role << ", " << session->name << ")" << std::endl;

    // If all players have disconnected, reset the game so the next clients can start a fresh game
    // without requiring an explicit READY handshake.
    bool should_reset = false;
    {
        std::lock_guard<std::mutex> clients_lock(g_clients_mutex);
        bool has_x = false;
        bool has_o = false;
        for (const auto& client : g_clients) {
            if (!client->active) {
                continue;
            }
            if (client->role == "X") {
                has_x = true;
            } else if (client->role == "O") {
                has_o = true;
            }
        }

        if (!has_x && !has_o) {
            for (auto& client : g_clients) {
                client->ready = false;
            }
            should_reset = true;
        }
    }
    if (should_reset) {
        std::lock_guard<std::mutex> game_lock(g_game_mutex);
        g_state = contrast::GameState();
        g_last_move.clear();
        g_status = "ongoing";
    }
}

int create_server_socket(int port) {
    const int sock = ::socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        throw std::runtime_error("socket() failed");
    }
    int opt = 1;
    ::setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    if (::bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(sock);
        throw std::runtime_error("bind() failed");
    }
    if (::listen(sock, kBacklog) < 0) {
        ::close(sock);
        throw std::runtime_error("listen() failed");
    }
    return sock;
}

void accept_loop(int server_sock) {
    while (true) {
        sockaddr_in client_addr{};
        socklen_t len = sizeof(client_addr);
        const int client_sock = ::accept(server_sock, reinterpret_cast<sockaddr*>(&client_addr), &len);
        if (client_sock < 0) {
            if (errno == EINTR) {
                continue;
            }
            std::cerr << "accept() failed: " << std::strerror(errno) << '\n';
            continue;
        }
        auto session = std::make_shared<ClientSession>();
        session->socket = client_sock;
        {
            std::lock_guard<std::mutex> lock(g_clients_mutex);
            session->role = assign_role_locked();
            g_clients.push_back(session);
        }
        std::thread(client_thread, session).detach();
    }
}

}  // namespace

int main(int argc, char** argv) {
    std::signal(SIGPIPE, SIG_IGN);
    
    // Open log file
    g_log_file.open("game_results.log", std::ios::app);
    if (!g_log_file.is_open()) {
        std::cerr << "Warning: Could not open game_results.log for writing" << std::endl;
    } else {
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        g_log_file << "\n=== New session started at " << std::ctime(&time) << "===\n";
        g_log_file.flush();
    }
    
    try {
        const int port = resolve_server_port(argc, argv);
        const int server_sock = create_server_socket(port);
        std::cout << "Server listening on port " << port << std::endl;
        accept_loop(server_sock);
        ::close(server_sock);
    } catch (const std::exception& ex) {
        std::cerr << "Fatal server error: " << ex.what() << std::endl;
        if (g_log_file.is_open()) {
            g_log_file.close();
        }
        return 1;
    }
    
    if (g_log_file.is_open()) {
        g_log_file.close();
    }
    return 0;
}
