# Building from Source

The project uses a Makefile. You do not need CMake or another build system.

## Build commands

```bash
# Default build (optimized)
make

# Clean then build
make clean && make

# Faster build using all CPU cores
make -j$(nproc)

# Debug build (symbols, no optimization)
make debug

# Release build (high optimization)
make release
```

There is no `make DEBUG=1` or `make RELEASE=1`; use `make debug` and `make release` instead.

## Build targets

| Target | Description |
|--------|-------------|
| `make` or `make all` | Build the main server binary (`bin/webserver`) |
| `make debug` | Build with debug symbols and `-O0` |
| `make release` | Build with `-O3` and `-DNDEBUG` |
| `make test` | Run a quick test (start server, curl `/`, then stop) |
| `make clean` | Remove build artifacts (`build/`, `bin/`) |
| `make help` | List available targets |
| `make info` | Show project info (sources, compiler, flags) |

There is no `make tests` (build test executables) or `make install` or `make check-deps` in this Makefile. Optional tools (e.g. load tester) can be built with targets such as `make load_tester` if their sources are present; test sources live under `tests/unit/` (see [Testing](testing.md)).

## Dependencies

The server links against:

- **OpenSSL** (`libssl`, `libcrypto`) – TLS and crypto
- **nghttp2** – HTTP/2
- **pthread** – threading

You can check that libraries are available (e.g. `pkg-config --modversion openssl`, `pkg-config --modversion libnghttp2`). The Makefile does not provide a `check-deps` target.
