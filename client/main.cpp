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

constexpr int kServerPort = 8765;
constexpr char kServerHost[] = "127.0.0.1";

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

void send_all(int socket, const std::string& payload) {
    const char* data = payload.data();
    ssize_t remaining = static_cast<ssize_t>(payload.size());
    while (remaining > 0) {
        const ssize_t sent = ::send(socket, data, remaining, 0);
        if (sent <= 0) {
            throw std::runtime_error("send failed");
        }
        remaining -= sent;
        data += sent;
    }
}

contrast::Player symbol_to_player(char symbol) {
    const char upper = static_cast<char>(std::toupper(static_cast<unsigned char>(symbol)));
    if (upper == 'X') {
        return contrast::Player::Black;
    }
    if (upper == 'O') {
        return contrast::Player::White;
    }
    return contrast::Player::None;
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

contrast::TileType tile_from_char(char color) {
    const char lower = static_cast<char>(std::tolower(static_cast<unsigned char>(color)));
    if (lower == 'b') {
        return contrast::TileType::Black;
    }
    if (lower == 'g') {
        return contrast::TileType::Gray;
    }
    return contrast::TileType::None;
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

int stock_value(const std::map<char, int>& store, char key) {
    if (auto it = store.find(key); it != store.end()) {
        return it->second;
    }
    return 0;
}

contrast::GameState snapshot_to_state(const protocol::StateSnapshot& snapshot) {
    contrast::GameState state;
    state.reset();
    contrast::Board& board = state.board();
    for (int y = 0; y < board.height(); ++y) {
        for (int x = 0; x < board.width(); ++x) {
            auto& cell = board.at(x, y);
            cell.occupant = contrast::Player::None;
            cell.tile = contrast::TileType::None;
        }
    }
    for (const auto& [coord, piece] : snapshot.pieces) {
        auto [x, y] = coord_to_xy(coord);
        board.at(x, y).occupant = symbol_to_player(piece);
    }
    for (const auto& [coord, tile_char] : snapshot.tiles) {
        auto [x, y] = coord_to_xy(coord);
        board.at(x, y).tile = tile_from_char(tile_char);
    }
    state.inventory(contrast::Player::Black).black = stock_value(snapshot.stock_black, 'X');
    state.inventory(contrast::Player::Black).gray = stock_value(snapshot.stock_gray, 'X');
    state.inventory(contrast::Player::White).black = stock_value(snapshot.stock_black, 'O');
    state.inventory(contrast::Player::White).gray = stock_value(snapshot.stock_gray, 'O');
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
        out.tile.skip = false;
        out.tile.coord = xy_to_coord(move.tx, move.ty);
        out.tile.color = tile_to_char(move.tile);
    } else {
        out.tile = protocol::TilePlacement::none();
    }
    return out;
}

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
        // Load weights from the trained file
        const std::string weights_path = "ai/bin/ntuple_weights_vs_rulebased_swap.bin.100000";
        if (!policy_.load(weights_path)) {
            std::cerr << "[NTuple] Warning: Failed to load weights from " << weights_path << std::endl;
        } else {
            std::cout << "[NTuple] Loaded weights from " << weights_path << std::endl;
        }
    }
    contrast::Move pick(const contrast::GameState& state) override { return policy_.pick(state); }

   private:
    contrast_ai::NTuplePolicy policy_;
};

