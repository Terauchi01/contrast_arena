#!/usr/bin/env bash
# Machine8 assigned matches
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
LOGDIR="${SCRIPT_DIR}/../logs/machine8"
PORT=${PORT:-18888}
GAMES=${1:-1000}
mkdir -p "$LOGDIR"

"${SCRIPT_DIR}/../server_app" --port "$PORT" > "$LOGDIR/server.log" 2>&1 &
SERVER_PID=$!
export CONTRAST_SERVER_PORT=$PORT
sleep 0.5

echo "mcts vs ntuple"
"${SCRIPT_DIR}/../client_app" X mcts_X4 mcts "$GAMES" > "$LOGDIR/mcts_vs_ntuple_X.log" 2>&1 &
"${SCRIPT_DIR}/../client_app" O ntuple_O4 ntuple "$GAMES" > "$LOGDIR/mcts_vs_ntuple_O.log" 2>&1 &

echo "ntuple vs alphazero"
"${SCRIPT_DIR}/../client_app" X ntuple_X4 ntuple "$GAMES" > "$LOGDIR/ntuple_vs_alphazero_X.log" 2>&1 &
python3 "${SCRIPT_DIR}/../client/python_alphazero_bot.py" --host 127.0.0.1 --port "$PORT" --role O --name az_m8 --games "$GAMES" > "$LOGDIR/ntuple_vs_alphazero_O.log" 2>&1 &

echo "ntuple vs rulebased2"
"${SCRIPT_DIR}/../client_app" X ntuple_X5 ntuple "$GAMES" > "$LOGDIR/ntuple_vs_rulebased2_X.log" 2>&1 &
"${SCRIPT_DIR}/../client_app" O rulebased2_O5 rulebased2 "$GAMES" > "$LOGDIR/ntuple_vs_rulebased2_O.log" 2>&1 &

wait || true
kill $SERVER_PID || true
wait $SERVER_PID 2>/dev/null || true
echo "machine8 done"
