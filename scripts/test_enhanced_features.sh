#!/bin/bash

# Test script for HTTP/2 Server Push and Priority Features

echo "=== HTTP/2 Enhanced Features Test ==="
echo

# Build the server
echo "Building the server..."
make clean && make
if [ $? -ne 0 ]; then
    echo "Build failed!"
    exit 1
fi

echo "Build successful!"
echo

# Start the server in background
echo "Starting server..."
./webserver &
SERVER_PID=$!
sleep 2

# Function to cleanup
cleanup() {
    echo "Stopping server..."
    kill $SERVER_PID 2>/dev/null
    wait $SERVER_PID 2>/dev/null
}

# Set trap for cleanup
trap cleanup EXIT

# Test 1: Basic HTTP/2 connectivity
echo "=== Test 1: Basic HTTP/2 Connection ==="
curl -v --http2-prior-knowledge http://localhost:8080/ -o /dev/null
echo

# Test 2: Test Server Push capabilities (note: curl doesn't show server push, but server logs should)
echo "=== Test 2: Server Push Test (check server logs for push promises) ==="
curl -v --http2-prior-knowledge http://localhost:8080/index.html -o /dev/null
echo

# Test 3: Multiple concurrent streams (tests priority handling)
echo "=== Test 3: Concurrent Streams Test ==="
curl --http2-prior-knowledge http://localhost:8080/style.css &
curl --http2-prior-knowledge http://localhost:8080/demo.html &
curl --http2-prior-knowledge http://localhost:8080/data.json &
wait
echo

# Test 4: Dashboard page (should trigger server push)
echo "=== Test 4: Dashboard Page Test ==="
curl -v --http2-prior-knowledge http://localhost:8080/dashboard.html -o /dev/null
echo

echo "=== Tests completed! Check server logs for:"
echo "  - Server push promises"
echo "  - Priority frame handling"
echo "  - ALPN negotiation (if TLS was enabled)"
echo

echo "Note: ALPN requires TLS. To test ALPN:"
echo "  1. Generate certificates: openssl req -x509 -newkey rsa:2048 -keyout key.pem -out cert.pem -days 365 -nodes"
echo "  2. Modify server code to enable TLS with cert/key files"
echo "  3. Test with: curl -v --http2 https://localhost:8080/ -k"
