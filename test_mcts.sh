#!/bin/bash
# Test MCTS vs Random

pkill -9 server_app client_app 2>/dev/null
sleep 1

echo "=== Testing MCTS vs Random ==="
./server_app &
SERVER_PID=$!
sleep 2

./client_app X MCTS mcts:1000 1 &
X_PID=$!

sleep 1

./client_app O RandomPlayer random 1 &
O_PID=$!

# Wait with timeout
for i in {1..120}; do
    if ! ps -p $X_PID > /dev/null 2>&1 && ! ps -p $O_PID > /dev/null 2>&1; then
        echo "Both clients finished"
        break
    fi
    sleep 1
done

# Check if processes are still running
if ps -p $X_PID > /dev/null 2>&1 || ps -p $O_PID > /dev/null 2>&1; then
    echo "Timeout - killing processes"
    kill $X_PID $O_PID 2>/dev/null
fi

kill $SERVER_PID 2>/dev/null

echo ""
echo "=== Latest Game Result ==="
tail -3 game_results.log
