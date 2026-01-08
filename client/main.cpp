// main.cpp (simplified + fixed: no stall on STALE_GAME_ID_REJECT / ERROR resync)

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cctype>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <functional>
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
#include <cstdint>

#include "../common/protocol.hpp"
#include "contrast/board.hpp"
#include "contrast/game_state.hpp"
#include "contrast/move.hpp"
#include "contrast/move_list.hpp"
#include "contrast/rules.hpp"
#include "contrast/types.hpp"

#include "random_policy.hpp"
#include "rule_based_policy.hpp"
#include "rule_based_policy2.hpp"
#include "ntuple_big.hpp"
#include "alphabeta.hpp"
#include "mcts.hpp"

namespace {

constexpr int kDefaultServerPort = 8765;
constexpr char kServerHost[] = "127.0.0.1";

inline bool minimal_mode() { return std::getenv("CONTRAST_MINIMAL") != nullptr; }
inline bool silent_mode()  { return std::getenv("CONTRAST_SILENT")  != nullptr; }

int parse_port_env(const char* value, int fallback) {
    if (!value) return fallback;
    try {
        const int port = std::stoi(value);
        if (port < 1 || port > 65535) return fallback;
        return port;
    } catch (...) {
        return fallback;
    }
}

int resolve_server_port() {
    return parse_port_env(std::getenv("CONTRAST_SERVER_PORT"), kDefaultServerPort);
}

std::optional<std::string> recv_line(int socket, std::string& buffer) {
    for (;;) {
        const auto nl = buffer.find('\n');
        if (nl != std::string::npos) {
            std::string line = buffer.substr(0, nl);
            buffer.erase(0, nl + 1);
            if (!silent_mode() && !minimal_mode()) {
                std::cerr << "[NET RECV] " << line << "\n";
            }
            return line;
        }
        char chunk[512];
        const ssize_t r = ::recv(socket, chunk, sizeof(chunk), 0);
        if (r == 0) {
            if (buffer.empty()) return std::nullopt;
            std::string line = buffer;
            buffer.clear();
            return line;
        }
        if (r < 0) {
            if (errno == EINTR) continue;
            return std::nullopt;
        }
        buffer.append(chunk, static_cast<size_t>(r));
    }
}

void send_all(int socket, const std::string& payload) {
    if (!silent_mode() && !minimal_mode()) {
        std::string out = payload;
        if (out.size() > 200) out = out.substr(0, 200) + "...";
        std::cerr << "[NET SEND] " << out << "\n";
    }
    const char* data = payload.data();
    ssize_t remaining = static_cast<ssize_t>(payload.size());
    while (remaining > 0) {
        const ssize_t sent = ::send(socket, data, remaining, 0);
        if (sent <= 0) throw std::runtime_error("send failed");
        remaining -= sent;
        data += sent;
    }
}

// ----- coord / tile / player helpers -----

contrast::Player symbol_to_player(char symbol) {
    const char upper = static_cast<char>(std::toupper(static_cast<unsigned char>(symbol)));
    if (upper == 'X') return contrast::Player::Black;
    if (upper == 'O') return contrast::Player::White;
    return contrast::Player::None;
}

char tile_to_char(contrast::TileType tile) {
    switch (tile) {
        case contrast::TileType::Black: return 'b';
        case contrast::TileType::Gray:  return 'g';
        default:                        return '-';
    }
}

contrast::TileType tile_from_char(char color) {
    const char lower = static_cast<char>(std::tolower(static_cast<unsigned char>(color)));
    if (lower == 'b') return contrast::TileType::Black;
    if (lower == 'g') return contrast::TileType::Gray;
    return contrast::TileType::None;
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

int stock_value(const std::map<char,int>& store, char key) {
    auto it = store.find(key);
    return (it == store.end()) ? 0 : it->second;
}

// NOTE:
// This function still uses state.to_move_ / state.history_ like your current code.
// If those are private in your GameState, tell me header and I'll switch to a public-only rebuild strategy.
contrast::GameState snapshot_to_state(const protocol::StateSnapshot& snapshot) {
    contrast::GameState state;
    state.reset();

    auto& board = state.board();
    for (int y = 0; y < board.height(); ++y) {
        for (int x = 0; x < board.width(); ++x) {
            auto& cell = board.at(x, y);
            cell.occupant = contrast::Player::None;
            cell.tile = contrast::TileType::None;
        }
    }

    for (const auto& [coord, piece] : snapshot.pieces) {
        auto [x,y] = coord_to_xy(coord);
        board.at(x,y).occupant = symbol_to_player(piece);
    }
    for (const auto& [coord, tile_char] : snapshot.tiles) {
        auto [x,y] = coord_to_xy(coord);
        board.at(x,y).tile = tile_from_char(tile_char);
    }

    state.inventory(contrast::Player::Black).black = stock_value(snapshot.stock_black, 'X');
    state.inventory(contrast::Player::Black).gray  = stock_value(snapshot.stock_gray,  'X');
    state.inventory(contrast::Player::White).black = stock_value(snapshot.stock_black, 'O');
    state.inventory(contrast::Player::White).gray  = stock_value(snapshot.stock_gray,  'O');

    contrast::Player to_move = symbol_to_player(snapshot.turn);
    state.to_move_ = (to_move == contrast::Player::None) ? contrast::Player::Black : to_move;

    state.history_.clear();
    state.history_[state.compute_hash()] = 1;
    return state;
}

protocol::Move convert_core_move(const contrast::Move& move) {
    protocol::Move out;
    out.origin = xy_to_coord(move.sx, move.sy);
    out.target = xy_to_coord(move.dx, move.dy);
    if (move.place_tile) {
        out.tile.skip  = false;
        out.tile.coord = xy_to_coord(move.tx, move.ty);
        out.tile.color = tile_to_char(move.tile);
    } else {
        out.tile = protocol::TilePlacement::none();
    }
    return out;
}

// ================================================================
// Policy adapters
// ================================================================

class PolicyAdapter {
public:
    virtual ~PolicyAdapter() = default;
    virtual contrast::Move pick(const contrast::GameState& state) = 0;
};

class RandomPolicyAdapter final : public PolicyAdapter {
public:
    contrast::Move pick(const contrast::GameState& state) override { return policy_.pick(state); }
private:
    contrast_ai::RandomPolicy policy_;
};

class RuleBasedPolicyAdapter final : public PolicyAdapter {
public:
    contrast::Move pick(const contrast::GameState& state) override { return policy_.pick(state); }
private:
    contrast_ai::RuleBasedPolicy policy_;
};

class RuleBasedPolicy2Adapter final : public PolicyAdapter {
public:
    contrast::Move pick(const contrast::GameState& state) override { return policy_.pick(state); }
private:
    contrast_ai::RuleBasedPolicy2 policy_;
};

class NTupleBigAdapter final : public PolicyAdapter {
public:
    NTupleBigAdapter() {
        const std::string weights_path = "ai/bin/ntuple_weights_vs_rulebased_swap.bin.100000";
        if (!policy_.load(weights_path)) {
            if (!silent_mode() && !minimal_mode()) {
                std::cerr << "[NTuple] Warning: Failed to load weights from " << weights_path << "\n";
            }
        } else {
            if (!silent_mode() && !minimal_mode()) {
                std::cout << "[NTuple] Loaded weights from " << weights_path << "\n";
            }
        }
    }
    contrast::Move pick(const contrast::GameState& state) override { return policy_.pick(state); }
private:
    contrast_ai::NTuplePolicy policy_;
};

class AlphaBetaAdapter final : public PolicyAdapter {
public:
    explicit AlphaBetaAdapter(int depth = 3) : depth_(depth) {
        const std::string weights_path = "ai/bin/ntuple_weights_vs_rulebased_swap.bin.100000";
        alphabeta_.load_network(weights_path);
        alphabeta_.set_verbose(false);
        alphabeta_.set_use_transposition_table(true);
        alphabeta_.set_use_move_ordering(true);
        if (!silent_mode() && !minimal_mode()) {
            std::cout << "[AlphaBeta] Loaded NTuple weights, depth=" << depth_ << "\n";
        }
    }
    contrast::Move pick(const contrast::GameState& state) override {
        return alphabeta_.search(state, depth_, -1);
    }
private:
    contrast_ai::AlphaBeta alphabeta_;
    int depth_;
};

class MCTSAdapter final : public PolicyAdapter {
public:
    explicit MCTSAdapter(int iterations = 400) : iterations_(iterations) {
        const std::string weights_path = "ai/bin/ntuple_weights_vs_rulebased_swap.bin.100000";
        mcts_.load_network(weights_path);
        mcts_.set_verbose(false);
        mcts_.set_exploration_constant(1.414f);
        if (!silent_mode() && !minimal_mode()) {
            std::cout << "[MCTS] Loaded NTuple weights, iterations=" << iterations_ << "\n";
        }
    }
    contrast::Move pick(const contrast::GameState& state) override {
        return mcts_.search(state, iterations_);
    }
private:
    contrast_ai::MCTS mcts_;
    int iterations_;
};

// ================================================================
// AutoPlayer (fixed: allow retry after ERROR, avoid stall)
// ================================================================

class AutoPlayer {
public:
    static std::unique_ptr<AutoPlayer> Create(
        const std::string& model_name,
        std::function<void(const protocol::Move&)> sender
    ) {
        const std::string normalized = protocol::to_lower(model_name);
        if (normalized.empty() || normalized == "-" || normalized == "manual") return nullptr;

        std::unique_ptr<PolicyAdapter> policy;
        if (normalized == "random") {
            policy = std::make_unique<RandomPolicyAdapter>();
        } else if (normalized == "rule" || normalized == "rulebase" || normalized == "rulebased") {
            policy = std::make_unique<RuleBasedPolicy2Adapter>();
        } else if (normalized == "rulebased1" || normalized == "policy1") {
            policy = std::make_unique<RuleBasedPolicyAdapter>();
        } else if (normalized == "rulebased2" || normalized == "policy2") {
            policy = std::make_unique<RuleBasedPolicy2Adapter>();
        } else if (normalized == "ntuple" || normalized == "ntuple_big" || normalized == "ntuplebig") {
            policy = std::make_unique<NTupleBigAdapter>();
        } else if (normalized == "alphabeta" || normalized == "ab") {
            policy = std::make_unique<AlphaBetaAdapter>(3);
        } else if (normalized.rfind("alphabeta", 0) == 0 || normalized.rfind("ab", 0) == 0) {
            int depth = 5;
            const auto pos = normalized.find(':');
            if (pos != std::string::npos) {
                try {
                    depth = std::stoi(normalized.substr(pos + 1));
                    if (depth < 1 || depth > 20) depth = 5;
                } catch (...) {
                    depth = 5;
                }
            }
            policy = std::make_unique<AlphaBetaAdapter>(depth);
        } else if (normalized == "mcts") {
            policy = std::make_unique<MCTSAdapter>(400);
        } else if (normalized.rfind("mcts", 0) == 0) {
            int iters = 400;
            const auto pos = normalized.find(':');
            if (pos != std::string::npos) {
                try {
                    iters = std::stoi(normalized.substr(pos + 1));
                    if (iters < 10 || iters > 10000) iters = 400;
                } catch (...) {
                    iters = 400;
                }
            }
            policy = std::make_unique<MCTSAdapter>(iters);
        } else {
            std::cerr << "[AUTO] Unsupported model: " << model_name << "\n";
            return nullptr;
        }

        return std::unique_ptr<AutoPlayer>(new AutoPlayer(normalized, std::move(policy), std::move(sender)));
    }

    const std::string& model_name() const { return model_name_; }

    void set_role(char role_symbol) {
        const char upper = static_cast<char>(std::toupper(static_cast<unsigned char>(role_symbol)));
        role_symbol_ = (upper == 'X' || upper == 'O') ? upper : '?';
        reset_internal();
    }

    // Called from client when an ERROR line is received.
    void on_error_received(const std::string& err = "") {
        // Mark last pending as rejected so next same-state turn can retry.
        rejected_game_id_   = pending_game_id_;
        rejected_state_hash_ = pending_state_hash_;
        rejected_move_text_ = pending_move_text_;

        // IMPORTANT: clear pending so we don't "wait forever" and so we can retry on next STATE.
        pending_game_id_ = 0;
        pending_state_hash_ = 0;
        pending_move_text_.clear();
        awaiting_result_ = false;

        if (!silent_mode() && !minimal_mode()) {
            std::cerr << "[AUTO ERROR] " << (err.empty() ? "ERROR" : err)
                      << " rejected='" << rejected_move_text_ << "'"
                      << " (game=" << rejected_game_id_ << ", hash=" << rejected_state_hash_ << ")\n";
        }
    }

    // Called on each authoritative STATE.
    void on_state(const protocol::StateSnapshot& snapshot) {
        if (!policy_) return;
        if (role_symbol_ == '?') return;

        // If game ended, stop pending.
        if (snapshot.status != "ongoing") {
            pending_game_id_ = 0;
            pending_state_hash_ = 0;
            pending_move_text_.clear();
            awaiting_result_ = false;
            return;
        }

        const char turn = static_cast<char>(std::toupper(static_cast<unsigned char>(snapshot.turn)));

        // If it's not our turn, we can clear "awaiting" (our previous move was presumably accepted or game progressed).
        if (turn != role_symbol_) {
            awaiting_result_ = false;
            // NOTE: keep rejected_* as-is (it is keyed by game_id+hash anyway)
            return;
        }

        // Rebuild state and compute hash
        contrast::GameState st = snapshot_to_state(snapshot);
        const uint64_t h = st.compute_hash();
        const uint64_t gid = snapshot.game_id;

        // If we already sent a move for exactly this (game_id, hash) and no error yet, don't resend.
        if (awaiting_result_ && gid == pending_game_id_ && h == pending_state_hash_) {
            return;
        }

        // Pick move
        contrast::Move mv = policy_->pick(st);
        if (mv.sx < 0 || mv.dx < 0) return;

        // quick sanity (avoid obvious desync crashes)
        if (!st.board().in_bounds(mv.sx, mv.sy)) return;
        if (st.board().at(mv.sx, mv.sy).occupant != symbol_to_player(role_symbol_)) return;

        protocol::Move proto = convert_core_move(mv);
        std::string text = protocol::format_move(proto);

        // If the server rejected our last move on the same (gid, hash), avoid resending identical move.
        if (!rejected_move_text_.empty() &&
            gid == rejected_game_id_ &&
            h == rejected_state_hash_ &&
            text == rejected_move_text_) {
            if (!silent_mode() && !minimal_mode()) {
                std::cerr << "[AUTO] picked rejected move again; choosing fallback.\n";
            }

            // Fallback: choose first legal move different from rejected
            contrast::MoveList ml;
            contrast::Rules::legal_moves(st, ml);

            bool found = false;
            for (size_t i = 0; i < ml.size; ++i) {
                protocol::Move p2 = convert_core_move(ml[i]);
                std::string t2 = protocol::format_move(p2);
                if (t2 != rejected_move_text_) {
                    proto = p2;
                    text = t2;
                    found = true;
                    break;
                }
            }
            if (!found) {
                // No alternative exists; do nothing to avoid infinite spam.
                return;
            }
        }

        // Mark as pending BEFORE send
        pending_game_id_ = gid;
        pending_state_hash_ = h;
        pending_move_text_ = text;
        awaiting_result_ = true;

        if (!silent_mode() && !minimal_mode()) {
            std::cout << "[AUTO] " << model_name_ << " plays " << text << "\n";
        }

        sender_(proto);
    }

private:
    AutoPlayer(std::string model_name, std::unique_ptr<PolicyAdapter> policy,
               std::function<void(const protocol::Move&)> sender)
        : sender_(std::move(sender)), policy_(std::move(policy)), model_name_(std::move(model_name)) {}

    void reset_internal() {
        awaiting_result_ = false;

        pending_game_id_ = 0;
        pending_state_hash_ = 0;
        pending_move_text_.clear();

        rejected_game_id_ = 0;
        rejected_state_hash_ = 0;
        rejected_move_text_.clear();
    }

    std::function<void(const protocol::Move&)> sender_;
    std::unique_ptr<PolicyAdapter> policy_;
    std::string model_name_;

    char role_symbol_{'?'};

    bool awaiting_result_{false};

    uint64_t pending_game_id_{0};
    uint64_t pending_state_hash_{0};
    std::string pending_move_text_;

    uint64_t rejected_game_id_{0};
    uint64_t rejected_state_hash_{0};
    std::string rejected_move_text_;
};

// ================================================================
// ContrastClient
// ================================================================

class ContrastClient {
public:
    ContrastClient(std::string desired_role, std::string nickname, std::string model, int num_games = 1)
        : desired_role_(std::move(desired_role)),
          nickname_(std::move(nickname)),
          model_arg_(std::move(model)),
          num_games_(num_games) {
        const std::string normalized = protocol::to_lower(model_arg_);
        model_requested_ = !normalized.empty() && normalized != "-" && normalized != "manual";
    }

    int run() {
        if (!connect_server()) return 1;

        if (model_requested_) {
            auto requested = AutoPlayer::Create(model_arg_, [this](const protocol::Move& m) {
                protocol::Move mm = m;
                mm.game_id = current_game_id_;       // always attach latest known game id
                mm.move_id = next_move_id_++;         // monotonic per game id
                const std::string text = protocol::format_move(mm);
                safe_send("MOVE " + text + "\n");
            });
            if (!requested) {
                std::cerr << "Unable to initialize model '" << model_arg_ << "'\n";
                return 1;
            }
            auto_player_ = std::move(requested);
            if (!silent_mode() && !minimal_mode()) {
                std::cout << "[AUTO] Enabled " << auto_player_->model_name() << " policy\n";
            }
        }

        std::thread reader(&ContrastClient::reader_loop, this);

        std::thread input;
        if (!auto_player_) {
            input = std::thread(&ContrastClient::input_loop, this);
        }

        send_handshake();

        reader.join();
        running_ = false;
        if (input.joinable()) input.join();

        if (socket_ >= 0) ::close(socket_);
        return 0;
    }

private:
    bool connect_server() {
        socket_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (socket_ < 0) {
            std::cerr << "socket() failed\n";
            return false;
        }

        const int server_port = resolve_server_port();
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(static_cast<uint16_t>(server_port));
        if (::inet_pton(AF_INET, kServerHost, &addr.sin_addr) <= 0) {
            std::cerr << "inet_pton failed\n";
            return false;
        }
        if (::connect(socket_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            std::cerr << "connect() failed: " << std::strerror(errno) << "\n";
            return false;
        }
        if (!minimal_mode()) std::cout << "Connected to " << kServerHost << ":" << server_port << "\n";
        return true;
    }

    void send_handshake() {
        if (desired_role_.empty() && nickname_.empty() && model_arg_.empty()) return;

        const std::string role  = desired_role_.empty() ? "-" : desired_role_;
        const std::string name  = nickname_.empty() ? "-" : nickname_;
        const std::string model = model_arg_.empty() ? "-" : model_arg_;

        std::string payload = "ROLE " + role + " " + name + " " + model;
        if (num_games_ > 1) payload += " multi";
        payload += "\n";
        safe_send(payload);
    }

    void reader_loop() {
        std::string buffer;

        while (running_) {
            auto line_opt = recv_line(socket_, buffer);
            if (!line_opt) break;
            std::string line = *line_opt;
            if (line.empty()) continue;

            if (line == "STATE") {
                std::vector<std::string> block;
                for (;;) {
                    auto next = recv_line(socket_, buffer);
                    if (!next) { running_ = false; break; }
                    if (*next == "END") break;
                    block.push_back(*next);
                }
                if (block.empty()) continue;

                protocol::StateSnapshot snapshot = protocol::parse_state_block(block);

                // game_id change => reset move_id counter
                if (snapshot.game_id != current_game_id_) {
                    current_game_id_ = snapshot.game_id;
                    next_move_id_ = 1;
                }

                if (!minimal_mode()) {
                    std::cout << "Turn: " << snapshot.turn
                              << " | Status: " << snapshot.status
                              << " | Last move: " << snapshot.last_move << "\n";
                }

                // finished?
                if (snapshot.status != last_status_ && snapshot.status != "ongoing") {
                    if (snapshot.status == "X_win" || snapshot.status == "x_win") {
                        std::cout << "[RESULT] X win\n";
                    } else if (snapshot.status == "O_win" || snapshot.status == "o_win" || snapshot.status == "0_win") {
                        std::cout << "[RESULT] O win\n";
                    } else {
                        std::cout << "[RESULT] " << snapshot.status << "\n";
                    }

                    games_played_++;
                    if (games_played_ < num_games_ && auto_player_) {
                        if (!minimal_mode()) {
                            std::cout << "[AUTO] Game " << games_played_ << "/" << num_games_
                                      << " finished. Sending READY...\n";
                        }
                        safe_send("READY\n");
                    } else if (games_played_ >= num_games_) {
                        if (!minimal_mode()) std::cout << "[AUTO] All " << num_games_ << " games completed.\n";
                        running_ = false;
                    }
                }
                last_status_ = snapshot.status;

                if (auto_player_) {
                    auto_player_->on_state(snapshot);
                }

                continue;
            }

            if (line.rfind("INFO ", 0) == 0) {
                const std::string payload = line.substr(5);
                if (!minimal_mode()) std::cout << "[INFO] " << payload << "\n";
                handle_info_line(payload);
                continue;
            }

            if (line.rfind("ERROR ", 0) == 0) {
                const std::string payload = line.substr(6);
                std::cout << "[ERROR] " << payload << "\n";
                if (auto_player_) auto_player_->on_error_received(payload);
                continue;
            }

            if (!minimal_mode()) {
                std::cout << "[SERVER] " << line << "\n";
            }
        }

        running_ = false;
        if (!minimal_mode()) std::cout << "Connection closed\n";
    }

    void input_loop() {
        while (running_) {
            std::string line;
            std::cout << "move> " << std::flush;
            if (!std::getline(std::cin, line)) break;
            if (line.empty()) continue;

            if (line == ":quit") { running_ = false; break; }
            if (line == ":get")  { safe_send("GET_STATE\n"); continue; }

            try {
                protocol::parse_move(line);
            } catch (const std::exception& ex) {
                std::cout << "[LOCAL] Invalid move: " << ex.what() << "\n";
                continue;
            }

            std::string payload = "MOVE " + line;
            if (current_game_id_ != 0) {
                payload += " " + std::to_string(current_game_id_) + " " + std::to_string(next_move_id_++);
            }
            payload += "\n";
            safe_send(payload);
        }
        running_ = false;
    }

    void safe_send(const std::string& payload) {
        std::lock_guard<std::mutex> lock(send_mutex_);
        try {
            send_all(socket_, payload);
        } catch (const std::exception& ex) {
            std::cerr << "Send failed: " << ex.what() << "\n";
            running_ = false;
        }
    }

    void handle_info_line(const std::string& payload) {
        const std::string prefix = "You are ";
        if (payload.rfind(prefix, 0) != 0) return;

        std::string rest = payload.substr(prefix.size());
        const auto end = rest.find_first_of(" (\t");
        if (end != std::string::npos) rest = rest.substr(0, end);

        char resolved = '?';
        if (!rest.empty()) {
            const char upper = static_cast<char>(std::toupper(static_cast<unsigned char>(rest.front())));
            if (upper == 'X' || upper == 'O') resolved = upper;
        }

        assigned_role_ = resolved;
        if (auto_player_) auto_player_->set_role(assigned_role_);
    }

private:
    int socket_{-1};
    std::atomic<bool> running_{true};
    std::mutex send_mutex_;

    std::string desired_role_;
    std::string nickname_;
    std::string model_arg_;
    bool model_requested_{false};

    std::unique_ptr<AutoPlayer> auto_player_;
    char assigned_role_{'?'};

    std::string last_status_{""};

    uint64_t current_game_id_{0};
    uint64_t next_move_id_{1};

    int num_games_{1};
    int games_played_{0};
};

} // namespace

int main(int argc, char** argv) {
    std::signal(SIGPIPE, SIG_IGN);

    try {
        std::string role;
        std::string name;
        std::string model;
        int num_games = 1;

        if (argc >= 2) role = argv[1];
        if (argc >= 3) name = argv[2];
        if (argc >= 4) model = argv[3];
        if (argc >= 5) {
            try {
                num_games = std::stoi(argv[4]);
                if (num_games < 1) num_games = 1;
            } catch (...) {
                std::cerr << "Invalid number of games, using 1\n";
                num_games = 1;
            }
        }

        ContrastClient client(role, name, model, num_games);
        return client.run();
    } catch (const std::exception& ex) {
        std::cerr << "Fatal client error: " << ex.what() << "\n";
        return 1;
    }
}
