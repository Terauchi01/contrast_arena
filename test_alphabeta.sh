#!/bin/bash
# AlphaBeta vs Random test script

echo "=== AlphaBeta vs Random Test ==="

# Kill any existing processes
pkill -9 server_app client_app 2>/dev/null
sleep 1

# Start server
echo "Starting server..."
./server_app > /tmp/server.log 2>&1 &
SERVER_PID=$!
sleep 2

# Start AlphaBeta player (X)
echo "Starting AlphaBeta player (X)..."
./client_app X AlphaBetaPlayer alphabeta 1 > /tmp/alphabeta.log 2>&1 &
ALPHABETA_PID=$!
sleep 2

# Start Random player (O)
echo "Starting Random player (O)..."
./client_app O RandomPlayer random 1 > /tmp/random.log 2>&1 &
RANDOM_PID=$!

# Wait for clients to finish
sleep 30

# Check results
echo ""
echo "=== AlphaBeta Player Log ==="
tail -30 /tmp/alphabeta.log

echo ""
echo "=== Random Player Log ==="
tail -30 /tmp/random.log

echo ""
echo "=== Server Log ==="
tail -30 /tmp/server.log

echo ""
echo "=== Game Results ==="
tail -5 game_results.log

# Cleanup
pkill -9 server_app client_app 2>/dev/null
