#!/bin/bash
# Tournament: All players round-robin
# Each matchup: 1000 games as X, 1000 games as O (2000 games total)

# デバッグ/動作確認用の上書きパラメータ
# 例: GAMES_PER_SIDE=1 MAX_PARALLEL=1 ./run_tournament.sh
GAMES_PER_SIDE=${GAMES_PER_SIDE:-1000}
MAX_PARALLEL=${MAX_PARALLEL:-16}
CLIENT_TIMEOUT_SEC=${CLIENT_TIMEOUT_SEC:-300}
AZ_TIMEOUT_SEC=${AZ_TIMEOUT_SEC:-600}

# 並列化設定
# - server_app は「1サーバ=同時に1試合」前提。
#   そのため並列に回したい場合は server_app を複数立ち上げ、各試合を別ポートに割り当てる。
# - MULTISERVER_COUNT=1 のときは従来どおり単一サーバ（8765）で順次実行。
MULTISERVER_COUNT=${MULTISERVER_COUNT:-1}
BASE_PORT=${BASE_PORT:-18765}

# ルートディレクトリ（スクリプトの場所）
ROOT_DIR=$(cd "$(dirname "$0")" && pwd)

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
ALPHAZERO_SCRIPT="$ROOT_DIR/client/python_alphazero_bot.py"

# 結果ディレクトリ
RESULT_DIR="$ROOT_DIR/tournament_results_$(date +%Y%m%d_%H%M%S)"
mkdir -p "$RESULT_DIR"

# server_appのログは盤面表示で肥大化しやすいのでデフォルトで捨てる
# - SERVER_LOG_MODE=none (default): /dev/null
# - SERVER_LOG_MODE=file: RESULT_DIR/server.log または SERVER_LOG_PATH
# - SERVER_LOG_MODE=stdout: 端末へ出す
SERVER_LOG_MODE=${SERVER_LOG_MODE:-none}
SERVER_LOG_PATH=${SERVER_LOG_PATH:-"$RESULT_DIR/server.log"}

LOG_FILE="$RESULT_DIR/tournament.log"
SUMMARY_FILE="$RESULT_DIR/summary.txt"

echo "=== Tournament Started at $(date) ===" | tee -a "$LOG_FILE"
echo "Players: random, rulebased2, ntuple, alphabeta(depth=3), mcts(400), alphazero(400)" | tee -a "$LOG_FILE"
echo "Format: Full round-robin (all combinations including self-play)" | tee -a "$LOG_FILE"
TOTAL_PLAYERS=6
TOTAL_MATCHUPS=$((TOTAL_PLAYERS * TOTAL_PLAYERS))
GAMES_PER_MATCHUP=$((GAMES_PER_SIDE * 2))
TOTAL_GAMES=$((TOTAL_MATCHUPS * GAMES_PER_MATCHUP))
echo "Total matchups: ${TOTAL_PLAYERS}x${TOTAL_PLAYERS} = ${TOTAL_MATCHUPS} matchups" | tee -a "$LOG_FILE"
echo "Games per matchup: ${GAMES_PER_MATCHUP} (${GAMES_PER_SIDE} as X, ${GAMES_PER_SIDE} as O)" | tee -a "$LOG_FILE"
echo "Total games: ${TOTAL_GAMES}" | tee -a "$LOG_FILE"
echo "Results directory: $RESULT_DIR" | tee -a "$LOG_FILE"
echo "Multi-server: ${MULTISERVER_COUNT} (BASE_PORT=${BASE_PORT})" | tee -a "$LOG_FILE"
if [ "$MULTISERVER_COUNT" -gt 1 ]; then
    echo "Parallel matchups: min(MAX_PARALLEL=${MAX_PARALLEL}, MULTISERVER_COUNT=${MULTISERVER_COUNT})" | tee -a "$LOG_FILE"
else
    echo "Parallel matchups: disabled (single server mode)" | tee -a "$LOG_FILE"
fi
echo "" | tee -a "$LOG_FILE"

# サーバー起動（単一 or 複数）
pkill -9 server_app client_app python 2>/dev/null
sleep 2

