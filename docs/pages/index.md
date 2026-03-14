# High-Performance C++ Web Server

This documentation is for anyone who wants to understand or work with the C++ web server: you only need basic C++ (variables, functions, classes) and a rough idea of what a web server does. No deep networking or advanced C++ required.

A **web server** is a program that listens on a port (e.g. 8080), accepts connections from clients (browsers or tools like `curl`), and sends back files or data in response to **HTTP requests**.

This project is a modern HTTP/1.1 and HTTP/2 web server written in C++14. It uses a **thread pool** (a fixed set of worker threads and a queue of tasks), supports **WebSocket** for real-time two-way communication, and includes an admin dashboard for live metrics.

## Project highlights

- **High performance**: Multiple worker threads handle many requests at once; connections can be reused (Keep-Alive).
- **Modern protocols**: HTTP/1.1, HTTP/2, and WebSocket.
- **Real-time dashboard**: Web-based admin interface at `/admin-dashboard` with live metrics.
- **Production-oriented**: Optional TLS/SSL, graceful shutdown, structured error handling.
- **Developer-friendly**: REST-style API, JSON support, and this documentation.

## Quick demo

```bash
# Clone and build
git clone https://github.com/jaysheeldodia/web-server-http.git
cd web-server-http
make

# Start the server (default port 8080)
./bin/webserver

# In another terminal or browser
curl http://localhost:8080/
curl http://localhost:8080/api/stats
# Admin dashboard: http://localhost:8080/admin-dashboard
```

## Next steps

- [Concepts](concepts.md) – Basic ideas (HTTP, thread pool, TLS, WebSocket, etc.) and a “where to find what” map in the code.
- [Quick Start](getting-started.md) – Prerequisites, build, and first request.
- [Configuration](configuration.md) – Command-line options and tuning.
- [Architecture](architecture.md) – How the server is structured and how a request is handled.
