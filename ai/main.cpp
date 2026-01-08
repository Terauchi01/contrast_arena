#include "contrast/game_state.hpp"
#include "contrast/rules.hpp"
#include "contrast/move_list.hpp"
#include "rule_based_policy.hpp"
#include "random_policy.hpp"
#include "mcts.hpp"
#include "alphabeta.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

using namespace contrast;
using namespace contrast_ai;

void print_board(const GameState& state) {
    std::cout << "\nBoard (y=0 at top, y=4 at bottom):\n";
    for (int y = 0; y < 5; ++y) {
        std::cout << "y=" << y << ": ";
        for (int x = 0; x < 5; ++x) {
            Player p = state.board().at(x, y).occupant;
            if (p == Player::Black) std::cout << "B ";
            else if (p == Player::White) std::cout << "W ";
            else std::cout << ". ";
        }
        std::cout << "\n";
    }
    std::cout << "Current player: " << (state.current_player() == Player::Black ? "Black" : "White") << "\n";
}

void print_move(const Move& m) {
    std::cout << "  Move: (" << m.sx << "," << m.sy << ") -> (" << m.dx << "," << m.dy << ")";
    std::cout << " [delta_y=" << (m.dy - m.sy) << "]";
    if (m.place_tile) {
        std::cout << " + tile at (" << m.tx << "," << m.ty << ")";
    }
    std::cout << "\n";
}

struct PolicyBinding {
    std::string name;
    std::function<Move(const GameState&)> pick;
};

Player play_game(const PolicyBinding& black_policy, const PolicyBinding& white_policy,
                 int& moves, bool verbose = false, int max_moves_to_log = 0) {
    GameState state;
    moves = 0;
    const int MAX_MOVES = 1000;

    if (verbose) {
        std::cout << "\n========== Game Start =========="
                  << "\nBlack policy: " << black_policy.name
                  << "\nWhite policy: " << white_policy.name
                  << "\nBlack goal: y=4 (bottom), starts at y=0 (top)"
                  << "\nWhite goal: y=0 (top), starts at y=4 (bottom)\n";
        print_board(state);
    }

    while (moves < MAX_MOVES) {
        if (Rules::is_win(state, Player::Black)) {
            if (verbose) {
                std::cout << "\n*** Black WINS! ***\n";
            }
            return Player::Black;
        }
        if (Rules::is_win(state, Player::White)) {
            if (verbose) {
                std::cout << "\n*** White WINS! ***\n";
            }
            return Player::White;
        }

        MoveList legal_moves;
        Rules::legal_moves(state, legal_moves);
        if (legal_moves.empty()) {
            const Player loser = state.current_player();
            const Player winner = (loser == Player::Black) ? Player::White : Player::Black;
            if (verbose) {
                std::cout << "\n*** No legal moves for "
                          << (loser == Player::Black ? "Black" : "White")
                          << " - " << (winner == Player::Black ? "Black" : "White")
                          << " WINS! ***\n";
            }
            return winner;
        }

        Move move = (state.current_player() == Player::Black) ? black_policy.pick(state)
                                                              : white_policy.pick(state);

        if (verbose && moves < max_moves_to_log) {
            std::cout << "\nMove " << (moves + 1) << " - "
                      << (state.current_player() == Player::Black ? "Black" : "White")
                      << ":\n";
            print_move(move);
        }

        state.apply_move(move);
        ++moves;

        if (verbose && moves < max_moves_to_log) {
            print_board(state);
        }
    }

    if (verbose) {
        std::cout << "\n*** DRAW (max moves reached) ***\n";
    }
    return Player::None;
}

void run_match_series(const PolicyBinding& black_policy, const PolicyBinding& white_policy, int num_games) {
    int black_wins = 0;
    int white_wins = 0;
    int draws = 0;
    int total_moves = 0;

    std::cout << "\n======================================\n";
    std::cout << "Testing " << black_policy.name << " (Black) vs " << white_policy.name << " (White)\n";
    std::cout << "Number of games: " << num_games << "\n";
    std::cout << "======================================\n";

    int moves = 0;
    Player winner = play_game(black_policy, white_policy, moves, true, 20);
    if (winner == Player::Black) {
        ++black_wins;
    } else if (winner == Player::White) {
        ++white_wins;
    } else {
        ++draws;
    }
    total_moves += moves;

    std::cout << "\nFirst game result: ";
    if (winner == Player::Black) {
        std::cout << "Black wins";
    } else if (winner == Player::White) {
        std::cout << "White wins";
    } else {
        std::cout << "Draw";
    }
    std::cout << " in " << moves << " moves\n";

    for (int i = 1; i < num_games; ++i) {
        int game_moves = 0;
        Player game_winner = play_game(black_policy, white_policy, game_moves);
        if (game_winner == Player::Black) {
            ++black_wins;
        } else if (game_winner == Player::White) {
            ++white_wins;
        } else {
            ++draws;
        }
        total_moves += game_moves;
    }

    std::cout << "\n======================================\n";
    std::cout << "Results after " << num_games << " games:\n";
    std::cout << "  Black wins: " << black_wins << " (" << (100.0 * black_wins / num_games) << "%)\n";
    std::cout << "  White wins: " << white_wins << " (" << (100.0 * white_wins / num_games) << "%)\n";
    std::cout << "  Draws: " << draws << " (" << (100.0 * draws / num_games) << "%)\n";
    std::cout << "  Average moves: " << static_cast<double>(total_moves) / num_games << "\n";
    std::cout << "======================================\n";
}

