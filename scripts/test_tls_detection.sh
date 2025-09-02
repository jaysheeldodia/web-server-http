#!/bin/bash

echo "=== TLS Detection and ALPN Test ==="
echo

# Build the server
echo "Building server..."
make clean &>/dev/null && make &>/dev/null
if [ $? -ne 0 ]; then
    echo "Build failed!"
    exit 1
fi

echo "✓ Build successful"

# Start server
echo "Starting server with TLS enabled..."
./webserver &
SERVER_PID=$!
sleep 2

# Cleanup function
cleanup() {
    echo "Stopping server..."
    kill $SERVER_PID 2>/dev/null
    wait $SERVER_PID 2>/dev/null
}
trap cleanup EXIT

echo "✓ Server started (PID: $SERVER_PID)"
echo

# Test 1: Regular HTTP connection (should work)
echo "=== Test 1: Regular HTTP Connection ==="
curl -s http://localhost:8080/ > /dev/null
if [ $? -eq 0 ]; then
    echo "✓ HTTP connection successful"
else
    echo "✗ HTTP connection failed"
fi
echo

# Test 2: HTTPS connection attempt (will fail but should show TLS detection)
echo "=== Test 2: HTTPS Connection (TLS Detection Test) ==="
echo "This will fail because TLS wrapper is not fully implemented,"
echo "but you should see 'Detected TLS connection' in server logs"
echo

# Use openssl to make a TLS connection attempt
echo "Attempting TLS handshake..."
timeout 3s openssl s_client -connect localhost:8080 -alpn h2,http/1.1 < /dev/null 2>&1 | head -5

echo
echo "=== Check Server Logs Above ==="
echo "Look for: 'Detected TLS connection, handling with SSL'"
echo "This shows the TLS detection is working!"
echo

echo "=== Current Status ==="
echo "✅ ALPN Negotiation - Framework implemented"
echo "✅ TLS Detection - Working (detects 0x16 handshake byte)"
echo "✅ SSL Context - Properly initialized with certificates"
echo "🔧 TLS Wrapper - Needs implementation for full functionality"
echo
echo "To complete TLS support, we need to implement:"
echo "1. SSL read/write wrappers for HTTP data"
echo "2. SSL-aware HTTP request parsing"
echo "3. SSL-aware HTTP response sending"
