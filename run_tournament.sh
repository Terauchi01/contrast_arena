#!/bin/bash
# Tournament: All players round-robin
# Each matchup: 1000 games as X, 1000 games as O (2000 games total)

# プレイヤー定義
declare -A PLAYERS
PLAYERS=(
    ["random"]="random"
    ["rulebased2"]="rulebased2"
    ["ntuple"]="ntuple"
    ["alphabeta"]="alphabeta"
    ["mcts"]="mcts"
)

# アルファゼロは別扱い（Pythonスクリプト）
ALPHAZERO_SCRIPT="client/python_alphazero_bot.py"

# 結果ディレクトリ
RESULT_DIR="tournament_results_$(date +%Y%m%d_%H%M%S)"
mkdir -p "$RESULT_DIR"

LOG_FILE="$RESULT_DIR/tournament.log"
SUMMARY_FILE="$RESULT_DIR/summary.txt"

echo "=== Tournament Started at $(date) ===" | tee -a "$LOG_FILE"
echo "Players: random, rulebased2, ntuple, alphabeta(depth=3), mcts(400), alphazero(400)" | tee -a "$LOG_FILE"
echo "Format: Full round-robin (all combinations including self-play)" | tee -a "$LOG_FILE"
echo "Total matchups: 6x6 = 36 matchups" | tee -a "$LOG_FILE"
echo "Games per matchup: 2000 (1000 as X, 1000 as O)" | tee -a "$LOG_FILE"
echo "Total games: 72,000" | tee -a "$LOG_FILE"
echo "Results directory: $RESULT_DIR" | tee -a "$LOG_FILE"
echo "" | tee -a "$LOG_FILE"

# サーバー起動
pkill -9 server_app client_app python 2>/dev/null
sleep 2
./server_app > "$RESULT_DIR/server.log" 2>&1 &
SERVER_PID=$!
sleep 3

echo "Server started (PID: $SERVER_PID)" | tee -a "$LOG_FILE"

# 1ゲーム実行関数（C++プレイヤー）
run_cpp_game() {
    local x_name=$1
    local x_type=$2
    local o_name=$3
    local o_type=$4
    local game_num=$5
    local log_file=$6
    
    timeout 300 ./client_app X "$x_name" "$x_type" 1 > /dev/null 2>&1 &
    local x_pid=$!
    sleep 0.5
    timeout 300 ./client_app O "$o_name" "$o_type" 1 > /dev/null 2>&1 &
    local o_pid=$!
    
    wait $x_pid $o_pid 2>/dev/null
    
    echo "Game $game_num: $x_name(X) vs $o_name(O) completed" >> "$log_file"
}

# 1ゲーム実行関数（アルファゼロ含む）
run_alphazero_game() {
    local x_name=$1
    local x_type=$2
    local x_is_az=$3
    local o_name=$4
    local o_type=$5
    local o_is_az=$6
    local game_num=$7
    local log_file=$8
    
    if [ "$x_is_az" = "true" ]; then
        timeout 600 python3 "$ALPHAZERO_SCRIPT" --role X --name "$x_name" --simulations 400 --games 1 > /dev/null 2>&1 &
        local x_pid=$!
    else
        timeout 600 ./client_app X "$x_name" "$x_type" 1 > /dev/null 2>&1 &
        local x_pid=$!
    fi
    
    sleep 1
    
    if [ "$o_is_az" = "true" ]; then
        timeout 600 python3 "$ALPHAZERO_SCRIPT" --role O --name "$o_name" --simulations 400 --games 1 > /dev/null 2>&1 &
        local o_pid=$!
    else
        timeout 600 ./client_app O "$o_name" "$o_type" 1 > /dev/null 2>&1 &
        local o_pid=$!
    fi
    
    wait $x_pid $o_pid 2>/dev/null
    
    echo "Game $game_num: $x_name(X) vs $o_name(O) completed" >> "$log_file"
}

