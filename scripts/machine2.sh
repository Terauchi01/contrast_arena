#!/usr/bin/env bash
# Machine2 assigned matches
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
LOGDIR="${SCRIPT_DIR}/../logs/machine2"
PORT=${PORT:-18882}
GAMES=${1:-1000}
mkdir -p "$LOGDIR"

echo "Starting server on port $PORT for machine2"
"${SCRIPT_DIR}/../server_app" --port "$PORT" > "$LOGDIR/server.log" 2>&1 &
SERVER_PID=$!
export CONTRAST_SERVER_PORT=$PORT
sleep 0.5

echo "Starting alphabeta vs alphabeta"
"${SCRIPT_DIR}/../client_app" X alphabeta_A alphabeta "$GAMES" > "$LOGDIR/alphabeta_vs_alphabeta_X.log" 2>&1 &
"${SCRIPT_DIR}/../client_app" O alphabeta_B alphabeta "$GAMES" > "$LOGDIR/alphabeta_vs_alphabeta_O.log" 2>&1 &

echo "Starting alphazero vs alphabeta"
python3 "${SCRIPT_DIR}/../client/python_alphazero_bot.py" --host 127.0.0.1 --port "$PORT" --role X --name alphazero_X --games "$GAMES" > "$LOGDIR/alphazero_vs_alphabeta_X.log" 2>&1 &
python3 "${SCRIPT_DIR}/../client/python_alphazero_bot.py" --host 127.0.0.1 --port "$PORT" --role O --name alphabeta_O --games "$GAMES" > "$LOGDIR/alphazero_vs_alphabeta_O.log" 2>&1 &

echo "Starting ntuple vs alphabeta"
"${SCRIPT_DIR}/../client_app" X ntuple_X ntuple "$GAMES" > "$LOGDIR/ntuple_vs_alphabeta_X.log" 2>&1 &
"${SCRIPT_DIR}/../client_app" O alphabeta_O2 alphabeta "$GAMES" > "$LOGDIR/ntuple_vs_alphabeta_O.log" 2>&1 &

echo "Starting rulebased2 vs rulebased2"
"${SCRIPT_DIR}/../client_app" X rulebased2_A rulebased2 "$GAMES" > "$LOGDIR/rulebased2_vs_rulebased2_X.log" 2>&1 &
"${SCRIPT_DIR}/../client_app" O rulebased2_B rulebased2 "$GAMES" > "$LOGDIR/rulebased2_vs_rulebased2_O.log" 2>&1 &

wait || true

kill $SERVER_PID || true
wait $SERVER_PID 2>/dev/null || true
echo "machine2 done"