declare -a SERVER_PIDS
declare -a SERVER_PORTS
declare -a SERVER_DIRS

start_one_server() {
    local port="$1"
    local dir="$2"
    mkdir -p "$dir"

    pushd "$dir" > /dev/null
    case "$SERVER_LOG_MODE" in
        file)
            if [ "$dir" = "$RESULT_DIR" ]; then
                env CONTRAST_SERVER_PORT="$port" "$ROOT_DIR/server_app" > "$SERVER_LOG_PATH" 2>&1 &
            else
                env CONTRAST_SERVER_PORT="$port" "$ROOT_DIR/server_app" > "$dir/server.log" 2>&1 &
            fi
            ;;
        stdout)
            env CONTRAST_SERVER_PORT="$port" "$ROOT_DIR/server_app" &
            ;;
        none|*)
            env CONTRAST_SERVER_PORT="$port" "$ROOT_DIR/server_app" > /dev/null 2>&1 &
            ;;
    esac
    local pid=$!
    popd > /dev/null

    # bind 失敗等で即死していないかを軽く確認
    sleep 0.2
    if ! kill -0 "$pid" 2>/dev/null; then
        echo "Failed to start server on port ${port} (process exited)." | tee -a "$LOG_FILE"
        exit 1
    fi

    SERVER_PIDS+=("$pid")
    SERVER_PORTS+=("$port")
    SERVER_DIRS+=("$dir")
}

if [ "$MULTISERVER_COUNT" -le 1 ]; then
    # 従来: 8765 単一サーバ
    start_one_server 8765 "$RESULT_DIR"
else
    # マルチサーバ: BASE_PORT..BASE_PORT+N-1
    for i in $(seq 0 $((MULTISERVER_COUNT - 1))); do
        port=$((BASE_PORT + i))
        start_one_server "$port" "$RESULT_DIR/server_${port}"
    done
fi

sleep 3

cleanup() {
    for pid in "${SERVER_PIDS[@]}"; do
        kill "$pid" 2>/dev/null || true
    done
    pkill -9 client_app python 2>/dev/null || true
}
trap cleanup EXIT INT TERM

echo "Servers started: ${#SERVER_PIDS[@]}" | tee -a "$LOG_FILE"

# 1ゲーム実行関数（C++プレイヤー）
run_cpp_game() {
    local x_name=$1
    local x_type=$2
    local o_name=$3
    local o_type=$4
    local game_num=$5
    local log_file=$6
    local server_port=$7
    
    env CONTRAST_SERVER_PORT="$server_port" timeout "$CLIENT_TIMEOUT_SEC" "$ROOT_DIR/client_app" X "$x_name" "$x_type" 1 > /dev/null 2>&1 &
    local x_pid=$!
    sleep 0.5
    env CONTRAST_SERVER_PORT="$server_port" timeout "$CLIENT_TIMEOUT_SEC" "$ROOT_DIR/client_app" O "$o_name" "$o_type" 1 > /dev/null 2>&1 &
    local o_pid=$!
    
    wait $x_pid $o_pid 2>/dev/null

    # 次のゲーム開始前にサーバー側の切断処理が追いつくのを待つ
    sleep 0.2
    
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
    local server_port=$9
    
    if [ "$x_is_az" = "true" ]; then
        timeout "$AZ_TIMEOUT_SEC" python3 "$ALPHAZERO_SCRIPT" --role X --name "$x_name" --simulations 400 --games 1 --port "$server_port" > /dev/null 2>&1 &
        local x_pid=$!
    else
        env CONTRAST_SERVER_PORT="$server_port" timeout "$CLIENT_TIMEOUT_SEC" "$ROOT_DIR/client_app" X "$x_name" "$x_type" 1 > /dev/null 2>&1 &
        local x_pid=$!
    fi
    
    sleep 1
    
    if [ "$o_is_az" = "true" ]; then
        timeout "$AZ_TIMEOUT_SEC" python3 "$ALPHAZERO_SCRIPT" --role O --name "$o_name" --simulations 400 --games 1 --port "$server_port" > /dev/null 2>&1 &
        local o_pid=$!
    else
        env CONTRAST_SERVER_PORT="$server_port" timeout "$CLIENT_TIMEOUT_SEC" "$ROOT_DIR/client_app" O "$o_name" "$o_type" 1 > /dev/null 2>&1 &
        local o_pid=$!
    fi
    
    wait $x_pid $o_pid 2>/dev/null

    # 次のゲーム開始前にサーバー側の切断処理が追いつくのを待つ
    sleep 0.2
    
    echo "Game $game_num: $x_name(X) vs $o_name(O) completed" >> "$log_file"
}

