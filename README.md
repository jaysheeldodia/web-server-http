<h1 align="center">A Web Server in C++</h1>

A high-performance HTTP/1.1 and HTTP/2 web server written in C++. It uses a thread pool to handle many connections at once, supports WebSocket for real-time communication, and can serve static files and a REST-style API. Optional TLS is supported via OpenSSL.

## What it does

- Listens on a configurable port (default 8080) and serves HTTP requests.
- Supports HTTP/1.1 and HTTP/2, with WebSocket for two-way communication.
- Serves static files, JSON API endpoints, and a web-based admin dashboard at `/admin-dashboard` for live metrics.
- Uses a fixed thread pool and connection reuse (Keep-Alive) for efficiency.

## Build and run

Requirements: a C++14 compiler (e.g. g++), OpenSSL, and nghttp2. On many systems you can install the latter with your package manager (e.g. `openssl`, `nghttp2`).

```bash
make
./bin/webserver
```

The server runs on port 8080 by default. Use `./bin/webserver --help` for options. For a quick check:

```bash
curl http://localhost:8080/
curl http://localhost:8080/api/stats
```

Open `http://localhost:8080/admin-dashboard` in a browser for the admin interface.

## Documentation

Full documentation (architecture, configuration, API, building, testing) is in the `docs/` directory. To build and view it locally:

```bash
pip install mkdocs mkdocs-material
mkdocs serve
```

Then open the URL shown in the terminal. The same docs can be built for production with `mkdocs build` (output in `site/`).

## License

See the repository for license information.
