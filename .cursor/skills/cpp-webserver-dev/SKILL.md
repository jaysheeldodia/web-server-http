---
name: cpp-webserver-dev
description: Guides development of the C++ HTTP/2 webserver (OpenSSL, nghttp2, TLS, WebSocket). Use when adding handlers, fixing server/core/network code, changing the build, or working with HTTP/2, ALPN, static files, JSON API, or admin dashboard.
---

# C++ HTTP/2 WebServer – Project Skill

## Project Summary

High-performance C++ webserver: C++14, multi-threaded, HTTP/1.1 and HTTP/2 (ALPN), TLS, WebSocket, static file serving, JSON API, admin dashboard. Build: Makefile; output: `bin/webserver`. Docs: `docs/docs.md`, `PROJECT_STRUCTURE.md`.

## Layout and Includes

- **Sources**: `src/core/`, `src/handlers/`, `src/network/`, `src/utils/`. Headers in `include/` with same module names.
- **Includes**: From `src/<module>/file.cpp` use `#include "../../include/<module>/header.h"`. From `include/<module>/file.h` use `#include "../other_module/header.h"` or `"same_module.h"`. No `-I` in Makefile; relative only.

## Build and Run

```bash
make                    # or make all
make debug              # debug symbols, -O0
make release            # -O3
make clean && make all
./bin/webserver         # default port 8080, doc root ./www
```

Dashboard: `http://localhost:8080/admin-dashboard`. Dependencies: OpenSSL, nghttp2; install e.g. `libssl-dev`, `libnghttp2-dev`.

## Adding a Handler

1. Add `include/handlers/new_handler.h` and `src/handlers/new_handler.cpp`.
2. In `.cpp`: `#include "../../include/handlers/new_handler.h"`. In `.h`: cross-includes like `#include "../core/globals.h"` if needed.
3. Wire the handler in `include/core/server.h` and `src/core/server.cpp` (include, member if needed, dispatch in request handling).
4. Rebuild with `make`; new files in existing modules are picked up by wildcards.

## Protocols and Features

- **HTTP/2**: ALPN, server push, stream priority; see `http2_handler`, nghttp2 usage.
- **TLS**: SSL_CTX, cert/key paths, ALPN for h2; graceful shutdown closes SSL cleanly.
- **WebSocket**: Dedicated handler; upgrade and frame handling in `websocket_handler`.
- **Static/JSON**: `FileHandler` (MIME, `document_root`), `JsonHandler` for API.

## Testing

- `make test`: starts server briefly, curl check, then kill.
- Scripts in `scripts/`: TLS and HTTP/2 tests; use `timeout` and assume server on 8080 unless noted.
- Unit-style sources under `tests/unit/`; build targets may live in Makefile or `tools/`.

## Conventions (Summary)

- C++14 only; header guards `#ifndef FOO_H` / `#define FOO_H` / `#endif`.
- Thread safety: mutex/atomic and shutdown coordination (ShutdownCoordinator, global flags).
- When changing build: keep `CXXFLAGS`/`LDFLAGS` baseline (C++14, -lssl -lcrypto -lnghttp2 -pthread); see `.cursor/rules/` for full conventions.
