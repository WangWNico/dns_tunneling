#!/bin/bash
set -euo pipefail

# Build
gcc -o server dns-server.c -Wall -Wextra
gcc -o client dns-client.c -Wall -Wextra

# Start server on port 5353 in background
./server 5353 &
SRV_PID=$!
echo "Server PID: $SRV_PID"
sleep 0.5

# Run client
./client 127.0.0.1 5353

# Stop server
kill $SRV_PID || true

echo "Test finished."
