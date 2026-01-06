#!/bin/bash

pkill -9 server_app client_app 2>/dev/null
sleep 1

# Start server
echo "Starting server..."
./server_app &
SERVER_PID=$!
sleep 2

# Start both clients
echo "Starting NTuple client..."
./client_app X NTuplePlayer ntuple 1 > /tmp/ntuple_debug.log 2>&1 &
NTUPLE_PID=$!

echo "Starting Random client..."
./client_app O RandomPlayer random 1 > /tmp/random_debug.log 2>&1 &
RANDOM_PID=$!

# Wait for both clients
echo "Waiting for game to complete (max 30 seconds)..."
for i in {1..30}; do
    if ! ps -p $NTUPLE_PID > /dev/null 2>&1 && ! ps -p $RANDOM_PID > /dev/null 2>&1; then
        echo "Both clients finished"
        break
    fi
    sleep 1
    echo -n "."
done
echo ""

# Kill everything
pkill -9 server_app client_app 2>/dev/null

# Show logs
echo ""
echo "=== NTuple Debug Log ==="
cat /tmp/ntuple_debug.log
echo ""
echo "=== Random Debug Log ==="
cat /tmp/random_debug.log
