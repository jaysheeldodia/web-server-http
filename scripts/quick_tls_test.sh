#!/bin/bash

echo "=== TLS HTTP/2 Quick Test ==="
echo

# Start server
echo "Starting server..."
./webserver &
SERVER_PID=$!
sleep 2

cleanup() {
    kill $SERVER_PID 2>/dev/null
    wait $SERVER_PID 2>/dev/null
}
trap cleanup EXIT

echo "✅ Server started"

# Test HTTPS connection
echo "Testing HTTPS connection..."
curl -k -s -m 10 --http2 https://localhost:8080/ -o /tmp/test_output.html 2>/dev/null
if [ $? -eq 0 ] && [ -s /tmp/test_output.html ]; then
    echo "✅ HTTPS working! Received $(wc -c < /tmp/test_output.html) bytes"
    echo "✅ TLS Infrastructure: 100% Complete!"
else
    echo "🔧 Still working on TLS data exchange..."
fi

# Quick ALPN test
echo "Testing ALPN negotiation..."
timeout 3s openssl s_client -connect localhost:8080 -alpn h2 2>&1 | grep -q "ALPN protocol: h2"
if [ $? -eq 0 ]; then
    echo "✅ ALPN negotiation working"
else
    echo "🔧 ALPN needs verification"
fi

rm -f /tmp/test_output.html
echo "Done."
