#!/usr/bin/env bash
# Machine6 assigned matches
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

SERVER_APP="${ROOT_DIR}/server_app"
CLIENT_APP="${ROOT_DIR}/client_app"
ALPHAZERO_BOT="${ROOT_DIR}/client/python_alphazero_bot.py"
VENV_DIR="${ROOT_DIR}/.venv_alphazero6"
VENV_PY="${VENV_DIR}/bin/python3"

# Create venv and install requirements if missing (with lock to avoid races)
if [ ! -x "$VENV_PY" ]; then
	echo "Setting up virtualenv for AlphaZero at $VENV_DIR"
	REQ_MIN="$ROOT_DIR/ai/contrast_alphazero/requirements-minimal.txt"
	REQ_FULL="$ROOT_DIR/ai/contrast_alphazero/requirements.txt"
	LOCK_DIR="${VENV_DIR}.lock"

	# try to acquire lock; if someone else holds it, wait until venv is ready
	if mkdir "$LOCK_DIR" 2>/dev/null; then
		cleanup() { rm -rf "$LOCK_DIR"; }
		trap cleanup EXIT

		if [ ! -d "$VENV_DIR" ]; then
			python3 -m venv "$VENV_DIR"
		fi
		$VENV_DIR/bin/pip install --upgrade pip setuptools wheel || true
		if [ -f "$REQ_MIN" ]; then
			$VENV_DIR/bin/pip install -r "$REQ_MIN" || true
		elif [ -f "$REQ_FULL" ]; then
			$VENV_DIR/bin/pip install -r "$REQ_FULL" || true
		fi

		trap - EXIT
		rm -rf "$LOCK_DIR" || true
	else
		echo "Waiting for another process to finish venv setup at $VENV_DIR"
		while [ ! -x "$VENV_PY" ]; do sleep 0.2; done
	fi
fi

echo "Building server and client (if needed)"
if ! make -C "$ROOT_DIR" server client; then
	echo "[WARN] make failed or returned non-zero; binaries may be missing" >&2
fi

LOGDIR="${ROOT_DIR}/logs/machine6"
PORT=${PORT:-18900}
GAMES=${1:-1000}
mkdir -p "$LOGDIR"

# If previous server/client processes are still running, kill them to avoid port conflicts
echo "Cleaning up existing server/client processes for machine6 (if any)"
pkill -f server_app 2>/dev/null || true
pkill -f client_app 2>/dev/null || true
pkill -f python_alphazero_bot.py 2>/dev/null || true
for p in $(seq "$PORT" $((PORT + 3))); do
	if command -v lsof >/dev/null 2>&1; then
		PIDS=$(lsof -ti tcp:"$p" 2>/dev/null || true)
		if [ -n "$PIDS" ]; then
			echo "Killing processes on port $p: $PIDS"
			echo "$PIDS" | xargs -r kill -9 || true
		fi
	else
		PIDS=$(ss -ltnp 2>/dev/null | awk -v port=":$p" '$0 ~ port { for(i=1;i<=NF;i++) if($i ~ /pid=/) { gsub(/pid=|,/,"",$i); print $i }}' || true)
		if [ -n "$PIDS" ]; then
			echo "Killing processes on port $p: $PIDS"
			echo "$PIDS" | xargs -r kill -9 || true
		fi
	fi
done

# Three matches -> three servers
PORT1=${PORT}
PORT2=$((PORT + 1))
PORT3=$((PORT + 2))

echo "Starting servers on ports $PORT1,$PORT2,$PORT3 for machine6"
"$SERVER_APP" --port "$PORT1" > "$LOGDIR/server1.log" 2>&1 &
SERVER1_PID=$!
"$SERVER_APP" --port "$PORT2" > "$LOGDIR/server2.log" 2>&1 &
SERVER2_PID=$!
"$SERVER_APP" --port "$PORT3" > "$LOGDIR/server3.log" 2>&1 &
SERVER3_PID=$!
sleep 0.5

echo "alphazero vs rulebased2 on port $PORT1"
"$VENV_PY" "$ALPHAZERO_BOT" --host 127.0.0.1 --port "$PORT1" --role X --name az_m6 --games "$GAMES" > "$LOGDIR/alphazero_vs_rulebased2_X.log" 2>&1 &
CLIENT1=$!
env CONTRAST_SERVER_PORT="$PORT1" "$CLIENT_APP" O rulebased2_O4 rulebased2 "$GAMES" > "$LOGDIR/alphazero_vs_rulebased2_O.log" 2>&1 &
CLIENT2=$!

echo "mcts vs alphabeta on port $PORT2"
env CONTRAST_SERVER_PORT="$PORT2" "$CLIENT_APP" X mcts_X2 mcts "$GAMES" > "$LOGDIR/mcts_vs_alphabeta_X.log" 2>&1 &
CLIENT3=$!
env CONTRAST_SERVER_PORT="$PORT2" "$CLIENT_APP" O alphabeta_O4 alphabeta "$GAMES" > "$LOGDIR/mcts_vs_alphabeta_O.log" 2>&1 &
CLIENT4=$!

echo "rulebased2 vs alphabeta on port $PORT3"
env CONTRAST_SERVER_PORT="$PORT3" "$CLIENT_APP" X rulebased2_X2 rulebased2 "$GAMES" > "$LOGDIR/rulebased2_vs_alphabeta_X.log" 2>&1 &
CLIENT5=$!
env CONTRAST_SERVER_PORT="$PORT3" "$CLIENT_APP" O alphabeta_O5 alphabeta "$GAMES" > "$LOGDIR/rulebased2_vs_alphabeta_O.log" 2>&1 &
CLIENT6=$!

wait $CLIENT1 $CLIENT2 $CLIENT3 $CLIENT4 $CLIENT5 $CLIENT6 || true

echo "Stopping servers (PIDs $SERVER1_PID $SERVER2_PID $SERVER3_PID)"
kill $SERVER1_PID $SERVER2_PID $SERVER3_PID || true
wait $SERVER1_PID $SERVER2_PID $SERVER3_PID 2>/dev/null || true
echo "machine6 done"
