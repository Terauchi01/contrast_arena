#!/bin/bash

# Kill any existing processes
pkill -9 server_app client_app 2>/dev/null
sleep 1

# Number of games
GAMES=1000

echo "Starting 1000 games: Random vs NTuple"
echo "Starting server..."
./server_app > /dev/null 2>&1 &
SERVER_PID=$!
sleep 2

# Start both clients with 1000 games (swapped roles)
echo "Starting Random player (X)..."
./client_app X RandomPlayer random $GAMES > /dev/null 2>&1 &
RANDOM_PID=$!

sleep 1

echo "Starting NTuple player (O)..."
./client_app O NTuplePlayer ntuple $GAMES > /dev/null 2>&1 &
NTUPLE_PID=$!

echo "Games in progress..."
echo "Waiting for completion (this may take a while)..."

# Wait for clients to finish
wait $NTUPLE_PID 2>/dev/null
wait $RANDOM_PID 2>/dev/null

# Give server time to write final results
sleep 2

# Kill server
kill -9 $SERVER_PID 2>/dev/null

echo ""
echo "=== GAME RESULTS ==="
echo ""

# Count results from log
if [ -f game_results.log ]; then
    TOTAL=$(grep "^Game" game_results.log | tail -$GAMES | wc -l)
    X_WINS=$(grep "^Game.*Winner: X" game_results.log | tail -$GAMES | wc -l)
    O_WINS=$(grep "^Game.*Winner: O" game_results.log | tail -$GAMES | wc -l)
    
    echo "Total games played: $TOTAL"
    echo "NTuple (X) wins: $X_WINS"
    echo "Random (O) wins: $O_WINS"
    echo ""
    
    if [ $TOTAL -gt 0 ]; then
        X_PCT=$(echo "scale=2; $X_WINS * 100 / $TOTAL" | bc)
        O_PCT=$(echo "scale=2; $O_WINS * 100 / $TOTAL" | bc)
        echo "NTuple win rate: ${X_PCT}%"
        echo "Random win rate: ${O_PCT}%"
    fi
    
    echo ""
    echo "Last 10 games:"
    tail -10 game_results.log | grep "^Game"
else
    echo "No game results file found"
fi

# Cleanup
pkill -9 server_app client_app 2>/dev/null
