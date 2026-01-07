#!/usr/bin/env bash
# Machine5 assigned matches
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
LOGDIR="${SCRIPT_DIR}/../logs/machine5"
PORT=${PORT:-18885}
GAMES=${1:-1000}
mkdir -p "$LOGDIR"

"${SCRIPT_DIR}/../server_app" --port "$PORT" > "$LOGDIR/server.log" 2>&1 &
SERVER_PID=$!
export CONTRAST_SERVER_PORT=$PORT
sleep 0.5

echo "alphabeta vs rulebased2"
"${SCRIPT_DIR}/../client_app" X alphabeta_X3 alphabeta "$GAMES" > "$LOGDIR/alphabeta_vs_rulebased2_X.log" 2>&1 &
"${SCRIPT_DIR}/../client_app" O rulebased2_O2 rulebased2 "$GAMES" > "$LOGDIR/alphabeta_vs_rulebased2_O.log" 2>&1 &

echo "alphazero vs ntuple"
python3 "${SCRIPT_DIR}/../client/python_alphazero_bot.py" --host 127.0.0.1 --port "$PORT" --role X --name az_m5 --games "$GAMES" > "$LOGDIR/alphazero_vs_ntuple_X.log" 2>&1 &
"${SCRIPT_DIR}/../client_app" O ntuple_O2 ntuple "$GAMES" > "$LOGDIR/alphazero_vs_ntuple_O.log" 2>&1 &

echo "ntuple vs rulebased2"
"${SCRIPT_DIR}/../client_app" X ntuple_X3 ntuple "$GAMES" > "$LOGDIR/ntuple_vs_rulebased2_X.log" 2>&1 &
"${SCRIPT_DIR}/../client_app" O rulebased2_O3 rulebased2 "$GAMES" > "$LOGDIR/ntuple_vs_rulebased2_O.log" 2>&1 &

wait || true
kill $SERVER_PID || true
wait $SERVER_PID 2>/dev/null || true
echo "machine5 done"
