#!/bin/bash

pkill -9 server_app client_app 2>/dev/null
sleep 1

# Start server with verbose output
echo "=== Starting server ==="
./server_app &
SERVER_PID=$!
sleep 2

# Start X player
echo "=== Starting X (NTuple) player ==="
echo "Command: ./client_app X NTuplePlayer ntuple 1"
./client_app X NTuplePlayer ntuple 1 > /tmp/ntuple_x.log 2>&1 &
X_PID=$!

sleep 3

# Start O player  
echo "=== Starting O (Random) player ==="
echo "Command: ./client_app O RandomPlayer random 1"
./client_app O RandomPlayer random 1 > /tmp/random_o.log 2>&1 &
O_PID=$!

# Wait for game
echo "=== Waiting for game (30 seconds) ==="
for i in {1..30}; do
    if ! ps -p $X_PID > /dev/null 2>&1 && ! ps -p $O_PID > /dev/null 2>&1; then
        echo "Both players finished"
        break
    fi
    sleep 1
    echo -n "."
done
echo ""

# Cleanup
pkill -9 server_app client_app 2>/dev/null

# Show logs
echo ""
echo "=== X (NTuple) Log - First 50 lines ==="
head -50 /tmp/ntuple_x.log | grep -E "(Connected|INFO|AUTO|NTuple::pick|handle_snapshot)"

echo ""
echo "=== O (Random) Log - First 50 lines ==="
head -50 /tmp/random_o.log | grep -E "(Connected|INFO|AUTO|handle_snapshot)"
