# Concepts

This page explains the main ideas you need to understand the application, in simple terms.

## Web server

A **web server** is a program that:

1. Listens on a **port** (e.g. 8080) for incoming network connections.
2. Receives **HTTP requests** (method, path, headers, and sometimes a body).
3. Sends back **HTTP responses** (status code, headers, and a body such as HTML or JSON).

When you open `http://localhost:8080/` in a browser, the browser sends an HTTP request; the server answers with the content of the page.

## HTTP request and response

- **Request**: The client sends a **method** (e.g. `GET`, `POST`), a **path** (e.g. `/`, `/api/stats`), **headers** (key-value metadata), and optionally a **body** (e.g. JSON).
- **Response**: The server sends a **status code** (e.g. 200 OK, 404 Not Found), **headers**, and a **body** (the actual content).

The server in this project parses the request and chooses a **handler** (e.g. serve a file, run an API, upgrade to WebSocket) based on the path and method.

## Thread pool

Creating a new thread for every request would be slow and wasteful. Instead, the server uses a **thread pool**:

- A **fixed number** of worker threads are started at startup.
- Incoming work (handling a client) is put into a **single shared queue**.
- Each worker repeatedly takes a task from the queue, handles it, then takes the next.
- A **mutex** and **condition variable** protect the queue so only one thread takes a task at a time.

So: “a fixed set of worker threads; each handles one client at a time; new work goes into a queue.” There is no work stealing, no lock-free queue, and no dynamic sizing in this implementation.

## Keep-Alive

Normally, each HTTP request could use a new TCP connection. **Keep-Alive** means reusing the same connection for several requests: the client sends multiple requests one after another on the same connection, and the server keeps the connection open for a short time (configurable timeout). This reduces overhead.

## TLS (SSL)

**TLS** (often called SSL) encrypts the connection between client and server so that data cannot be read or tampered with in transit. The server can optionally run in HTTPS mode if you provide a certificate and key. **ALPN** (Application-Layer Protocol Negotiation) lets the client and server agree to use HTTP/2 over that secure connection.

## WebSocket

**WebSocket** is a protocol for **real-time, two-way** communication over a single connection. The client sends an HTTP “upgrade” request; the server responds with “101 Switching Protocols,” and then both sides can send frames (e.g. text or binary) at any time. The admin dashboard uses WebSocket to stream live metrics to the browser.

## REST and JSON

- **REST** here means “HTTP API with clear URLs and methods”: e.g. `GET /api/stats` to get statistics, `POST /api/users` to create a user. The server exposes several such endpoints.
- **JSON** is a text format for data (objects, arrays, strings, numbers). The API uses JSON for request and response bodies (e.g. `{"name": "Alice", "email": "alice@example.com"}`).

## Where to find what in the code

The codebase is split into **modules**. When you want to change something, this table points you to the right place:

| What you want to do | Where to look |
|---------------------|----------------|
| Change port, document root, or thread count | `src/core/main.cpp` (defaults and CLI parsing) |
| Add or change an API endpoint | `src/core/server.cpp` (routing and handler calls) |
| Change how static files are served | `include/handlers/file_handler.h` and `src/handlers/file_handler.cpp` |
| Change thread pool size (default 4) | CLI option `-t` / `--threads` in `main.cpp`; pool is created in `server.cpp` |
| Dashboard UI (HTML/JS) | `www/admin-dashboard.html` (admin metrics), `www/dashboard.html` (simpler dashboard) |
| HTTP request parsing | `include/network/http_request.h` and `src/network/http_request.cpp` |
| WebSocket handling | `include/handlers/websocket_handler.h` and `src/handlers/websocket_handler.cpp` |
| HTTP/2 handling | `include/handlers/http2_handler.h` and `src/handlers/http2_handler.cpp` |

**Module layout** (matches the project’s C++ standards):

- **core** (`src/core/`, `include/core/`): Server, thread pool, main, shutdown, globals.
- **handlers** (`src/handlers/`, `include/handlers/`): File serving, HTTP/2, JSON API, WebSocket.
- **network** (`src/network/`, `include/network/`): HTTP request/response.
- **utils** (`src/utils/`, `include/utils/`): Shared helpers (if any).

Source files use **relative includes** to the `include/` directory (e.g. `#include "../../include/core/server.h"`). Adding a new module means adding its `src/` and `include/` directories and updating the Makefile.