# 対戦実行関数（1000ゲームずつ白黒入れ替え）
run_matchup_on_port() {
    local server_port=$1
    shift
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
    
    # Phase 1: p1 as X, p2 as O
    echo "Phase 1: $p1_name(X) vs $p2_name(O) - ${GAMES_PER_SIDE} games" >> "$matchup_log"
    for i in $(seq 1 "$GAMES_PER_SIDE"); do
        if [ "$p1_is_az" = "true" ] || [ "$p2_is_az" = "true" ]; then
            run_alphazero_game "$p1_name" "$p1_type" "$p1_is_az" "$p2_name" "$p2_type" "$p2_is_az" "$i" "$matchup_log" "$server_port"
        else
            run_cpp_game "$p1_name" "$p1_type" "$p2_name" "$p2_type" "$i" "$matchup_log" "$server_port"
        fi
        
        if [ $((i % 100)) -eq 0 ]; then
            echo "  Progress: $i/${GAMES_PER_SIDE} games completed" >> "$matchup_log"
        fi
    done
    
    # Phase 2: p2 as X, p1 as O
    echo "Phase 2: $p2_name(X) vs $p1_name(O) - ${GAMES_PER_SIDE} games" >> "$matchup_log"
    for i in $(seq 1 "$GAMES_PER_SIDE"); do
        if [ "$p1_is_az" = "true" ] || [ "$p2_is_az" = "true" ]; then
            run_alphazero_game "$p2_name" "$p2_type" "$p2_is_az" "$p1_name" "$p1_type" "$p1_is_az" "$((i + GAMES_PER_SIDE))" "$matchup_log" "$server_port"
        else
            run_cpp_game "$p2_name" "$p2_type" "$p1_name" "$p1_type" "$((i + GAMES_PER_SIDE))" "$matchup_log" "$server_port"
        fi
        
        if [ $((i % 100)) -eq 0 ]; then
            echo "  Progress: $((i + GAMES_PER_SIDE))/$((GAMES_PER_SIDE * 2)) games completed" >> "$matchup_log"
        fi
    done
    
    local end_time=$(date +%s)
    local duration=$((end_time - start_time))
    
    echo "Completed at: $(date)" >> "$matchup_log"
    echo "Duration: $duration seconds" >> "$matchup_log"
    echo "Completed: $p1_name vs $p2_name ($duration seconds)" | tee -a "$LOG_FILE"
}


# マルチサーバ時: マッチアップをサーバ（ポート）に割り当てて並列実行
declare -a AVAILABLE_PORTS
declare -a RUNNING_PIDS
declare -A JOB_PORT

AVAILABLE_PORTS=("${SERVER_PORTS[@]}")

reap_finished_jobs() {
    local still_running=()
    for pid in "${RUNNING_PIDS[@]}"; do
        if kill -0 "$pid" 2>/dev/null; then
            still_running+=("$pid")
        else
            wait "$pid" 2>/dev/null || true
            AVAILABLE_PORTS+=("${JOB_PORT[$pid]}")
            unset JOB_PORT[$pid]
        fi
    done
    RUNNING_PIDS=("${still_running[@]}")
}

wait_for_slot() {
    local effective_parallel
    if [ "$MAX_PARALLEL" -lt "${#SERVER_PORTS[@]}" ]; then
        effective_parallel="$MAX_PARALLEL"
    else
        effective_parallel="${#SERVER_PORTS[@]}"
    fi

    while [ "${#AVAILABLE_PORTS[@]}" -eq 0 ] || [ "${#RUNNING_PIDS[@]}" -ge "$effective_parallel" ]; do
        reap_finished_jobs
        sleep 0.2
    done
}

