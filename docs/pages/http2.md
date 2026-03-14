# HTTP/2 Support

The server can speak HTTP/2 (via the nghttp2 library), which allows multiple requests over a single connection, header compression, and optional server push.

## Features

- **Stream multiplexing**: Many requests over one connection instead of one request per connection.
- **Server push**: The server can push related resources (e.g. CSS/JS) before the client asks for them.
- **Header compression**: HPACK reduces the size of repeated headers.
- **Binary protocol**: More efficient than HTTP/1.1 text framing.

HTTP/2 is typically used over TLS. The server negotiates HTTP/2 with the client using **ALPN** (Application-Layer Protocol Negotiation) when TLS is enabled and both sides support it.

## Configuration

HTTP/2 is enabled in code (e.g. `server.enable_http2(true)` in `main.cpp`). When TLS is enabled and certificates are present, the server can negotiate `h2` (HTTP/2) or fall back to `http/1.1` over the same connection.

## Note

If you run without TLS (plain HTTP), the server still runs HTTP/1.1. Full HTTP/2 with ALPN is used when TLS is enabled and the client supports it.