class AlphaBetaAdapter final : public PolicyAdapter {
   public:
    AlphaBetaAdapter(int depth = 3) : depth_(depth) {
        // Load weights from the trained file
        const std::string weights_path = "ai/bin/ntuple_weights_vs_rulebased_swap.bin.100000";
        alphabeta_.load_network(weights_path);
        alphabeta_.set_verbose(false);
        alphabeta_.set_use_transposition_table(true);
        alphabeta_.set_use_move_ordering(true);
        std::cout << "[AlphaBeta] Loaded NTuple weights, depth=" << depth_ << std::endl;
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
    MCTSAdapter(int iterations = 400) : iterations_(iterations) {
        // Load weights from the trained file
        const std::string weights_path = "ai/bin/ntuple_weights_vs_rulebased_swap.bin.100000";
        mcts_.load_network(weights_path);
        mcts_.set_verbose(false);  // 本番用
        mcts_.set_exploration_constant(1.414f);
        std::cout << "[MCTS] Loaded NTuple weights, iterations=" << iterations_ << std::endl;
    }
    contrast::Move pick(const contrast::GameState& state) override {
        return mcts_.search(state, iterations_);
    }

   private:
    contrast_ai::MCTS mcts_;
    int iterations_;
};

class AutoPlayer {
   public:
    static std::unique_ptr<AutoPlayer> Create(const std::string& model_name,
                                              std::function<void(const std::string&)> sender) {
        const std::string normalized = protocol::to_lower(model_name);
        if (normalized.empty() || normalized == "-" || normalized == "manual") {
            return nullptr;
        }
        std::unique_ptr<PolicyAdapter> policy;
        if (normalized == "random") {
            policy = std::make_unique<RandomPolicyAdapter>();
        } else if (normalized == "rule" || normalized == "rulebase" || normalized == "rulebased") {
            policy = std::make_unique<RuleBasedPolicy2Adapter>();  // デフォルトはpolicy2
        } else if (normalized == "rulebased1" || normalized == "policy1") {
            policy = std::make_unique<RuleBasedPolicyAdapter>();  // 旧版も使える
        } else if (normalized == "rulebased2" || normalized == "policy2") {
            policy = std::make_unique<RuleBasedPolicy2Adapter>();
        } else if (normalized == "ntuple" || normalized == "ntuple_big" || normalized == "ntuplebig") {
            policy = std::make_unique<NTupleBigAdapter>();
        } else if (normalized == "alphabeta" || normalized == "ab") {
            // デフォルト深さは3（実用的な速度）
            policy = std::make_unique<AlphaBetaAdapter>(3);
        } else if (normalized.find("alphabeta") == 0 || normalized.find("ab") == 0) {
            // alphabeta:6 のように深さを指定できる
            size_t pos = normalized.find(':');
            int depth = 5;
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
            // デフォルトは400回
            policy = std::make_unique<MCTSAdapter>(400);
        } else if (normalized.find("mcts") == 0) {
            // mcts:1000 のように回数を指定できる
            size_t pos = normalized.find(':');
            int iterations = 400;
            if (pos != std::string::npos) {
                try {
                    iterations = std::stoi(normalized.substr(pos + 1));
                    if (iterations < 10 || iterations > 10000) iterations = 400;
                } catch (...) {
                    iterations = 400;
                }
            }
            policy = std::make_unique<MCTSAdapter>(iterations);
        } else {
            std::cerr << "[AUTO] Unsupported model: " << model_name << std::endl;
            return nullptr;
        }
        return std::unique_ptr<AutoPlayer>(
            new AutoPlayer(normalized, std::move(policy), std::move(sender)));
    }

    void set_role(char role_symbol) {
        const char upper = static_cast<char>(std::toupper(static_cast<unsigned char>(role_symbol)));
        if (upper == 'X' || upper == 'O') {
            role_symbol_ = upper;
        } else {
            role_symbol_ = '?';
        }
        awaiting_turn_resolution_ = false;
    }

    void handle_snapshot(const protocol::StateSnapshot& snapshot) {
        if (!policy_ || role_symbol_ == '?') {
            awaiting_turn_resolution_ = false;
            return;
        }
        if (snapshot.status != "ongoing") {
            awaiting_turn_resolution_ = false;
            return;
        }
        const char turn_symbol = static_cast<char>(
            std::toupper(static_cast<unsigned char>(snapshot.turn)));
        if (turn_symbol != role_symbol_) {
            awaiting_turn_resolution_ = false;
            return;
        }
        if (awaiting_turn_resolution_) {
            return;
        }
        const contrast::GameState state = snapshot_to_state(snapshot);
        const contrast::Move move = policy_->pick(state);
        if (move.sx < 0 || move.dx < 0) {
            return;
        }
        const protocol::Move proto = convert_core_move(move);
        const std::string text = protocol::format_move(proto);
        std::cout << "[AUTO] " << model_name_ << " plays " << text << '\n';
        sender_("MOVE " + text + "\n");
        awaiting_turn_resolution_ = true;
    }

