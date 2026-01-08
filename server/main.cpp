#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>
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
    if (port < 1 || port > 65535) return fallback;
    return port;
  } catch (...) {
    return fallback;
  }
}

int resolve_server_port(int argc, char** argv) {
  // Priority: --port / --port=... > env CONTRAST_SERVER_PORT > default
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
  std::string role{"spectator"}; // "X" / "O" / "spectator"
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

// game_id is authoritative server-side.
uint64_t g_game_id = 1;

// last accepted move_id per role in the current game (for idempotency)
std::map<std::string, uint64_t> g_last_received_move_id{{"X", 0}, {"O", 0}};

bool should_log_board() {
  const char* flag = std::getenv("CONTRAST_SERVER_LOG_BOARD");
  return flag && std::string(flag) == "1";
}

char player_to_symbol(contrast::Player player) {
  switch (player) {
    case contrast::Player::Black: return 'X';
    case contrast::Player::White: return 'O';
    default: return '?';
  }
}

contrast::Player role_to_player(const std::string& role) {
  if (role == "X") return contrast::Player::Black;
  if (role == "O") return contrast::Player::White;
  return contrast::Player::None;
}

std::pair<int,int> coord_to_xy(const std::string& coord) {
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

contrast::TileType tile_from_char(char c) {
  if (c == 'b') return contrast::TileType::Black;
  if (c == 'g') return contrast::TileType::Gray;
  return contrast::TileType::None;
}

char tile_to_char(contrast::TileType tile) {
  switch (tile) {
    case contrast::TileType::Black: return 'b';
    case contrast::TileType::Gray:  return 'g';
    default: return '-';
  }
}

contrast::Move convert_move(const protocol::Move& move) {
  auto [sx, sy] = coord_to_xy(move.origin);
  auto [dx, dy] = coord_to_xy(move.target);
  contrast::Move m;
  m.sx = sx; m.sy = sy; m.dx = dx; m.dy = dy;
  if (!move.tile.skip) {
    auto [tx, ty] = coord_to_xy(move.tile.coord);
    m.place_tile = true;
    m.tx = tx; m.ty = ty;
    m.tile = tile_from_char(move.tile.color);
  }
  return m;
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
  if (a.sx != b.sx || a.sy != b.sy || a.dx != b.dx || a.dy != b.dy) return false;
  if (a.place_tile != b.place_tile) return false;
  if (!a.place_tile) return true;
  return a.tx == b.tx && a.ty == b.ty && a.tile == b.tile;
}

protocol::StateSnapshot build_snapshot_locked() {
  // g_game_mutex must be held.
  protocol::StateSnapshot snapshot;
  const contrast::Board& board = g_state.board();
  for (int y = 0; y < board.height(); ++y) {
    for (int x = 0; x < board.width(); ++x) {
      const auto& cell = board.at(x, y);
      const std::string coord = xy_to_coord(x, y);
      if (cell.occupant != contrast::Player::None) snapshot.pieces[coord] = player_to_symbol(cell.occupant);
      if (cell.tile != contrast::TileType::None) snapshot.tiles[coord] = tile_to_char(cell.tile);
    }
  }
  snapshot.turn = player_to_symbol(g_state.current_player());
  snapshot.status = g_status;
  snapshot.last_move = g_last_move;

  const auto& invX = g_state.inventory(contrast::Player::Black);
  const auto& invO = g_state.inventory(contrast::Player::White);
  snapshot.stock_black = std::map<char,int>{{'X', invX.black}, {'O', invO.black}};
  snapshot.stock_gray  = std::map<char,int>{{'X', invX.gray},  {'O', invO.gray}};

  snapshot.game_id = g_game_id;
  return snapshot;
}

void update_status_locked(contrast::Player last_player) {
  // g_game_mutex must be held.
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

void reset_game_locked(bool clear_ready_flags) {
  // g_game_mutex must be held.
  g_state = contrast::GameState();
  g_last_move.clear();
  g_status = "ongoing";
  ++g_game_id;

  g_last_received_move_id["X"] = 0;
  g_last_received_move_id["O"] = 0;

  if (clear_ready_flags) {
    std::lock_guard<std::mutex> lock(g_clients_mutex);
    for (auto& c : g_clients) c->ready = false;
  }
}

void log_line(const std::string& s) {
  std::cerr << s << std::endl;
  if (g_log_file.is_open()) {
    g_log_file << s << std::endl;
    g_log_file.flush();
  }
}

void log_snapshot_details(const protocol::StateSnapshot& snapshot, const std::string& prefix) {
  std::ostringstream oss;
  oss << prefix
      << " game_id=" << snapshot.game_id
      << " turn=" << snapshot.turn
      << " status=" << snapshot.status
      << " last_move=" << snapshot.last_move;

  oss << " pieces:";
  for (const auto& p : snapshot.pieces) oss << ' ' << p.first << ':' << p.second;
  oss << " tiles:";
  for (const auto& t : snapshot.tiles) oss << ' ' << t.first << ':' << t.second;
  oss << " stock_b:";
  for (const auto& sb : snapshot.stock_black) oss << ' ' << sb.first << ':' << sb.second;
  oss << " stock_g:";
  for (const auto& sg : snapshot.stock_gray) oss << ' ' << sg.first << ':' << sg.second;

  log_line(oss.str());
}

void record_game_result(const std::string& winner) {
  std::lock_guard<std::mutex> lock(g_clients_mutex);
  g_stats.total_games++;
  if (winner == "X") g_stats.x_wins++;
  else if (winner == "O") g_stats.o_wins++;
  else g_stats.draws++;

  for (const auto& client : g_clients) {
    if (!client->active) continue;
    if (client->role == "X") g_stats.x_player_name = client->name;
    if (client->role == "O") g_stats.o_player_name = client->name;
  }

  if (g_log_file.is_open()) {
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    g_log_file << "Game " << g_stats.total_games << " | Winner: " << winner
               << " | X(" << g_stats.x_player_name << ") vs O(" << g_stats.o_player_name << ")"
               << " | Time: " << std::ctime(&t);
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
  bool has_x=false, has_o=false;
  bool x_ready=false, o_ready=false;
  for (const auto& c : g_clients) {
    if (!c->active) continue;
    if (c->role == "X") { has_x = true; x_ready = c->ready; }
    if (c->role == "O") { has_o = true; o_ready = c->ready; }
  }
  return has_x && has_o && x_ready && o_ready;
}

bool both_players_multi_game() {
  std::lock_guard<std::mutex> lock(g_clients_mutex);
  bool x=false, o=false;
  for (const auto& c : g_clients) {
    if (!c->active) continue;
    if (c->role == "X") x = c->multi_game;
    if (c->role == "O") o = c->multi_game;
  }
  return x && o;
}

void send_all(int socket, const std::string& payload) {
  const char* data = payload.data();
  ssize_t remaining = static_cast<ssize_t>(payload.size());
  if (std::getenv("CONTRAST_DEBUG")) {
    std::string out = payload;
    if (out.size() > 400) out = out.substr(0,400) + "...";
    std::cerr << "[NET OUT] " << out << std::endl;
  }
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
    const auto pos = buffer.find('\n');
    if (pos != std::string::npos) {
      std::string line = buffer.substr(0, pos);
      buffer.erase(0, pos + 1);
      if (std::getenv("CONTRAST_DEBUG")) std::cerr << "[NET IN] " << line << std::endl;
      return line;
    }
    char chunk[512];
    const ssize_t received = ::recv(socket, chunk, sizeof(chunk), 0);
    if (received == 0) {
      if (buffer.empty()) return std::nullopt;
      std::string line = buffer;
      buffer.clear();
      return line;
    }
    if (received < 0) {
      if (errno == EINTR) continue;
      return std::nullopt;
    }
    buffer.append(chunk, received);
  }
}

void send_state_to(int socket, const protocol::StateSnapshot& snapshot) {
  const std::string msg = protocol::build_state_message(snapshot);
  if (std::getenv("CONTRAST_DEBUG")) {
    std::cerr << "[STATE SEND -> sock=" << socket << "] game_id=" << snapshot.game_id
              << " last_move='" << snapshot.last_move << "'" << std::endl;
  }
  send_all(socket, msg);
}

void broadcast_state(const protocol::StateSnapshot& snapshot) {
  const std::string msg = protocol::build_state_message(snapshot);
  std::lock_guard<std::mutex> lock(g_clients_mutex);
  for (auto it = g_clients.begin(); it != g_clients.end();) {
    auto s = *it;
    if (!s->active) { it = g_clients.erase(it); continue; }
    try {
      if (std::getenv("CONTRAST_DEBUG")) {
        std::cerr << "[STATE BROADCAST] to role=" << s->role << " name=" << s->name
                  << " sock=" << s->socket << " game_id=" << snapshot.game_id
                  << " last_move='" << snapshot.last_move << "'" << std::endl;
      }
      send_all(s->socket, msg);
      ++it;
    } catch (...) {
      s->active = false;
      ::close(s->socket);
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
  bool hasX=false, hasO=false;
  for (const auto& c : g_clients) {
    if (!c->active) continue;
    if (c->role == "X") hasX = true;
    if (c->role == "O") hasO = true;
  }
  if (!hasX) return "X";
  if (!hasO) return "O";
  return "spectator";
}

bool role_in_use_locked(const std::string& role, const std::shared_ptr<ClientSession>& requester) {
  if (role != "X" && role != "O") return false;
  for (const auto& c : g_clients) {
    if (!c->active || c == requester) continue;
    if (c->role == role) return true;
  }
  return false;
}

void remove_client(const std::shared_ptr<ClientSession>& session) {
  std::lock_guard<std::mutex> lock(g_clients_mutex);
  for (auto it = g_clients.begin(); it != g_clients.end(); ++it) {
    if (*it == session) { g_clients.erase(it); break; }
  }
}

void send_authoritative_state_to_session(const std::shared_ptr<ClientSession>& session,
                                        const std::string& tag_for_log) {
  protocol::StateSnapshot snap;
  {
    std::lock_guard<std::mutex> lock(g_game_mutex);
    snap = build_snapshot_locked();
  }
  log_snapshot_details(snap, tag_for_log);
  try {
    // IMPORTANT: direct send first (self-heal)
    send_state_to(session->socket, snap);
    // optional broadcast for spectators / other client sync
    broadcast_state(snap);
  } catch (...) {
    // ignore
  }
}

void handle_move(const std::shared_ptr<ClientSession>& session, const std::string& payload) {
  if (session->role != "X" && session->role != "O") {
    send_error(session->socket, "Spectators cannot submit moves");
    return;
  }

  protocol::Move move;
  try {
    move = protocol::parse_move(payload);
  } catch (const std::exception& ex) {
    send_error(session->socket, ex.what());
    return;
  }

  const contrast::Player player = role_to_player(session->role);
  if (player == contrast::Player::None) {
    send_error(session->socket, "Unknown player role");
    return;
  }

  // log recv
  {
    contrast::Move core = convert_move(move);
    std::ostringstream oss;
    oss << "[RECV_MOVE] from " << session->role << "(" << session->name << "): proto=\""
        << protocol::format_move(move) << "\" core=\"" << format_core_move(core) << "\"";
    log_line(oss.str());
  }

  // game_id check (authoritative)
  {
    std::lock_guard<std::mutex> lock(g_game_mutex);
    if (move.game_id != 0 && move.game_id != g_game_id) {
      // stale: resync (critical!)
      send_error(session->socket, "Stale or mismatched game_id; resyncing state");
    } else {
      // ok
      goto GAME_ID_OK;
    }
  }
  send_authoritative_state_to_session(session, "[STALE_GAME_ID_REJECT]");
  return;

GAME_ID_OK:

  // main game logic
  protocol::StateSnapshot snapshot;
  bool game_ended = false;

  {
    std::lock_guard<std::mutex> lock(g_game_mutex);

    if (session->role != std::string(1, player_to_symbol(g_state.current_player()))) {
      send_error(session->socket,
                 std::string("It is ") + player_to_symbol(g_state.current_player()) + "'s turn");
      // send state to help client recover
      // (do it outside lock)
      snapshot = build_snapshot_locked();
      // fallthrough
      goto SEND_TURN_STATE;
    }

    // move_id idempotency
    if (move.move_id != 0) {
      const uint64_t last = g_last_received_move_id[session->role];
      if (move.move_id <= last) {
        send_error(session->socket, "Duplicate or old move_id; resyncing state");
        snapshot = build_snapshot_locked();
        goto SEND_DUP_STATE;
      }
    }

    // legality
    contrast::Move desired = convert_move(move);
    contrast::MoveList legal;
    contrast::Rules::legal_moves(g_state, legal);

    auto it = std::find_if(legal.begin(), legal.end(),
                           [&](const contrast::Move& cand){ return moves_equal(cand, desired); });

    if (it == legal.end()) {
      // best-effort reason
      std::string reason = "Move not present in generated legal moves";
      const contrast::Board& board = g_state.board();
      if (!board.in_bounds(desired.sx, desired.sy) || !board.in_bounds(desired.dx, desired.dy)) {
        reason = "Origin or target coordinate out of bounds";
      } else if (board.at(desired.sx, desired.sy).occupant != player) {
        const auto occ = board.at(desired.sx, desired.sy).occupant;
        reason = std::string("Origin does not contain player's piece (has ")
                 + (occ == contrast::Player::None ? "none" : std::string(1, player_to_symbol(occ))) + ")";
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
          if (desired.tile == contrast::TileType::Black && inv.black <= 0) reason = "No black tiles available in inventory";
          if (desired.tile == contrast::TileType::Gray  && inv.gray  <= 0) reason = "No gray tiles available in inventory";
        }
      }

      // detailed log
      std::ostringstream oss;
      oss << "[ILLEGAL_MOVE] from " << session->role << "(" << session->name << "): "
          << protocol::format_move(move) << " reason=" << reason << " legal:";
      for (const auto& lm : legal) oss << ' ' << format_core_move(lm);
      log_line(oss.str());

      send_error(session->socket, std::string("Illegal move: ") + reason + "; resyncing state");
      snapshot = build_snapshot_locked();
      goto SEND_ILLEGAL_STATE;
    }

    // apply
    g_state.apply_move(*it);
    g_last_move = protocol::format_move(move);

    if (move.move_id != 0) g_last_received_move_id[session->role] = move.move_id;

    update_status_locked(player);
    snapshot = build_snapshot_locked();

    if (g_status != "ongoing") {
      std::string winner;
      if (g_status == "X_win") winner = "X";
      else if (g_status == "O_win") winner = "O";
      else if (g_status == "draw") winner = "Draw";
      if (!winner.empty()) record_game_result(winner);
      game_ended = true;
    }
  } // unlock g_game_mutex

  if (should_log_board()) {
    std::cout << "\n" << protocol::render_board(snapshot.pieces, snapshot.tiles) << "\n";
  }
  broadcast_state(snapshot);

  // multi-game auto start
  if (game_ended && both_players_multi_game()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    protocol::StateSnapshot next;
    {
      std::lock_guard<std::mutex> lock(g_game_mutex);
      // IMPORTANT: in multi-game, do NOT clear ready flags; continuous match.
      reset_game_locked(/*clear_ready_flags=*/false);
      next = build_snapshot_locked();
    }
    log_snapshot_details(next, "[AUTO_RESET_BROADCAST]");
    broadcast_state(next);
  }
  return;

SEND_TURN_STATE:
  // outside game mutex
  log_snapshot_details(snapshot, "[TURN_MISMATCH_RESYNC]");
  try { send_state_to(session->socket, snapshot); } catch (...) {}
  return;

SEND_DUP_STATE:
  log_snapshot_details(snapshot, "[DUPLICATE_OR_OLD_MOVE]");
  try { send_state_to(session->socket, snapshot); } catch (...) {}
  broadcast_state(snapshot);
  return;

SEND_ILLEGAL_STATE:
  log_snapshot_details(snapshot, "[ILLEGAL_MOVE_SNAPSHOT]");
  try { send_state_to(session->socket, snapshot); } catch (...) {}
  broadcast_state(snapshot);
  return;
}

void handle_role(const std::shared_ptr<ClientSession>& session, const std::string& payload) {
  std::istringstream iss(payload);
  std::string role_token, name_token, model_token, multi_token;

  if (!(iss >> role_token)) {
    send_error(session->socket, "ROLE requires a target role");
    return;
  }
  if (!(iss >> name_token)) name_token = "-";
  if (iss >> model_token) {
    // ignore model
    if (iss >> multi_token) {
      if (multi_token == "multi" || multi_token == "連戦" || multi_token == "multi_game") {
        session->multi_game = true;
      }
    }
  }

  std::string normalized = role_token;
  for (char& ch : normalized) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));

  std::string requested_role;
  if (normalized == "-") requested_role.clear();
  else if (normalized == "X" || normalized == "O") requested_role = normalized;
  else if (normalized == "SPECTATOR" || normalized == "SPEC") requested_role = "spectator";
  else { send_error(session->socket, "Unknown role: " + role_token); return; }

  if (requested_role.empty()) requested_role = session->role;

  {
    std::lock_guard<std::mutex> lock(g_clients_mutex);
    if (role_in_use_locked(requested_role, session)) {
      send_error(session->socket, requested_role + " already taken");
      return;
    }
    session->role = requested_role;
    if (name_token != "-") session->name = name_token;
    if (multi_token.empty()) session->multi_game = false;
  }

  send_info(session->socket, "You are " + session->role + " (" + session->name + ")");

  // always send current state after ROLE
  protocol::StateSnapshot snap;
  {
    std::lock_guard<std::mutex> lock(g_game_mutex);
    snap = build_snapshot_locked();
  }
  try { send_state_to(session->socket, snap); } catch (...) {}
}

void handle_ready(const std::shared_ptr<ClientSession>& session) {
  if (session->role != "X" && session->role != "O") {
    send_error(session->socket, "Spectators cannot ready up");
    return;
  }
  {
    std::lock_guard<std::mutex> lock(g_clients_mutex);
    session->ready = true;
  }
  send_info(session->socket, "Ready acknowledged");

  if (!all_players_ready()) return;

  std::cout << "Both players ready, starting new game..." << std::endl;

  protocol::StateSnapshot snap;
  {
    std::lock_guard<std::mutex> lock(g_game_mutex);
    // READY starts a fresh match: clear ready flags so next READY is required unless multi-game.
    reset_game_locked(/*clear_ready_flags=*/true);
    snap = build_snapshot_locked();
  }
  log_snapshot_details(snap, "[NEW_GAME_BROADCAST]");
  broadcast_state(snap);
}

void client_thread(std::shared_ptr<ClientSession> session) {
  std::string buffer;
  try {
    while (true) {
      auto line = recv_line(session->socket, buffer);
      if (!line) break;
      if (line->empty()) continue;

      if (line->rfind("MOVE ", 0) == 0) {
        handle_move(session, line->substr(5));
      } else if (line->rfind("ROLE ", 0) == 0) {
        handle_role(session, line->substr(5));
      } else if (*line == "READY") {
        handle_ready(session);
      } else if (*line == "GET_STATE") {
        protocol::StateSnapshot snap;
        {
          std::lock_guard<std::mutex> lock(g_game_mutex);
          snap = build_snapshot_locked();
        }
        send_state_to(session->socket, snap);
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
  std::cout << "Client disconnected (" << session->role << ", " << session->name << ")\n";

  // If no active players remain, reset to a clean state (without bumping game_id too aggressively).
  bool no_players = false;
  {
    std::lock_guard<std::mutex> lock(g_clients_mutex);
    bool hasX=false, hasO=false;
    for (const auto& c : g_clients) {
      if (!c->active) continue;
      if (c->role == "X") hasX = true;
      if (c->role == "O") hasO = true;
    }
    no_players = (!hasX && !hasO);
  }
  if (no_players) {
    std::lock_guard<std::mutex> lock(g_game_mutex);
    // soft reset: keep game_id, just clear state
    g_state = contrast::GameState();
    g_last_move.clear();
    g_status = "ongoing";
    g_last_received_move_id["X"] = 0;
    g_last_received_move_id["O"] = 0;
  }
}

int create_server_socket(int port) {
  const int sock = ::socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0) throw std::runtime_error("socket() failed");

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
      if (errno == EINTR) continue;
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

} // namespace

int main(int argc, char** argv) {
  std::signal(SIGPIPE, SIG_IGN);

  g_log_file.open("game_results.log", std::ios::app);
  if (!g_log_file.is_open()) {
    std::cerr << "Warning: Could not open game_results.log for writing\n";
  } else {
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    g_log_file << "\n=== New session started at " << std::ctime(&t) << "===\n";
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
    if (g_log_file.is_open()) g_log_file.close();
    return 1;
  }

  if (g_log_file.is_open()) g_log_file.close();
  return 0;
}
