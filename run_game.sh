#!/bin/bash

# Clean up
pkill -9 client_app server_app 2>/dev/null
sleep 1

# Start server
./server_app &
SERVER_PID=$!
sleep 2

# Start NTuple player
./client_app X NTuplePlayer ntuple 1 > ntuple_game.log 2>&1 &
NTUPLE_PID=$!
sleep 2

# Start Random player (foreground)
./client_app O RandomPlayer random 1 > random_game.log 2>&1
RANDOM_EXIT=$?

# Wait a bit for completion
sleep 2

# Show results
echo "=== Game Results ==="
echo ""
echo "--- NTuple Log (last 20 lines) ---"
tail -20 ntuple_game.log
echo ""
echo "--- Random Log (last 20 lines) ---"
tail -20 random_game.log
echo ""
echo "--- Game Results File ---"
tail -5 game_results.log 2>/dev/null || echo "No game_results.log found"

# Cleanup
pkill -9 client_app server_app 2>/dev/null
