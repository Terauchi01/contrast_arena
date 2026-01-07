#!/usr/bin/env bash
# Machine7 assigned matches
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
LOGDIR="${SCRIPT_DIR}/../logs/machine7"
PORT=${PORT:-18887}
GAMES=${1:-1000}
mkdir -p "$LOGDIR"

"${SCRIPT_DIR}/../server_app" --port "$PORT" > "$LOGDIR/server.log" 2>&1 &
SERVER_PID=$!
export CONTRAST_SERVER_PORT=$PORT
sleep 0.5

echo "mcts vs alphazero"
"${SCRIPT_DIR}/../client_app" X mcts_X3 mcts "$GAMES" > "$LOGDIR/mcts_vs_alphazero_X.log" 2>&1 &
python3 "${SCRIPT_DIR}/../client/python_alphazero_bot.py" --host 127.0.0.1 --port "$PORT" --role O --name az_m7 --games "$GAMES" > "$LOGDIR/mcts_vs_alphazero_O.log" 2>&1 &

echo "mcts vs mcts"
"${SCRIPT_DIR}/../client_app" X mcts_A mcts "$GAMES" > "$LOGDIR/mcts_vs_mcts_X.log" 2>&1 &
"${SCRIPT_DIR}/../client_app" O mcts_B mcts "$GAMES" > "$LOGDIR/mcts_vs_mcts_O.log" 2>&1 &

echo "rulebased2 vs mcts"
"${SCRIPT_DIR}/../client_app" X rulebased2_X3 rulebased2 "$GAMES" > "$LOGDIR/rulebased2_vs_mcts_X.log" 2>&1 &
"${SCRIPT_DIR}/../client_app" O mcts_O4 mcts "$GAMES" > "$LOGDIR/rulebased2_vs_mcts_O.log" 2>&1 &

wait || true
kill $SERVER_PID || true
wait $SERVER_PID 2>/dev/null || true
echo "machine7 done"