enum class PolicyChoice { Rule, Random, MCTS, AlphaBeta };

std::string to_lower_copy(std::string_view text) {
    std::string lowered(text);
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return lowered;
}

bool is_number_string(std::string_view text) {
    return !text.empty() && std::all_of(text.begin(), text.end(), [](unsigned char ch) { return std::isdigit(ch); });
}

PolicyChoice parse_policy_choice(std::string_view text) {
    const std::string lowered = to_lower_copy(text);
    if (lowered == "random" || lowered == "rand") {
        return PolicyChoice::Random;
    }
    if (lowered == "rule" || lowered == "rulebased") {
        return PolicyChoice::Rule;
    }
    if (lowered == "mcts") {
        return PolicyChoice::MCTS;
    }
    if (lowered == "alphabeta" || lowered == "ab") {
        return PolicyChoice::AlphaBeta;
    }
    throw std::invalid_argument("Unsupported policy type: " + std::string(text));
}

PolicyBinding make_binding(PolicyChoice choice, RuleBasedPolicy& rule_policy, RandomPolicy& random_policy) {
    if (choice == PolicyChoice::Random) {
        return {"RandomPolicy", [&random_policy](const GameState& state) { return random_policy.pick(state); }};
    }
    return {"RuleBasedPolicy", [&rule_policy](const GameState& state) { return rule_policy.pick(state); }};
}

int main(int argc, char** argv) {
    int num_games = 100;
    PolicyChoice black_choice = PolicyChoice::Rule;
    PolicyChoice white_choice = PolicyChoice::Rule;

    int arg_index = 1;
    if (arg_index < argc && is_number_string(argv[arg_index])) {
        num_games = std::max(1, std::atoi(argv[arg_index]));
        ++arg_index;
    }

    for (; arg_index < argc; ++arg_index) {
        std::string_view arg = argv[arg_index];
        if (arg.rfind("--black=", 0) == 0) {
            black_choice = parse_policy_choice(arg.substr(8));
        } else if (arg.rfind("--white=", 0) == 0) {
            white_choice = parse_policy_choice(arg.substr(8));
        } else if (arg == "--random-vs-random") {
            black_choice = PolicyChoice::Random;
            white_choice = PolicyChoice::Random;
        } else if (arg == "--rule-vs-rule") {
            black_choice = PolicyChoice::Rule;
            white_choice = PolicyChoice::Rule;
        } else {
            std::cerr << "Unknown argument: " << arg << "\n";
        }
    }

    RuleBasedPolicy black_rule_policy;
    RandomPolicy black_random_policy;
    RuleBasedPolicy white_rule_policy;
    RandomPolicy white_random_policy;

    // Optional MCTS / AlphaBeta instances (kept alive for closure captures)
    std::unique_ptr<contrast_ai::MCTS> black_mcts_ptr;
    std::unique_ptr<contrast_ai::MCTS> white_mcts_ptr;
    std::unique_ptr<contrast_ai::AlphaBeta> black_ab_ptr;
    std::unique_ptr<contrast_ai::AlphaBeta> white_ab_ptr;

    const std::string weights_path = "ai/bin/ntuple_weights_vs_rulebased_swap.bin.100000";

    PolicyBinding black_binding;
    if (black_choice == PolicyChoice::MCTS) {
        black_mcts_ptr = std::make_unique<contrast_ai::MCTS>();
        black_mcts_ptr->load_network(weights_path);
        black_binding.name = "MCTS";
        black_binding.pick = [m = black_mcts_ptr.get()](const GameState& state) {
            return m->search(state, 400, 0);
        };
    } else if (black_choice == PolicyChoice::AlphaBeta) {
        black_ab_ptr = std::make_unique<contrast_ai::AlphaBeta>();
        black_ab_ptr->load_network(weights_path);
        black_binding.name = "AlphaBeta";
        black_binding.pick = [m = black_ab_ptr.get()](const GameState& state) {
            return m->search(state, 3, 0);
        };
    } else {
        black_binding = make_binding(black_choice, black_rule_policy, black_random_policy);
    }

    PolicyBinding white_binding;
    if (white_choice == PolicyChoice::MCTS) {
        white_mcts_ptr = std::make_unique<contrast_ai::MCTS>();
        white_mcts_ptr->load_network(weights_path);
        white_binding.name = "MCTS";
        white_binding.pick = [m = white_mcts_ptr.get()](const GameState& state) {
            return m->search(state, 400, 0);
        };
    } else if (white_choice == PolicyChoice::AlphaBeta) {
        white_ab_ptr = std::make_unique<contrast_ai::AlphaBeta>();
        white_ab_ptr->load_network(weights_path);
        white_binding.name = "AlphaBeta";
        white_binding.pick = [m = white_ab_ptr.get()](const GameState& state) {
            return m->search(state, 5, 0);
        };
    } else {
        white_binding = make_binding(white_choice, white_rule_policy, white_random_policy);
    }

    run_match_series(black_binding, white_binding, num_games);
    return 0;
}