start_matchup_job() {
    local port="$1"
    shift
    run_matchup_on_port "$port" "$@" &
    local pid=$!
    RUNNING_PIDS+=("$pid")
    JOB_PORT[$pid]="$port"
}

echo "" | tee -a "$LOG_FILE"
echo "=== Phase 1: C++ players only ===" | tee -a "$LOG_FILE"

# 全組み合わせ（自分自身との対戦も含む）: 5x5 = 25通り
for p1 in "${!PLAYERS[@]}"; do
    for p2 in "${!PLAYERS[@]}"; do
        if [ "$MULTISERVER_COUNT" -le 1 ]; then
            run_matchup_on_port 8765 "$p1" "${PLAYERS[$p1]}" "false" "$p2" "${PLAYERS[$p2]}" "false"
        else
            wait_for_slot
            port="${AVAILABLE_PORTS[0]}"
            AVAILABLE_PORTS=("${AVAILABLE_PORTS[@]:1}")
            start_matchup_job "$port" "$p1" "${PLAYERS[$p1]}" "false" "$p2" "${PLAYERS[$p2]}" "false"
        fi
    done
done

echo "" | tee -a "$LOG_FILE"
echo "=== Phase 2: AlphaZero matchups ===" | tee -a "$LOG_FILE"

# アルファゼロ vs 各C++プレイヤー
for p in "${!PLAYERS[@]}"; do
    if [ "$MULTISERVER_COUNT" -le 1 ]; then
        run_matchup_on_port 8765 "alphazero" "" "true" "$p" "${PLAYERS[$p]}" "false"
    else
        wait_for_slot
        port="${AVAILABLE_PORTS[0]}"
        AVAILABLE_PORTS=("${AVAILABLE_PORTS[@]:1}")
        start_matchup_job "$port" "alphazero" "" "true" "$p" "${PLAYERS[$p]}" "false"
    fi
done

# 各C++プレイヤー vs アルファゼロ
for p in "${!PLAYERS[@]}"; do
    if [ "$MULTISERVER_COUNT" -le 1 ]; then
        run_matchup_on_port 8765 "$p" "${PLAYERS[$p]}" "false" "alphazero" "" "true"
    else
        wait_for_slot
        port="${AVAILABLE_PORTS[0]}"
        AVAILABLE_PORTS=("${AVAILABLE_PORTS[@]:1}")
        start_matchup_job "$port" "$p" "${PLAYERS[$p]}" "false" "alphazero" "" "true"
    fi
done

# アルファゼロ vs アルファゼロ（自分自身）
if [ "$MULTISERVER_COUNT" -le 1 ]; then
    run_matchup_on_port 8765 "alphazero" "" "true" "alphazero" "" "true"
else
    wait_for_slot
    port="${AVAILABLE_PORTS[0]}"
    AVAILABLE_PORTS=("${AVAILABLE_PORTS[@]:1}")
    start_matchup_job "$port" "alphazero" "" "true" "alphazero" "" "true"
fi

# 並列ジョブの完了待ち
while [ "${#RUNNING_PIDS[@]}" -gt 0 ]; do
    reap_finished_jobs
    sleep 0.2
done

# サーバー停止
for pid in "${SERVER_PIDS[@]}"; do
    kill "$pid" 2>/dev/null || true
done

# マルチサーバ時は各サーバの game_results.log を結合して集計に回す
if [ "$MULTISERVER_COUNT" -gt 1 ]; then
    : > "$RESULT_DIR/game_results.log"
    for dir in "${SERVER_DIRS[@]}"; do
        if [ -f "$dir/game_results.log" ]; then
            cat "$dir/game_results.log" >> "$RESULT_DIR/game_results.log"
        fi
    done
