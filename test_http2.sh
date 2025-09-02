#!/bin/bash

echo "Testing HTTP/2 connection..."

# Test with netcat to see raw bytes
echo "Testing raw HTTP/2 preface with netcat:"
(printf "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n"; sleep 1) | nc localhost 8080 | hexdump -C

echo -e "\n\nTesting with curl HTTP/2 prior knowledge:"
timeout 5 curl -v --http2-prior-knowledge http://localhost:8080/ || echo "Curl test failed"
