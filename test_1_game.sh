#!/bin/bash

pkill -9 server_app client_app 2>/dev/null
sleep 1

echo "=== Testing 1 game ==="
./server_app &
SERVER_PID=$!
sleep 2

./client_app X AlphaBeta alphabeta 1 &
X_PID=$!

sleep 1

./client_app O RandomPlayer random 1 &
O_PID=$!

# Wait with timeout
for i in {1..60}; do
    if ! ps -p $X_PID > /dev/null 2>&1 && ! ps -p $O_PID > /dev/null 2>&1; then
        echo "Both clients finished"
        break
    fi
    sleep 1
    echo -n "."
done
echo ""

sleep 2
kill -9 $SERVER_PID 2>/dev/null

echo ""
tail -5 game_results.log