fi

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
# CSV出力: 総当たりの表（セルに wins/total (winrate%) を入れる）
with open("round_robin_table.csv", "w") as f:
    f.write("Player," + ",".join(all_players) + "\n")
    for p1 in all_players:
        row = [p1]
        for p2 in all_players:
            if p1 == p2:
                row.append("-")
                continue
            if p2 in matchups[p1]:
                data = matchups[p1][p2]
                total_wins = data["as_x"]["wins"] + data["as_o"]["wins"]
                total_games = data["as_x"]["games"] + data["as_o"]["games"]
                win_rate = (total_wins / total_games * 100) if total_games > 0 else 0
                row.append(f"{total_wins}/{total_games} ({win_rate:.1f}%)")
            else:
                row.append("0/0 (0.0%)")
        f.write(",".join(row) + "\n")
print("Created: standings.csv (overall standings)")
print("Created: winrate_matrix.csv (win rate matrix)")
print("Created: head_to_head.csv (detailed matchup results)")
print("Created: round_robin_table.csv (wins/total table)")

# CSV出力: 総当たりの表（セルに左側プレイヤの勝利数だけを入れる）
with open("round_robin_wins.csv", "w") as f:
    f.write("Player," + ",".join(all_players) + "\n")
    for p1 in all_players:
        row = [p1]
        for p2 in all_players:
            if p1 == p2:
                row.append("-")
                continue
            if p2 in matchups[p1]:
                data = matchups[p1][p2]
                total_wins = data["as_x"]["wins"] + data["as_o"]["wins"]
                row.append(str(total_wins))
            else:
                row.append("0")
        f.write(",".join(row) + "\n")
print("Created: round_robin_wins.csv (wins-only matrix)")


# CSV出力: 総当たりの表（左側プレイヤがX手番のときの勝利数）
with open("round_robin_wins_as_x.csv", "w") as f:
    f.write("Player," + ",".join(all_players) + "\n")
    for p1 in all_players:
        row = [p1]
        for p2 in all_players:
            if p1 == p2:
                row.append("-")
                continue
            if p2 in matchups[p1]:
                data = matchups[p1][p2]
                row.append(str(data["as_x"]["wins"]))
            else:
                row.append("0")
        f.write(",".join(row) + "\n")
print("Created: round_robin_wins_as_x.csv (wins as X matrix)")


# CSV出力: 総当たりの表（左側プレイヤがO手番のときの勝利数）
with open("round_robin_wins_as_o.csv", "w") as f:
    f.write("Player," + ",".join(all_players) + "\n")
    for p1 in all_players:
        row = [p1]
        for p2 in all_players:
            if p1 == p2:
                row.append("-")
                continue
            if p2 in matchups[p1]:
                data = matchups[p1][p2]
                row.append(str(data["as_o"]["wins"]))
            else:
                row.append("0")
        f.write(",".join(row) + "\n")
print("Created: round_robin_wins_as_o.csv (wins as O matrix)")


# CSV出力: 総当たりの表（左=常にX側固定。セルに X勝ち数/対局数 を入れる）
# これがあれば X側勝率 = Xwins/Xgames, O側勝率 = 1 - Xwins/Xgames を計算できる
with open("round_robin_x_fixed.csv", "w") as f:
    f.write("X\\O," + ",".join(all_players) + "\n")
    for x_player in all_players:
        row = [x_player]
        for o_player in all_players:
            if x_player == o_player:
                row.append("-")
                continue
            if o_player in matchups[x_player]:
                data = matchups[x_player][o_player]
                x_wins = data["as_x"]["wins"]
                x_games = data["as_x"]["games"]
                row.append(f"{x_wins}/{x_games}")
            else:
                row.append("0/0")
        f.write(",".join(row) + "\n")
print("Created: round_robin_x_fixed.csv (X-fixed Xwins/Xgames table)")


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
if [ "$SERVER_LOG_MODE" = "file" ]; then
    if [ "$MULTISERVER_COUNT" -le 1 ]; then
        echo "Server log: $SERVER_LOG_PATH" | tee -a "$LOG_FILE"
    else
        echo "Server logs: $RESULT_DIR/server_*/server.log" | tee -a "$LOG_FILE"
    fi
else
    echo "Server log: (disabled; set SERVER_LOG_MODE=file to enable)" | tee -a "$LOG_FILE"
fi
