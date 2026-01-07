#!/usr/bin/env bash
# Machine4 assigned matches
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
LOGDIR="${SCRIPT_DIR}/../logs/machine4"
PORT=${PORT:-18884}
GAMES=${1:-1000}
mkdir -p "$LOGDIR"

"${SCRIPT_DIR}/../server_app" --port "$PORT" > "$LOGDIR/server.log" 2>&1 &
SERVER_PID=$!
export CONTRAST_SERVER_PORT=$PORT
sleep 0.5

echo "alphabeta vs ntuple"
"${SCRIPT_DIR}/../client_app" X alphabeta_X2 alphabeta "$GAMES" > "$LOGDIR/alphabeta_vs_ntuple_X.log" 2>&1 &
"${SCRIPT_DIR}/../client_app" O ntuple_O ntuple "$GAMES" > "$LOGDIR/alphabeta_vs_ntuple_O.log" 2>&1 &

echo "alphazero vs mcts"
python3 "${SCRIPT_DIR}/../client/python_alphazero_bot.py" --host 127.0.0.1 --port "$PORT" --role X --name az_m4 --games "$GAMES" > "$LOGDIR/alphazero_vs_mcts_X.log" 2>&1 &
"${SCRIPT_DIR}/../client_app" O mcts_O3 mcts "$GAMES" > "$LOGDIR/alphazero_vs_mcts_O.log" 2>&1 &

echo "ntuple vs ntuple"
"${SCRIPT_DIR}/../client_app" X ntuple_A ntuple "$GAMES" > "$LOGDIR/ntuple_vs_ntuple_X.log" 2>&1 &
"${SCRIPT_DIR}/../client_app" O ntuple_B ntuple "$GAMES" > "$LOGDIR/ntuple_vs_ntuple_O.log" 2>&1 &

wait || true
kill $SERVER_PID || true
wait $SERVER_PID 2>/dev/null || true
echo "machine4 done"
