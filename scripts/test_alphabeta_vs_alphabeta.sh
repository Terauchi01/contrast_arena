#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

SERVER_APP="${ROOT_DIR}/server_app"
CLIENT_APP="${ROOT_DIR}/client_app"

PORT=${PORT:-18950}
GAMES=${1:-100}
LOGDIR=${LOGDIR:-"${ROOT_DIR}/logs/test_alphabeta_vs_alphabeta"}
mkdir -p "$LOGDIR"

PID_DIR="${ROOT_DIR}/pids"
mkdir -p "$PID_DIR"
PID_FILES=()

echo "Cleaning existing server/client processes started by previous runs (only recorded PIDs)"
for f in "$PID_DIR"/server_test_*.pid "$PID_DIR"/client_test_*.pid; do
  [ -e "$f" ] || continue
  pid=$(cat "$f" 2>/dev/null || true)
  if [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null; then
    echo "Killing previous process $pid (from $f)"
    kill "$pid" 2>/dev/null || true
  fi
  rm -f "$f" || true
done

cleanup() {
  echo "Cleaning up recorded processes and pid files"
  for f in "${PID_FILES[@]}"; do
    if [ -f "$f" ]; then
      pid=$(cat "$f" 2>/dev/null || true)
      if [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null; then
        echo "Killing $pid (from $f)"
        kill "$pid" 2>/dev/null || true
      fi
      rm -f "$f" || true
    fi
  done
}
trap cleanup EXIT INT TERM

echo "Building server and client (if needed)"
if ! make -C "$ROOT_DIR" server client; then
  echo "[WARN] make failed or returned non-zero; binaries may be missing" >&2
fi

[[ -x "$SERVER_APP" ]] || { echo "[ERROR] not found/executable: $SERVER_APP" >&2; exit 1; }
[[ -x "$CLIENT_APP" ]] || { echo "[ERROR] not found/executable: $CLIENT_APP" >&2; exit 1; }

echo "Starting server on port $PORT"
"$SERVER_APP" --port "$PORT" > "$LOGDIR/server.log" 2>&1 &
SERVER_PID=$!
SERVER_PID_FILE="$PID_DIR/server_test_${PORT}.pid"
echo "$SERVER_PID" > "$SERVER_PID_FILE"
PID_FILES+=("$SERVER_PID_FILE")
sleep 0.5

echo "Starting alphazero (X)"
ALPHAZERO_BOT="${ROOT_DIR}/client/python_alphazero_bot.py"
if [ -x "${ROOT_DIR}/.venv_alphazero_test/bin/python3" ]; then
  PY_CMD="${ROOT_DIR}/.venv_alphazero_test/bin/python3"
else
  PY_CMD="python3"
fi
"$PY_CMD" "$ALPHAZERO_BOT" --host 127.0.0.1 --port "$PORT" --role X --name alphazero_X --games "$GAMES" > "$LOGDIR/alphazero_X.log" 2>&1 &
CLIENT_X_PID=$!
CLIENT_X_PID_FILE="$PID_DIR/client_test_${PORT}_alphazero_X.pid"
echo "$CLIENT_X_PID" > "$CLIENT_X_PID_FILE"
PID_FILES+=("$CLIENT_X_PID_FILE")
sleep 0.5

echo "Starting alphabeta (O)"
env CONTRAST_SERVER_PORT="$PORT" "$CLIENT_APP" O alphabeta_O alphabeta "$GAMES" > "$LOGDIR/alphabeta_O.log" 2>&1 &
CLIENT_O_PID=$!
CLIENT_O_PID_FILE="$PID_DIR/client_test_${PORT}_O.pid"
echo "$CLIENT_O_PID" > "$CLIENT_O_PID_FILE"
PID_FILES+=("$CLIENT_O_PID_FILE")

echo "Waiting for clients to finish (PIDs: $CLIENT_X_PID $CLIENT_O_PID)"
wait "$CLIENT_X_PID" || true
wait "$CLIENT_O_PID" || true

echo "Stopping server (PID: $SERVER_PID)"
kill "$SERVER_PID" 2>/dev/null || true
wait "$SERVER_PID" 2>/dev/null || true
# PID files cleaned by trap

echo "Test finished. Logs in $LOGDIR"