# 対戦実行関数（1000ゲームずつ白黒入れ替え）
run_matchup() {
    local p1_name=$1
    local p1_type=$2
    local p1_is_az=$3
    local p2_name=$4
    local p2_type=$5
    local p2_is_az=$6
    
    local matchup_log="$RESULT_DIR/${p1_name}_vs_${p2_name}.log"
    local start_time=$(date +%s)
    
    echo "Starting: $p1_name vs $p2_name" | tee -a "$LOG_FILE"
    echo "=== Matchup: $p1_name vs $p2_name ===" > "$matchup_log"
    echo "Started at: $(date)" >> "$matchup_log"
    
    # Phase 1: p1 as X, p2 as O (1000 games)
    echo "Phase 1: $p1_name(X) vs $p2_name(O) - 1000 games" >> "$matchup_log"
    for i in $(seq 1 1000); do
        if [ "$p1_is_az" = "true" ] || [ "$p2_is_az" = "true" ]; then
            run_alphazero_game "$p1_name" "$p1_type" "$p1_is_az" "$p2_name" "$p2_type" "$p2_is_az" "$i" "$matchup_log"
        else
            run_cpp_game "$p1_name" "$p1_type" "$p2_name" "$p2_type" "$i" "$matchup_log"
        fi
        
        if [ $((i % 100)) -eq 0 ]; then
            echo "  Progress: $i/1000 games completed" >> "$matchup_log"
        fi
    done
    
    # Phase 2: p2 as X, p1 as O (1000 games)
    echo "Phase 2: $p2_name(X) vs $p1_name(O) - 1000 games" >> "$matchup_log"
    for i in $(seq 1 1000); do
        if [ "$p1_is_az" = "true" ] || [ "$p2_is_az" = "true" ]; then
            run_alphazero_game "$p2_name" "$p2_type" "$p2_is_az" "$p1_name" "$p1_type" "$p1_is_az" "$((i + 1000))" "$matchup_log"
        else
            run_cpp_game "$p2_name" "$p2_type" "$p1_name" "$p1_type" "$((i + 1000))" "$matchup_log"
        fi
        
        if [ $((i % 100)) -eq 0 ]; then
            echo "  Progress: $((i + 1000))/2000 games completed" >> "$matchup_log"
        fi
    done
    
    local end_time=$(date +%s)
    local duration=$((end_time - start_time))
    
    echo "Completed at: $(date)" >> "$matchup_log"
    echo "Duration: $duration seconds" >> "$matchup_log"
    echo "Completed: $p1_name vs $p2_name ($duration seconds)" | tee -a "$LOG_FILE"
}

# 並行実行用のラッパー
run_matchup_parallel() {
    run_matchup "$@" &
}

echo "" | tee -a "$LOG_FILE"
echo "=== Phase 1: C++ players only (parallel execution) ===" | tee -a "$LOG_FILE"

# C++プレイヤー同士の対戦（並行実行）
# 全組み合わせ（自分自身との対戦も含む）: 5x5 = 25通り
# 最大同時実行数を制限（16並列）
MAX_PARALLEL=16
RUNNING=0

for p1 in "${!PLAYERS[@]}"; do
    for p2 in "${!PLAYERS[@]}"; do
        # 全組み合わせを実行（自分自身との対戦も含む）
        # 同時実行数をチェック
        while [ $RUNNING -ge $MAX_PARALLEL ]; do
            sleep 5
            RUNNING=$(jobs -r | wc -l)
        done
        
        run_matchup "$p1" "${PLAYERS[$p1]}" "false" "$p2" "${PLAYERS[$p2]}" "false" &
        RUNNING=$((RUNNING + 1))
        sleep 2
    done
done

# 全並行ジョブの完了を待つ
echo "Waiting for all C++ matchups to complete..." | tee -a "$LOG_FILE"
wait

echo "" | tee -a "$LOG_FILE"
echo "=== Phase 2: AlphaZero matchups (sequential execution) ===" | tee -a "$LOG_FILE"

# アルファゼロ vs 各C++プレイヤー（順次実行）
for p in "${!PLAYERS[@]}"; do
    run_matchup "alphazero" "" "true" "$p" "${PLAYERS[$p]}" "false"
done

# 各C++プレイヤー vs アルファゼロ（順次実行）
for p in "${!PLAYERS[@]}"; do
    run_matchup "$p" "${PLAYERS[$p]}" "false" "alphazero" "" "true"
done

# アルファゼロ vs アルファゼロ（自分自身）
run_matchup "alphazero" "" "true" "alphazero" "" "true"

# サーバー停止
kill $SERVER_PID 2>/dev/null

echo "" | tee -a "$LOG_FILE"
echo "=== Tournament Completed at $(date) ===" | tee -a "$LOG_FILE"

# 結果集計
echo "=== Generating Summary ===" | tee -a "$LOG_FILE"

cat > "$RESULT_DIR/analyze_results.py" << 'PYTHON_SCRIPT'
#!/usr/bin/env python3
import re
from collections import defaultdict

# game_results.logを解析
results = defaultdict(lambda: {"wins": 0, "losses": 0, "total": 0})
matchups = defaultdict(lambda: defaultdict(lambda: {"as_x": {"wins": 0, "games": 0}, "as_o": {"wins": 0, "games": 0}}))

with open("game_results.log", "r") as f:
    for line in f:
        match = re.search(r"Winner: (\w+) \| X\((\w+)\) vs O\((\w+)\)", line)
        if match:
            winner = match.group(1)
            x_player = match.group(2)
            o_player = match.group(3)
            
            # 総合成績
            if winner == "X":
                results[x_player]["wins"] += 1
                results[x_player]["total"] += 1
                results[o_player]["losses"] += 1
                results[o_player]["total"] += 1
                
                # 対戦成績
                matchups[x_player][o_player]["as_x"]["wins"] += 1
                matchups[x_player][o_player]["as_x"]["games"] += 1
                matchups[o_player][x_player]["as_o"]["games"] += 1
            else:  # O wins
                results[o_player]["wins"] += 1
                results[o_player]["total"] += 1
                results[x_player]["losses"] += 1
                results[x_player]["total"] += 1
                
                # 対戦成績
                matchups[o_player][x_player]["as_o"]["wins"] += 1
                matchups[o_player][x_player]["as_o"]["games"] += 1
                matchups[x_player][o_player]["as_x"]["games"] += 1

