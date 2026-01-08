#!/bin/bash
# AlphaBeta vs AlphaZero test script: 10 games as X, 10 games as O

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORKSPACE="$(dirname "$SCRIPT_DIR")"
cd "$WORKSPACE"

CLIENT_APP="./client_app"
SERVER_APP="./server_app"
ALPHAZERO_BOT="client/python_alphazero_bot.py"
VENV_ALPHAZERO=".venv_alphazero"
LOGDIR="logs/test_alphabeta_alphazero"

mkdir -p "$LOGDIR"

# Check for virtual environment
if [ ! -d "$VENV_ALPHAZERO" ]; then
    echo "Creating virtual environment $VENV_ALPHAZERO..."
    python3 -m venv "$VENV_ALPHAZERO"
fi

VENV_PY="$VENV_ALPHAZERO/bin/python3"

# Check if dependencies are installed
echo "Checking dependencies..."
if ! "$VENV_PY" -c "import numpy, torch" 2>/dev/null; then
    echo "Installing dependencies in $VENV_ALPHAZERO..."
    "$VENV_ALPHAZERO/bin/pip" install --upgrade pip setuptools wheel -q
    "$VENV_ALPHAZERO/bin/pip" install -r ai/contrast_alphazero/requirements-minimal.txt -q || \
    "$VENV_ALPHAZERO/bin/pip" install torch numpy -q
fi

echo "=== AlphaBeta vs AlphaZero Test ==="
echo "Building..."
make -j4 > /dev/null 2>&1 || { echo "Build failed"; exit 1; }

# Test 1: AlphaBeta(X) vs AlphaZero(O) - 10 games
echo ""
echo "[Test 1/2] AlphaBeta as X vs AlphaZero as O (10 games)..."
PORT1=18101

env CONTRAST_SERVER_PORT="$PORT1" "$SERVER_APP" > "$LOGDIR/test1_server.log" 2>&1 &
SERVER_PID=$!
sleep 1

env CONTRAST_SERVER_PORT="$PORT1" "$CLIENT_APP" X alphabeta_test alphabeta 10 > "$LOGDIR/test1_alphabeta_X.log" 2>&1 &
CLIENT1_PID=$!

"$VENV_PY" "$ALPHAZERO_BOT" --host 127.0.0.1 --port "$PORT1" --role O --name alphazero_test --games 10 > "$LOGDIR/test1_alphazero_O.log" 2>&1 &
CLIENT2_PID=$!

wait $CLIENT1_PID
wait $CLIENT2_PID
kill $SERVER_PID 2>/dev/null || true
wait $SERVER_PID 2>/dev/null || true

echo "  Results:"
python3 scripts/aggregate_per_log.py "$LOGDIR/test1_server.log" | column -t

# Test 2: AlphaZero(X) vs AlphaBeta(O) - 10 games
echo ""
echo "[Test 2/2] AlphaZero as X vs AlphaBeta as O (10 games)..."
PORT2=18102

env CONTRAST_SERVER_PORT="$PORT2" "$SERVER_APP" > "$LOGDIR/test2_server.log" 2>&1 &
SERVER_PID=$!
sleep 1

"$VENV_PY" "$ALPHAZERO_BOT" --host 127.0.0.1 --port "$PORT2" --role X --name alphazero_test --games 10 > "$LOGDIR/test2_alphazero_X.log" 2>&1 &
CLIENT1_PID=$!

env CONTRAST_SERVER_PORT="$PORT2" "$CLIENT_APP" O alphabeta_test alphabeta 10 > "$LOGDIR/test2_alphabeta_O.log" 2>&1 &
CLIENT2_PID=$!

wait $CLIENT1_PID
wait $CLIENT2_PID
kill $SERVER_PID 2>/dev/null || true
wait $SERVER_PID 2>/dev/null || true

echo "  Results:"
python3 scripts/aggregate_per_log.py "$LOGDIR/test2_server.log" | column -t

echo ""
echo "=== Test Complete ==="
echo "Full results:"
python3 scripts/aggregate_per_log.py "$LOGDIR" | column -t
