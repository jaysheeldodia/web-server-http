---
layout: none
---
# C++ WebServer Documentation

Welcome to the documentation for the high-performance C++ WebServer project. The docs are written in Markdown and built with **MkDocs**.

## Build the documentation

```bash
make docs
# Output: site/ (static HTML)
# Or: mkdocs serve  (local preview at http://127.0.0.1:8000)
```

## Quick navigation (source pages)

| Section | Page |
|---------|------|
| [Home / Overview](pages/index.md) | Overview and quick demo |
| [Concepts](pages/concepts.md) | Basic ideas and "where to find what" in the code |
| [Quick Start](pages/getting-started.md) | Prerequisites, build, first request |
| [Installation](pages/installation.md) | Platform-specific setup |
| [Configuration](pages/configuration.md) | Command-line options (`-d`/`--docroot`, `-t`, etc.) |
| [Architecture](pages/architecture.md) | Module layout and request flow |
| [Thread Pool](pages/thread-pool.md) | How the server handles concurrent requests |
| [REST API](pages/api-rest.md) | `/api/stats`, `/api/users`, `/api/docs` |
| [WebSocket API](pages/api-websocket.md) | `ws://localhost:8080/ws` and message types |
| [Admin Dashboard](pages/admin-dashboard.md) | `/admin-dashboard` vs `/dashboard` |
| [Building](pages/building.md) | Makefile targets (`make`, `make debug`, `make release`, `make test`) |
| [Testing](pages/testing.md) | How to run tests and where test code lives |
| [Benchmarks](pages/benchmarks.md) | Observing performance and using the dashboard |

## Quick start (server)

1. Install dependencies: `build-essential`, `libssl-dev`, `libnghttp2-dev` (or equivalent).
2. `make` then `./bin/webserver`.
3. Open `http://localhost:8080/admin-dashboard` for the admin UI, or `curl http://localhost:8080/api/stats` for the API.

For full details and beginner-friendly explanations, start with [pages/index.md](pages/index.md) and [pages/concepts.md](pages/concepts.md).