# 結果出力
print("=" * 80)
print("TOURNAMENT SUMMARY")
print("=" * 80)
print()
print("Overall Standings:")
print("-" * 80)
print(f"{'Player':<20} {'Wins':<10} {'Losses':<10} {'Total':<10} {'Win Rate':<10}")
print("-" * 80)

sorted_players = sorted(results.keys(), key=lambda p: results[p]["wins"], reverse=True)
for player in sorted_players:
    r = results[player]
    win_rate = (r["wins"] / r["total"] * 100) if r["total"] > 0 else 0
    print(f"{player:<20} {r['wins']:<10} {r['losses']:<10} {r['total']:<10} {win_rate:>6.2f}%")

print()
print("=" * 80)
print("Head-to-Head Results:")
print("=" * 80)

for p1 in sorted(matchups.keys()):
    for p2 in sorted(matchups[p1].keys()):
        data = matchups[p1][p2]
        x_wins = data["as_x"]["wins"]
        x_games = data["as_x"]["games"]
        o_wins = data["as_o"]["wins"]
        o_games = data["as_o"]["games"]
        total_wins = x_wins + o_wins
        total_games = x_games + o_games
        
        if total_games > 0:
            win_rate = (total_wins / total_games * 100)
            print(f"{p1} vs {p2}: {total_wins}/{total_games} ({win_rate:.1f}%) | As X: {x_wins}/{x_games} | As O: {o_wins}/{o_games}")

# CSV出力: 総合成績
print("\n" + "=" * 80)
print("Saving results to CSV files...")
print("=" * 80)

with open("standings.csv", "w") as f:
    f.write("Player,Wins,Losses,Total,WinRate\n")
    for player in sorted_players:
        r = results[player]
        win_rate = (r["wins"] / r["total"] * 100) if r["total"] > 0 else 0
        f.write(f"{player},{r['wins']},{r['losses']},{r['total']},{win_rate:.2f}\n")

# CSV出力: 勝率マトリクス（各プレイヤー同士の対戦成績）
all_players = sorted(set(results.keys()))
with open("winrate_matrix.csv", "w") as f:
    # ヘッダー
    f.write("Player," + ",".join(all_players) + "\n")
    
    # 各行
    for p1 in all_players:
        row = [p1]
        for p2 in all_players:
            if p2 in matchups[p1]:
                data = matchups[p1][p2]
                total_wins = data["as_x"]["wins"] + data["as_o"]["wins"]
                total_games = data["as_x"]["games"] + data["as_o"]["games"]
                win_rate = (total_wins / total_games * 100) if total_games > 0 else 0
                row.append(f"{win_rate:.2f}")
            else:
                row.append("0.00")
        f.write(",".join(row) + "\n")

# CSV出力: 詳細対戦成績
with open("head_to_head.csv", "w") as f:
    f.write("Player1,Player2,TotalWins,TotalGames,WinRate,AsX_Wins,AsX_Games,AsO_Wins,AsO_Games\n")
    for p1 in sorted(matchups.keys()):
        for p2 in sorted(matchups[p1].keys()):
            data = matchups[p1][p2]
            x_wins = data["as_x"]["wins"]
            x_games = data["as_x"]["games"]
            o_wins = data["as_o"]["wins"]
            o_games = data["as_o"]["games"]
            total_wins = x_wins + o_wins
            total_games = x_games + o_games
            win_rate = (total_wins / total_games * 100) if total_games > 0 else 0
            f.write(f"{p1},{p2},{total_wins},{total_games},{win_rate:.2f},{x_wins},{x_games},{o_wins},{o_games}\n")

print("Created: standings.csv (overall standings)")
print("Created: winrate_matrix.csv (win rate matrix)")
print("Created: head_to_head.csv (detailed matchup results)")


PYTHON_SCRIPT

chmod +x "$RESULT_DIR/analyze_results.py"

# 結果解析実行
cd "$RESULT_DIR"
python3 analyze_results.py > summary.txt
cd ..

echo "" | tee -a "$LOG_FILE"
cat "$SUMMARY_FILE" | tee -a "$LOG_FILE"

echo "" | tee -a "$LOG_FILE"
echo "Results saved to: $RESULT_DIR" | tee -a "$LOG_FILE"
echo "Summary: $SUMMARY_FILE" | tee -a "$LOG_FILE"
echo "Server log: $RESULT_DIR/server.log" | tee -a "$LOG_FILE"
