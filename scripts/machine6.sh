#!/usr/bin/env bash
# Machine6 assigned matches
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
LOGDIR="${SCRIPT_DIR}/../logs/machine6"
PORT=${PORT:-18886}
GAMES=${1:-1000}
mkdir -p "$LOGDIR"

"${SCRIPT_DIR}/../server_app" --port "$PORT" > "$LOGDIR/server.log" 2>&1 &
SERVER_PID=$!
export CONTRAST_SERVER_PORT=$PORT
sleep 0.5

echo "alphazero vs rulebased2"
python3 "${SCRIPT_DIR}/../client/python_alphazero_bot.py" --host 127.0.0.1 --port "$PORT" --role X --name az_m6 --games "$GAMES" > "$LOGDIR/alphazero_vs_rulebased2_X.log" 2>&1 &
"${SCRIPT_DIR}/../client_app" O rulebased2_O4 rulebased2 "$GAMES" > "$LOGDIR/alphazero_vs_rulebased2_O.log" 2>&1 &

echo "mcts vs alphabeta"
"${SCRIPT_DIR}/../client_app" X mcts_X2 mcts "$GAMES" > "$LOGDIR/mcts_vs_alphabeta_X.log" 2>&1 &
"${SCRIPT_DIR}/../client_app" O alphabeta_O4 alphabeta "$GAMES" > "$LOGDIR/mcts_vs_alphabeta_O.log" 2>&1 &

echo "rulebased2 vs alphabeta"
"${SCRIPT_DIR}/../client_app" X rulebased2_X2 rulebased2 "$GAMES" > "$LOGDIR/rulebased2_vs_alphabeta_X.log" 2>&1 &
"${SCRIPT_DIR}/../client_app" O alphabeta_O5 alphabeta "$GAMES" > "$LOGDIR/rulebased2_vs_alphabeta_O.log" 2>&1 &

wait || true
kill $SERVER_PID || true
wait $SERVER_PID 2>/dev/null || true
echo "machine6 done"
