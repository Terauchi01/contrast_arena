#!/usr/bin/env bash
# Machine1 assigned matches
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
LOGDIR="${SCRIPT_DIR}/../logs/machine1"
PORT=${PORT:-18881}
GAMES=${1:-1000}
mkdir -p "$LOGDIR"

echo "Starting server on port $PORT for machine1"
"${SCRIPT_DIR}/../server_app" --port "$PORT" > "$LOGDIR/server.log" 2>&1 &
SERVER_PID=$!
export CONTRAST_SERVER_PORT=$PORT
sleep 0.5

sleep 0.5
# Matches assigned to machine1:
# alphabeta vs alphazero (handled on machine1? mapping shows alphabeta alphazero -> machine1)
echo "Starting alphabeta (X) vs alphazero (O)"
"${SCRIPT_DIR}/../client_app" X alphabeta_X alphabeta "$GAMES" > "$LOGDIR/alphabeta_vs_alphazero_X.log" 2>&1 &
CLIENT1=$!
python3 "${SCRIPT_DIR}/../client/python_alphazero_bot.py" --host 127.0.0.1 --port "$PORT" --role O --name alphazero_O --games "$GAMES" > "$LOGDIR/alphabeta_vs_alphazero_O.log" 2>&1 &
CLIENT2=$!

# mcts vs rulebased2 assigned to machine1
echo "Starting mcts (X) vs rulebased2 (O)"
"${SCRIPT_DIR}/../client_app" X mcts_X mcts "$GAMES" > "$LOGDIR/mcts_vs_rulebased2_X.log" 2>&1 &
PID_A=$!
"${SCRIPT_DIR}/../client_app" O rulebased2_O rulebased2 "$GAMES" > "$LOGDIR/mcts_vs_rulebased2_O.log" 2>&1 &
PID_B=$!

# rulebased2 vs alphazero -> machine1
echo "Starting rulebased2 (X) vs alphazero (O)"
"${SCRIPT_DIR}/../client_app" X rulebased2_X rulebased2 "$GAMES" > "$LOGDIR/rulebased2_vs_alphazero_X.log" 2>&1 &
PID_C=$!
python3 "${SCRIPT_DIR}/../client/python_alphazero_bot.py" --host 127.0.0.1 --port "$PORT" --role O --name alphazero_O2 --games "$GAMES" > "$LOGDIR/rulebased2_vs_alphazero_O.log" 2>&1 &
PID_D=$!

echo "Waiting for clients to finish on machine1..."
wait $CLIENT1 $CLIENT2 $PID_A $PID_B $PID_C $PID_D || true

echo "Stopping server (PID $SERVER_PID)"
kill $SERVER_PID || true
wait $SERVER_PID 2>/dev/null || true

echo "machine1 done"
