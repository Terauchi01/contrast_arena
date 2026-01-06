#!/bin/bash
# Simple test: AlphaBeta vs Random

pkill -9 server_app client_app 2>/dev/null
sleep 1

echo "Starting server..."
./server_app &
SERVER_PID=$!
sleep 2

echo "Starting AlphaBeta (X)..."
./client_app X AlphaBeta alphabeta 1 &
sleep 3

echo "Starting Random (O)..."
./client_app O Random random 1 &

wait

echo ""
echo "=== Latest Game Result ==="
tail -3 game_results.log

# Cleanup
pkill -9 server_app client_app 2>/dev/null