    const std::string& model_name() const { return model_name_; }

   private:
    AutoPlayer(std::string model_name, std::unique_ptr<PolicyAdapter> policy,
               std::function<void(const std::string&)> sender)
        : sender_(std::move(sender)), policy_(std::move(policy)), model_name_(std::move(model_name)) {}

    std::function<void(const std::string&)> sender_;
    std::unique_ptr<PolicyAdapter> policy_;
    std::string model_name_;
    char role_symbol_{'?'};
    bool awaiting_turn_resolution_{false};
};

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
        if (!connect_server()) {
            return 1;
        }
        if (model_requested_) {
            auto requested = AutoPlayer::Create(model_arg_, [this](const std::string& payload) {
                safe_send(payload);
            });
            if (!requested) {
                std::cerr << "Unable to initialize model '" << model_arg_ << "'" << std::endl;
                return 1;
            }
            auto_player_ = std::move(requested);
            std::cout << "[AUTO] Enabled " << auto_player_->model_name() << " policy" << std::endl;
        }
        std::thread reader(&ContrastClient::reader_loop, this);
        std::thread input;
        // Only start input loop if not in auto mode
        if (!auto_player_) {
            input = std::thread(&ContrastClient::input_loop, this);
        }
        // Send handshake immediately (server waits for ROLE command)
        send_handshake();
        reader.join();
        running_ = false;
        if (input.joinable()) {
            input.join();
        }
        if (socket_ >= 0) {
            ::close(socket_);
        }
        return 0;
    }

   private:
    bool connect_server() {
        socket_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (socket_ < 0) {
            std::cerr << "socket() failed" << std::endl;
            return false;
        }
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(kServerPort);
        if (::inet_pton(AF_INET, kServerHost, &addr.sin_addr) <= 0) {
            std::cerr << "inet_pton failed" << std::endl;
            return false;
        }
        if (::connect(socket_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            std::cerr << "connect() failed: " << std::strerror(errno) << std::endl;
            return false;
        }
        std::cout << "Connected to " << kServerHost << ':' << kServerPort << std::endl;
        return true;
    }

    void send_handshake() {
        if (desired_role_.empty() && nickname_.empty() && model_arg_.empty()) {
            return;
        }
        const std::string role = desired_role_.empty() ? "-" : desired_role_;
        const std::string name = nickname_.empty() ? "-" : nickname_;
        const std::string model = model_arg_.empty() ? "-" : model_arg_;
        safe_send("ROLE " + role + ' ' + name + ' ' + model + "\n");
    }

    void reader_loop() {
        std::string buffer;
        while (running_) {
            auto line = recv_line(socket_, buffer);
            if (!line) {
                break;
            }
            if (line->empty()) {
                continue;
            }
            if (*line == "STATE") {
                std::vector<std::string> block;
                while (true) {
                    auto next = recv_line(socket_, buffer);
                    if (!next) {
                        running_ = false;
                        break;
                    }
                    if (*next == "END") {
                        break;
                    }
                    block.push_back(*next);
                }
                if (!block.empty()) {
                    protocol::StateSnapshot snapshot = protocol::parse_state_block(block);
                    std::cout << "\n=== STATE ===\n";
                    std::cout << protocol::render_board(snapshot.pieces, snapshot.tiles) << '\n';
                    std::cout << "Turn: " << snapshot.turn << " | Status: " << snapshot.status
                              << " | Last move: " << snapshot.last_move << '\n';
                    auto stock_val = [](const std::map<char, int>& store, char player) {
                        if (auto it = store.find(player); it != store.end()) {
                            return it->second;
                        }
                        return 0;
                    };
                    const int o_black = stock_val(snapshot.stock_black, 'O');
                    const int o_gray = stock_val(snapshot.stock_gray, 'O');
                    const int x_black = stock_val(snapshot.stock_black, 'X');
                    const int x_gray = stock_val(snapshot.stock_gray, 'X');
                    if (o_black || o_gray || x_black || x_gray) {
                        std::cout << "Tiles O[B/G]=" << o_black << '/' << o_gray
                                  << " | X[B/G]=" << x_black << '/' << x_gray << '\n';
                    }
                    if (snapshot.status != last_status_ && snapshot.status != "ongoing") {
                        std::string result = snapshot.status;
                        if (result == "X_win" || result == "x_win") {
                            std::cout << "[RESULT] X win\n";
                        } else if (result == "O_win" || result == "o_win" || result == "0_win") {
                            std::cout << "[RESULT] O win\n";
                        } else {
                            std::cout << "[RESULT] " << result << '\n';
                        }
                        
                        // Handle game completion and auto-restart
                        games_played_++;
                        if (games_played_ < num_games_ && auto_player_) {
                            std::cout << "[AUTO] Game " << games_played_ << "/" << num_games_ 
                                     << " finished. Sending READY for next game...\n";
                            safe_send("READY\n");
                        } else if (games_played_ >= num_games_) {
                            std::cout << "[AUTO] All " << num_games_ << " games completed.\n";
                            running_ = false;
                        }
                    }
                    last_status_ = snapshot.status;
                    if (auto_player_) {
                        auto_player_->handle_snapshot(snapshot);
                    }
                }
                continue;
            }
            if (line->rfind("INFO ", 0) == 0) {
                const std::string payload = line->substr(5);
                std::cout << "[INFO] " << payload << '\n';
                handle_info_line(payload);
            } else if (line->rfind("ERROR ", 0) == 0) {
                std::cout << "[ERROR] " << line->substr(6) << '\n';
            } else {
                std::cout << "[SERVER] " << *line << '\n';
            }
        }
        running_ = false;
        std::cout << "Connection closed" << std::endl;
    }

    void input_loop() {
        while (running_) {
            std::string line;
            std::cout << "move> " << std::flush;
            if (!std::getline(std::cin, line)) {
                break;
            }
            if (line.empty()) {
                continue;
            }
            if (line == ":quit") {
                running_ = false;
                break;
            }
            if (line == ":get") {
                safe_send("GET_STATE\n");
                continue;
            }
            try {
                protocol::parse_move(line);
            } catch (const std::exception& ex) {
                std::cout << "[LOCAL] Invalid move: " << ex.what() << '\n';
                continue;
            }
            safe_send("MOVE " + line + "\n");
        }
        running_ = false;
    }

    void safe_send(const std::string& payload) {
        std::lock_guard<std::mutex> lock(send_mutex_);
        try {
            send_all(socket_, payload);
        } catch (const std::exception& ex) {
            std::cerr << "Send failed: " << ex.what() << std::endl;
            running_ = false;
        }
    }

    void handle_info_line(const std::string& payload) {
        const std::string prefix = "You are ";
        if (payload.rfind(prefix, 0) != 0) {
            return;
        }
        std::string rest = payload.substr(prefix.size());
        const auto end = rest.find_first_of(" (\t");
        if (end != std::string::npos) {
            rest = rest.substr(0, end);
        }
        char resolved = '?';
        if (!rest.empty()) {
            const char upper = static_cast<char>(std::toupper(static_cast<unsigned char>(rest.front())));
            if (upper == 'X' || upper == 'O') {
                resolved = upper;
            }
        }
        assigned_role_ = resolved;
        if (auto_player_) {
            auto_player_->set_role(assigned_role_);
        }
    }

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
    int num_games_{1};
    int games_played_{0};
};

}  // namespace

int main(int argc, char** argv) {
    std::signal(SIGPIPE, SIG_IGN);
    try {
        std::string role;
        std::string name;
        std::string model;
        int num_games = 1;
        
        if (argc >= 2) {
            role = argv[1];
        }
        if (argc >= 3) {
            name = argv[2];
        }
        if (argc >= 4) {
            model = argv[3];
        }
        if (argc >= 5) {
            try {
                num_games = std::stoi(argv[4]);
                if (num_games < 1) num_games = 1;
            } catch (...) {
                std::cerr << "Invalid number of games, using 1" << std::endl;
                num_games = 1;
            }
        }
        
        ContrastClient client(role, name, model, num_games);
        return client.run();
    } catch (const std::exception& ex) {
        std::cerr << "Fatal client error: " << ex.what() << std::endl;
        return 1;
    }
}
