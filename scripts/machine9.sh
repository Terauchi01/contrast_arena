#!/usr/bin/env bash
set -euo pipefail
ID="9-4"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

SERVER_APP="${ROOT_DIR}/server_app"
CLIENT_APP="${ROOT_DIR}/client_app"
ALPHAZERO_BOT="${ROOT_DIR}/client/python_alphazero_bot.py"
VENV_DIR="${ROOT_DIR}/.venv_alphazero${ID}"
VENV_PY="${VENV_DIR}/bin/python3"

# Create venv and install requirements if missing (with lock to avoid races)
if [ ! -x "$VENV_PY" ]; then
    echo "Setting up virtualenv for AlphaZero at $VENV_DIR"
    REQ_MIN="$ROOT_DIR/ai/contrast_alphazero/requirements-minimal.txt"
    REQ_FULL="$ROOT_DIR/ai/contrast_alphazero/requirements.txt"
    LOCK_DIR="${VENV_DIR}.lock"

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

LOGDIR="${ROOT_DIR}/logs/machine${ID}"
PORT=${PORT:-18889}
GAMES=${1:-1000}
mkdir -p "$LOGDIR"

# Silence verbose/debug output for AI clients (alphabeta, alphazero)
# Set minimal mode so debug traces are suppressed when running matches
export CONTRAST_MINIMAL=1

# PID files and cleanup
PID_DIR="${ROOT_DIR}/pids"
mkdir -p "$PID_DIR"
PID_FILES=()

cleanup() {
    echo "Cleaning up PID files and processes"
    # kill any remaining clients
    if [ "${#CLIENT_PIDS[@]}" -gt 0 ]; then
        echo "Killing clients: ${CLIENT_PIDS[*]}"
        echo "${CLIENT_PIDS[@]}" | xargs -r kill -9 || true
    fi
    # kill any remaining servers
    if [ "${#SERVER_PIDS[@]}" -gt 0 ]; then
        echo "Killing servers: ${SERVER_PIDS[*]}"
        echo "${SERVER_PIDS[@]}" | xargs -r kill -9 || true
    fi
    # remove pid files
    for f in "${PID_FILES[@]}"; do
        [ -f "$f" ] && rm -f "$f" || true
    done
}
trap cleanup EXIT INT TERM

echo "Cleaning up existing server/client processes for machine${ID} (if any)"
pkill -f server_app 2>/dev/null || true
pkill -f client_app 2>/dev/null || true
pkill -f python_alphazero_bot.py 2>/dev/null || true
for p in $(seq "$PORT" $((PORT + 8))); do
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

echo "Building server and client (if needed)"
if ! make -C "$ROOT_DIR" server client; then
    echo "[WARN] make failed or returned non-zero; binaries may be missing" >&2
fi

[[ -x "$SERVER_APP" ]] || { echo "[ERROR] not found/executable: $SERVER_APP" >&2; exit 1; }
[[ -x "$CLIENT_APP" ]] || { echo "[ERROR] not found/executable: $CLIENT_APP" >&2; exit 1; }
[[ -f "$ALPHAZERO_BOT" ]] || { echo "[ERROR] not found: $ALPHAZERO_BOT" >&2; exit 1; }

echo "Running matches for machine${ID}"
SERVER_PIDS=()
CLIENT_PIDS=()

# Define matches assigned to machine9 (format: X_player O_player)
MATCHES=(
"alphabeta alphazero"
"alphabeta alphabeta"
"alphabeta mcts"
"alphabeta ntuple"
"alphabeta rulebased2"
"rulebased2 alphazero"
"rulebased2 rulebased2"
"rulebased2 alphabeta"
"rulebased2 mcts"
)

PBASE=$PORT
idx=0
for pair in "${MATCHES[@]}"; do
    idx=$((idx + 1))
    P=$((PBASE + idx - 1))
    X_PLAYER=$(echo "$pair" | awk '{print $1}')
    O_PLAYER=$(echo "$pair" | awk '{print $2}')

    echo "Match #$idx: $X_PLAYER (X) vs $O_PLAYER (O) on port $P"

    # prepare commands
    if [ "$X_PLAYER" = "alphazero" ] || [ "$O_PLAYER" = "alphazero" ]; then
        # alphazero uses python bot
        if [ "$X_PLAYER" = "alphazero" ]; then
            CMD1=("$VENV_PY" "$ALPHAZERO_BOT" --host 127.0.0.1 --port "$P" --role X --name alphazero_X --games "$GAMES")
        else
            CMD1=(env CONTRAST_SERVER_PORT="$P" "$CLIENT_APP" X ${X_PLAYER}_X $X_PLAYER "$GAMES")
        fi

        if [ "$O_PLAYER" = "alphazero" ]; then
            CMD2=("$VENV_PY" "$ALPHAZERO_BOT" --host 127.0.0.1 --port "$P" --role O --name alphazero_O --games "$GAMES")
        else
            CMD2=(env CONTRAST_SERVER_PORT="$P" "$CLIENT_APP" O ${O_PLAYER}_O $O_PLAYER "$GAMES")
        fi
    else
        CMD1=(env CONTRAST_SERVER_PORT="$P" "$CLIENT_APP" X ${X_PLAYER}_X $X_PLAYER "$GAMES")
        CMD2=(env CONTRAST_SERVER_PORT="$P" "$CLIENT_APP" O ${O_PLAYER}_O $O_PLAYER "$GAMES")
    fi

    LOG1="$LOGDIR/${X_PLAYER}_vs_${O_PLAYER}_X.log"
    LOG2="$LOGDIR/${X_PLAYER}_vs_${O_PLAYER}_O.log"

    echo " Starting server on port $P"
    "$SERVER_APP" --port "$P" > "$LOGDIR/server${idx}.log" 2>&1 &
    SERVER_PID=$!
    SERVER_PID_FILE="$PID_DIR/server_${ID}_${idx}.pid"
    echo "$SERVER_PID" > "$SERVER_PID_FILE"
    PID_FILES+=("$SERVER_PID_FILE")
    sleep 0.5

    echo "  Starting clients for match #$idx (background, staggered)"
    ("${CMD1[@]}" > "$LOG1" 2>&1) &
    CLIENT1_PID=$!
    sleep 0.5
    ("${CMD2[@]}" > "$LOG2" 2>&1) &
    CLIENT2_PID=$!

    CLIENT1_PID_FILE="$PID_DIR/client_${ID}_${idx}_1.pid"
    CLIENT2_PID_FILE="$PID_DIR/client_${ID}_${idx}_2.pid"
    echo "$CLIENT1_PID" > "$CLIENT1_PID_FILE"
    echo "$CLIENT2_PID" > "$CLIENT2_PID_FILE"
    PID_FILES+=("$CLIENT1_PID_FILE" "$CLIENT2_PID_FILE")

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

echo "machine${ID} done"
