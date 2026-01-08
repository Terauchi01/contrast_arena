#!/usr/bin/env bash
# Machine3 assigned matches
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

SERVER_APP="${ROOT_DIR}/server_app"
CLIENT_APP="${ROOT_DIR}/client_app"
ALPHAZERO_BOT="${ROOT_DIR}/client/python_alphazero_bot.py"
VENV_DIR="${ROOT_DIR}/.venv_alphazero3"
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

PORT=${PORT:-18888}
GAMES=${1:-1000}
LOGDIR="${ROOT_DIR}/logs/machine3"
mkdir -p "$LOGDIR"

# If previous server/client processes are still running, kill them to avoid port conflicts
echo "Cleaning up existing server/client processes for machine3 (if any)"
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

# Three matches -> run sequentially: start server, start two clients, wait at connect time, then continue
PORT1=${PORT}
PORT2=$((PORT + 1))
PORT3=$((PORT + 2))

echo "Running 3 matches sequentially on ports $PORT1,$PORT2,$PORT3 for machine3"
SERVER_PIDS=()
CLIENT_PIDS=()
for i in 1 2 3; do
	case "$i" in
		1)
			P=$PORT1
			echo "Match #$i: alphabeta vs mcts on port $P"
			CMD1=(env CONTRAST_SERVER_PORT="$P" "$CLIENT_APP" X alphabeta_X alphabeta "$GAMES")
			CMD2=(env CONTRAST_SERVER_PORT="$P" "$CLIENT_APP" O mcts_O mcts "$GAMES")
			LOG1="$LOGDIR/alphabeta_vs_mcts_X.log"
			LOG2="$LOGDIR/alphabeta_vs_mcts_O.log"
			;;
		2)
			P=$PORT2
			echo "Match #$i: alphazero vs alphazero on port $P"
			CMD1=("$VENV_PY" "$ALPHAZERO_BOT" --host 127.0.0.1 --port "$P" --role X --name az1 --games "$GAMES")
			CMD2=("$VENV_PY" "$ALPHAZERO_BOT" --host 127.0.0.1 --port "$P" --role O --name az2 --games "$GAMES")
			LOG1="$LOGDIR/alphazero_vs_alphazero_X.log"
			LOG2="$LOGDIR/alphazero_vs_alphazero_O.log"
			;;
		3)
			P=$PORT3
			echo "Match #$i: ntuple vs mcts on port $P"
			CMD1=(env CONTRAST_SERVER_PORT="$P" "$CLIENT_APP" X ntuple_X2 ntuple "$GAMES")
			CMD2=(env CONTRAST_SERVER_PORT="$P" "$CLIENT_APP" O mcts_O2 mcts "$GAMES")
			LOG1="$LOGDIR/ntuple_vs_mcts_X.log"
			LOG2="$LOGDIR/ntuple_vs_mcts_O.log"
			;;
	esac

	echo " Starting server on port $P"
	"$SERVER_APP" --port "$P" > "$LOGDIR/server${i}.log" 2>&1 &
	SERVER_PID=$!
	sleep 0.5

	echo "  Starting clients for match #$i (background, staggered)"
	("${CMD1[@]}" > "$LOG1" 2>&1) &
	CLIENT1_PID=$!
	sleep 0.5
	("${CMD2[@]}" > "$LOG2" 2>&1) &
	CLIENT2_PID=$!

	SERVER_PIDS+=("$SERVER_PID")
	CLIENT_PIDS+=("$CLIENT1_PID" "$CLIENT2_PID")
	sleep 1
done

echo "All matches started; waiting for clients to finish..."
if [ "${#CLIENT_PIDS[@]}" -gt 0 ]; then
	wait "${CLIENT_PIDS[@]}" || true
fi

echo "Stopping servers (PIDs ${SERVER_PIDS[*]})"
for pid in "${SERVER_PIDS[@]}"; do
	kill "$pid" 2>/dev/null || true
done
wait "${SERVER_PIDS[@]}" 2>/dev/null || true
echo "machine3 done"
