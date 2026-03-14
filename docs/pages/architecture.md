# Architecture Overview

How the server is structured and how a request is handled.

## High-level design

The server is built in modules: **core** (server, thread pool, main), **handlers** (file, API, HTTP/2, WebSocket), and **network** (HTTP request parsing). The main thread accepts connections; each connection is handled by a worker from the thread pool.

## Module structure

```
src/
├── core/                    # Core server
│   ├── main.cpp             # Entry point, CLI, signal handling
│   ├── server.cpp           # WebServer: accept, route, dispatch to handlers
│   └── thread_pool.cpp      # Thread pool (queue + workers)
├── handlers/
│   ├── file_handler.cpp     # Static file serving, MIME types
│   ├── http2_handler.cpp    # HTTP/2 protocol (nghttp2)
│   ├── json_handler.cpp     # JSON API (stats, users)
│   └── websocket_handler.cpp # WebSocket upgrade and frames
├── network/
│   └── http_request.cpp     # Parse HTTP request (method, path, headers, body)
└── utils/                   # Shared helpers (if any)
```

Headers live in `include/` with the same structure (`include/core/`, `include/handlers/`, `include/network/`).

## How a request is handled

1. **Accept**: The main thread accepts a new TCP connection.
2. **Dispatch**: The connection is handed off to the thread pool (one task per connection).
3. **Read**: The worker reads the incoming HTTP request (or HTTP/2 preface / WebSocket upgrade).
4. **Parse**: The request is parsed into method, path, headers, and body.
5. **Route**: The server decides which handler to use:
   - `/admin-dashboard` → admin dashboard HTML
   - `/dashboard`, `/dashboard.html` → dashboard page
   - `/api/*` → JSON API (e.g. `/api/stats`, `/api/users`, `/api/docs`)
   - `/ws`, `/websocket` → WebSocket upgrade
   - Otherwise → static file from document root (e.g. `/` → `index.html`)
6. **Handle**: The chosen handler runs (e.g. read file, build JSON, perform WebSocket handshake).
7. **Send**: The worker sends the HTTP response (or continues with WebSocket/HTTP/2).
8. **Connection**: For HTTP/1.1, the connection is either closed or kept open for the next request (Keep-Alive).

So in short: **client connects → server accepts → worker reads and parses request → route to handler → handler produces response → server sends response → connection closed or reused.**

## Design principles

- **RAII**: Resources (sockets, threads, locks) are tied to object lifetime so they are released when objects are destroyed; this avoids leaks and keeps cleanup simple.
- **Modularity**: Clear separation between core, handlers, and network so you can find and change one part without touching the rest.
- **Thread safety**: Shared state (e.g. the task queue, connection counts) is protected with mutexes or atomics so multiple workers can run safely.
- **Scalability**: You can increase the number of worker threads (e.g. `-t 8`) to handle more concurrent connections, within the limits of your machine.
